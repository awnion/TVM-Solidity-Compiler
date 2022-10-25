/*
 * Copyright 2018-2022 TON DEV SOLUTIONS LTD.
 *
 * Licensed under the  terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License.
 *
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the  GNU General Public License for more details at: https://www.gnu.org/licenses/gpl-3.0.html
 */
/**
 * @author TON Labs <connect@tonlabs.io>
 * @date 2019
 */


#include <boost/range/adaptor/reversed.hpp>

#include <libsolidity/ast/ASTForward.h>

#include "TVMCommons.hpp"
#include "TVMConstants.hpp"
#include "TVMTypeChecker.hpp"

using namespace solidity::frontend;
using namespace solidity::langutil;
using namespace solidity::util;
using namespace std;

TVMTypeChecker::TVMTypeChecker(langutil::ErrorReporter& _errorReporter) :
	m_errorReporter{_errorReporter}
{

}

void TVMTypeChecker::checkOverrideAndOverload() {
	std::set<CallableDeclaration const*> overridedFunctions;
	std::set<CallableDeclaration const*> functions;
	std::map<uint32_t, CallableDeclaration const*> funcId2Decl;
	for (ContractDefinition const* cd : contractDefinition->annotation().linearizedBaseContracts | boost::adaptors::reversed) {
		for (FunctionDefinition const *f : cd->definedFunctions()) {

			if (f->functionID().has_value()) {
				uint32_t id = f->functionID().value();
				if (funcId2Decl.count(id) != 0) {
					CallableDeclaration const *f2 = funcId2Decl.at(id);
					std::set<CallableDeclaration const*> bf = getAllBaseFunctions(f);
					std::set<CallableDeclaration const*> bf2 = getAllBaseFunctions(f2);
					if (bf.count(f2) == 0 && bf2.count(f) == 0) {
						m_errorReporter.typeError(
							f->location(),
							SecondarySourceLocation().append("Declaration of the function with the same function ID: ", funcId2Decl.at(id)->location()),
							"Two functions have the same functionID.");
					}
				} else {
					funcId2Decl[id] = f;
				}
			}

			if (f->isConstructor() || f->isReceive() || f->isFallback() || f->isOnTickTock()) {
				continue;
			}

			FunctionDefinitionAnnotation &annotation = f->annotation();
			if (!annotation.baseFunctions.empty()) {
				overridedFunctions.insert(f);
				for (CallableDeclaration const *base : annotation.baseFunctions) {
					auto baseFunction = to<FunctionDefinition>(base);
					overridedFunctions.insert(base);
					if ((!f->functionID().has_value() && baseFunction->functionID()) || (f->functionID().has_value() && !baseFunction->functionID())) {
						m_errorReporter.typeError(
								f->location(),
								SecondarySourceLocation().append("Declaration of the base function: ", baseFunction->location()),
								"Both override and base functions should have functionID if it is defined for one of them.");
					} else if (f->functionID().has_value() && f->functionID() != baseFunction->functionID()) {
						m_errorReporter.typeError(
							f->location(),
							SecondarySourceLocation().append("Declaration of the base function: ", baseFunction->location()),
							"Override function should have functionID = " + toString(baseFunction->functionID().value()) + ".");
					}

					if (baseFunction->isResponsible() != f->isResponsible()) {
						m_errorReporter.typeError(
								f->location(),
								SecondarySourceLocation().append("Declaration of the base function: ", baseFunction->location()),
								"Both override and base functions should be marked as responsible or not");
					}

					if ((!f->functionID().has_value() && baseFunction->functionID()) || (f->functionID().has_value() && !baseFunction->functionID())) {
						m_errorReporter.typeError(
								f->location(),
								SecondarySourceLocation().append("Declaration of the base function: ",
																 baseFunction->location()),
								"Both override and base functions should have functionID if it is defined for one of them.");
					}

					if ((f->internalMsg() ^ baseFunction->internalMsg()) || (f->externalMsg() ^ baseFunction->externalMsg())) {
						m_errorReporter.typeError(
								f->location(),
								SecondarySourceLocation().append("Declaration of the base function: ",
																 baseFunction->location()),
								"Both override and base functions should be marked as internalMsg or externalMsg.");
					}
				}
			}
			functions.insert(f);
		}
	}


	std::set<std::pair<CallableDeclaration const*, CallableDeclaration const*>> used{};
	for (CallableDeclaration const* f : functions) {
		if (!f->isPublic()) {
			continue;
		}
		if (overridedFunctions.count(f)) {
			continue;
		}
		for (CallableDeclaration const* ff : functions) {
			if (!ff->isPublic()) {
				continue;
			}
			if (overridedFunctions.count(ff) || f == ff) {
				continue;
			}
			if (f->name() == ff->name()) {
				if (used.count(std::make_pair(f, ff)) == 0) {
					m_errorReporter.typeError(
						f->location(),
						SecondarySourceLocation().append("Another overloaded function is here:", ff->location()),
						"Function overloading is not supported for public functions.");
					used.insert({f, ff});
					used.insert({ff, f});
				}
			}
		}
	}
}

