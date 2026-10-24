#include "program_identity.hpp"

#include "ast.hpp"
#include "diagnostics.hpp"

namespace moss {

std::string program_module_identity(const Program& program,
                                   const std::string& module) {
  auto found = program.module_identities.find(module);
  if (found != program.module_identities.end()) return found->second;
  if (module == "std") return module_identity("moss-std", "0.1.0", "std");
  if (program.package_identities)
    throw CompileError(0, "module '" + module + "' has no package identity",
                       "MODULE_IDENTITY_UNRESOLVED");
  return module_identity("__standalone__", "0.0.0",
                         module.empty() ? "main" : module);
}

} // namespace moss
