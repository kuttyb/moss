#pragma once

#include "ast.hpp"

#include <iosfwd>
#include <set>
#include <string>
#include <vector>

namespace moss {

// These are the narrow frontend entry points used by the compiler driver.
Program parse_source(std::istream& input, const std::string& source_file = {},
                     const std::set<std::string>& standard_aliases = {});
Program parse_source_lines(
    std::vector<Line> lines,
    const std::set<std::string>& standard_aliases = {});

}
