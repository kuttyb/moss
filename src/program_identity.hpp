#pragma once

#include <string>

namespace moss {

struct Program;

std::string program_module_identity(const Program& program,
                                   const std::string& module);

} // namespace moss
