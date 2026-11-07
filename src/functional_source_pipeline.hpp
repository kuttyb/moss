#pragma once

#include "functional_ir.hpp"

#include <string>
#include <cstdint>
#include <vector>

namespace moss {

struct ParsedFunctionalStage {
  FunctionalNodeKind kind = FunctionalNodeKind::Source;
  std::string raw;
  std::vector<std::string> arguments;
  std::uint32_t arena_index = UINT32_MAX;
  std::uint32_t expression_id = UINT32_MAX;
  std::vector<std::uint32_t> argument_expression_ids;
};

struct ParsedFunctionalPipeline {
  std::string source;
  std::vector<ParsedFunctionalStage> stages;
  std::uint32_t arena_index = UINT32_MAX;
  std::uint32_t expression_id = UINT32_MAX;
};

}
