#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace moss {
struct DiagnosticEntity {
  std::string kind;
  std::string name;
  std::string semantic_identity;
  std::string expression;
  std::string access;
  int argument_index = 0;
};

struct DiagnosticRelated {
  std::string source_file;
  int line = 0;
  int column = 1;
  std::string message;
};

// Optional compiler-owned teaching facts.  Empty fields mean that a diagnostic
// has not yet been specialized; the ordinary stable error contract remains
// valid.  Both human and JSON renderers consume this one representation.
struct TeachingDiagnostic {
  std::string rule_id;
  std::string rule_summary;
  std::string cause_kind;
  std::vector<DiagnosticEntity> entities;
  std::vector<DiagnosticRelated> related;
  std::string guidance_kind;
  std::string guidance_summary;

  bool empty() const {
    return rule_id.empty() && cause_kind.empty() && entities.empty() &&
        related.empty() && guidance_kind.empty();
  }
};

struct CompileError : std::runtime_error {
  int line;
  std::string code;
  std::string semantic_identity;
  std::string symbol;
  std::string source_file;
  TeachingDiagnostic teaching;
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
