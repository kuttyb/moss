#pragma once

#include <string>

namespace moss {

struct Program;

std::string semantic_identity_near_line(const Program* program, int line);
std::string semantic_identity_near_source_line(
    const Program* program, const std::string& source_file, int line);

} // namespace moss
