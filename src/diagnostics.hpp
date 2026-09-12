#pragma once

#include <stdexcept>
#include <string>

namespace moss {
struct CompileError : std::runtime_error {
  int line;
  CompileError(int ln, const std::string& msg) : std::runtime_error(msg), line(ln) {}
};
} // namespace moss
