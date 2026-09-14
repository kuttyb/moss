#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace moss {
struct CompileError : std::runtime_error {
  int line;
  std::string code;
  std::string semantic_identity;
  std::string symbol;
  std::string source_file;
  CompileError(int ln, const std::string& msg, std::string diagnostic_code = {})
      : std::runtime_error(msg), line(ln), code(std::move(diagnostic_code)) {}
};

struct Warning {
  int line;
  std::string message;
  std::string code = "MOSS_WARNING";
  std::string source_file;
};
} // namespace moss
