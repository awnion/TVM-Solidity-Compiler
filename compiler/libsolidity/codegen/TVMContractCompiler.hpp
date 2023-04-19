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
 * AST to TVM bytecode contract compiler
 */

#pragma once

#include "TVM.h"
#include "TVMPusher.hpp"
#include "TvmAst.hpp"

namespace solidity::frontend {

class TVMCompilerContext;

class TVMConstructorCompiler: private boost::noncopyable {
	StackPusher& m_pusher;
	std::map<ContractDefinition const*, std::vector<ContractDefinition const*>> path;
	std::vector<ContractDefinition const*> dfsOrder;
	std::map<ContractDefinition const*, bool> used;
	std::map<ContractDefinition const*, std::vector<ASTPointer<Expression>> const*> m_args;

public:
	explicit TVMConstructorCompiler(StackPusher& pusher);
	void dfs(ContractDefinition const* c);
	Pointer<Function> generateConstructors();
private:
	void c4ToC7WithMemoryInitAndConstructorProtection();
};

class TVMContractCompiler: private boost::noncopyable {
public:
	static void printFunctionIds(ContractDefinition const& contract, PragmaDirectiveHelper const& pragmaHelper);
	static void printPrivateFunctionIds(
		ContractDefinition const& contract,
		std::vector<std::shared_ptr<SourceUnit>> _sourceUnits,
		PragmaDirectiveHelper const& pragmaHelper
	);
	static void generateABI(
		const std::string& fileName,
		ContractDefinition const* contract,
		std::vector<PragmaDirective const *> const& pragmaDirectives
	);
	static void generateCodeAndSaveToFile(
		const std::string& fileName,
		ContractDefinition const& contract,
		std::vector<std::shared_ptr<SourceUnit>> _sourceUnits,
		PragmaDirectiveHelper const &pragmaHelper
	);
	static Pointer<Contract> generateContractCode(
		ContractDefinition const* contract,
		std::vector<std::shared_ptr<SourceUnit>> _sourceUnits,
		PragmaDirectiveHelper const& pragmaHelper
	);
	static void optimizeCode(Pointer<Contract>& c);
private:
	static void fillInlineFunctions(TVMCompilerContext& ctx, ContractDefinition const* contract);
};

}	// end solidity::frontend
