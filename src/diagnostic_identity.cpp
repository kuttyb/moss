#include "diagnostic_identity.hpp"

#include "ast.hpp"

namespace moss {

std::string semantic_identity_near_line(const Program* program, int line) {
  if (!program) return {};
  std::string best;
  int best_line = -1;
  auto consider = [&](int candidate_line, const std::string& identity) {
    if (candidate_line <= line && candidate_line >= best_line) {
      best_line = candidate_line;
      best = identity;
    }
  };
  for (const auto& function : program->functions)
    consider(function.line,
             "fn:" + function.name + "@" + std::to_string(function.line));
  for (const auto& object : program->objects) {
    consider(object.line,
             "type:" + object.name + "@" + std::to_string(object.line));
    for (const auto& method : object.methods)
      consider(method.line, "method:" + object.name + "." + method.name +
                               "@" + std::to_string(method.line));
  }
  for (const auto& domain : program->domains) {
    consider(domain.line,
             "domain:" + domain.name + "@" + std::to_string(domain.line));
    for (const auto& handler : domain.handlers)
      consider(handler.line, "handler:" + domain.name + "." + handler.name +
                                "@" + std::to_string(handler.line));
  }
  for (const auto& test : program->tests)
    consider(test.line, test.semantic_identity);
  for (const auto& benchmark : program->benchmarks)
    consider(benchmark.line, benchmark.semantic_identity);
  if (program->main)
    consider(program->main->line,
             "main@" + std::to_string(program->main->line));
  for (const auto& pipeline : program->functional_pipelines)
    consider(pipeline.line, pipeline.semantic_identity);
  return best;
}

std::string semantic_identity_near_source_line(
    const Program* program, const std::string& source_file, int line) {
  if (!program) return {};
  std::string best;
  int best_line = -1;
  auto consider = [&](int candidate_line, const std::string& candidate_source,
                      const std::string& identity) {
    if (candidate_source == source_file && candidate_line <= line &&
        candidate_line >= best_line) {
      best_line = candidate_line;
      best = identity;
    }
  };
  for (const auto& function : program->functions)
    consider(function.line, function.source_file,
             function.semantic_callable_id.empty()
                 ? "fn:" + function.name + "@" + std::to_string(function.line)
                 : function.semantic_callable_id);
  for (const auto& object : program->objects) {
    consider(object.line, object.source_file,
             "type:" + object.name + "@" + std::to_string(object.line));
    for (const auto& method : object.methods)
      consider(method.line, method.source_file,
               "method:" + object.name + "." + method.name + "@" +
                   std::to_string(method.line));
  }
  for (const auto& enumeration : program->enums)
    consider(enumeration.line, enumeration.source_file,
             "enum:" + enumeration.name + "@" +
                 std::to_string(enumeration.line));
  for (const auto& domain : program->domains) {
    consider(domain.line, domain.source_file,
             "domain:" + domain.name + "@" + std::to_string(domain.line));
    for (const auto& handler : domain.handlers)
      consider(handler.line, handler.source_file,
               "handler:" + domain.name + "." + handler.name + "@" +
                   std::to_string(handler.line));
  }
  for (const auto& test : program->tests)
    consider(test.line, test.source_file, test.semantic_identity);
  for (const auto& benchmark : program->benchmarks)
    consider(benchmark.line, benchmark.source_file,
             benchmark.semantic_identity);
  if (program->main)
    consider(program->main->line, program->main->source_file,
             "main@" + std::to_string(program->main->line));
  return best;
}

} // namespace moss
