#pragma once

#include <string>

namespace moss {

struct Program;

std::string semantic_identity_near_line(const Program* program, int line);

} // namespace moss