void TVMTypeChecker::check_onCodeUpgrade(FunctionDefinition const& f) {
	const std::string s = "\nfunction onCodeUpgrade(...) (internal|private) { /*...*/ }";
	if (!f.returnParameters().empty()) {
		m_errorReporter.typeError(f.returnParameters().at(0)->location(), "Function mustn't return any parameters. Expected function signature:" + s);
	}
	if (f.isPublic()) {
		m_errorReporter.typeError(f.location(), "Bad function visibility. Expected function signature:" + s);
	}
}

bool TVMTypeChecker::visit(VariableDeclaration const& _node) {
	if (_node.isStateVariable() && _node.type()->category() == Type::Category::TvmSlice) {
		m_errorReporter.typeError(_node.location(), "This type can't be used for state variables.");
	}
	return true;
}

bool TVMTypeChecker::visit(const Mapping &_mapping) {
    if (auto keyType = to<UserDefinedTypeName>(&_mapping.keyType())) {
        if (keyType->annotation().type->category() == Type::Category::Struct) {
            auto structType = to<StructType>(_mapping.keyType().annotation().type);
            int bitLength = 0;
            StructDefinition const& structDefinition = structType->structDefinition();
            for (const auto& member : structDefinition.members()) {
                TypeInfo ti {member->type()};
                if (!ti.isNumeric) {
					m_errorReporter.typeError(
						_mapping.keyType().location(),
						SecondarySourceLocation().append("Bad field: ", member->location()),
						"If struct type is used as a key type for mapping, then "
						"fields of the struct must have integer, boolean, fixed bytes or enum type"
					);
                }
                bitLength += ti.numBits;
            }
            if (bitLength > TvmConst::CellBitLength) {
				m_errorReporter.typeError(_mapping.keyType().location(), "If struct type is used as a key type for mapping, then "
											   "struct must fit in " + toString(TvmConst::CellBitLength) + " bits");
            }
        }
    }
    return true;
}

bool TVMTypeChecker::visit(const FunctionDefinition &f) {
	if (f.functionID().has_value() && f.functionID().value() == 0) {
		m_errorReporter.typeError(f.location(), "functionID can't be equal to zero because this value is reserved for receive function.");
	}
	if (f.functionID().has_value() && (!f.isPublic() && f.name() != "onCodeUpgrade")) {
		m_errorReporter.typeError(f.location(), "Only public/external functions and function `onCodeUpgrade` can have functionID.");
	}
	if (f.functionID().has_value() && (f.isReceive() || f.isFallback() || f.isOnTickTock() || f.isOnBounce())) {
		m_errorReporter.typeError(f.location(), "functionID isn't supported for receive, fallback, onBounce and onTickTock functions.");
	}

	if (f.isInline() && f.isPublic()) {
		m_errorReporter.typeError(f.location(), "Inline function should have private or internal visibility");
	}
	if (f.name() == "onCodeUpgrade") {
		check_onCodeUpgrade(f);
	}

	if (f.name() == "afterSignatureCheck") {
		const std::string s = "\nExpected follow format: \"function afterSignatureCheck(TvmSlice restOfMessageBody, TvmCell message) private inline returns (TvmSlice) { /*...*/ }\"";
		if (
				f.parameters().size() != 2 ||
				f.parameters().at(0)->type()->category() != Type::Category::TvmSlice ||
				f.parameters().at(1)->type()->category() != Type::Category::TvmCell
				) {
			m_errorReporter.typeError(f.location(),
									  "Unexpected function parameters." + s);
		}
		if (f.returnParameters().size() != 1 ||
			f.returnParameters().at(0)->type()->category() != Type::Category::TvmSlice) {
			m_errorReporter.typeError(f.location(), "Should return TvmSlice." + s);
		}
		if (f.visibility() != Visibility::Private) {
			m_errorReporter.typeError(f.location(), "Should be marked as private." + s);
		}
		if (!f.isInline()) {
			m_errorReporter.typeError(f.location(), "Should be marked as inline." + s);
		}
	}

	return true;
}

bool TVMTypeChecker::visit(IndexRangeAccess const& indexRangeAccess) {
	Type const *baseType = indexRangeAccess.baseExpression().annotation().type;
	auto baseArrayType = to<ArrayType>(baseType);
	if (baseType->category() != Type::Category::Array || !baseArrayType->isByteArray()) {
		m_errorReporter.typeError(indexRangeAccess.location(), "Index range access is available only for bytes.");
	}
	return true;
}

bool TVMTypeChecker::visit(ContractDefinition const& cd) {
	contractDefinition = &cd;

	checkOverrideAndOverload();

	return true;
}

void TVMTypeChecker::endVisit(ContractDefinition const& ) {
	contractDefinition = nullptr;
}
