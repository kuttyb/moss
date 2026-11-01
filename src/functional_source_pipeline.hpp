#pragma once

#include "functional_ir.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace moss {

namespace est { class Arena; using ExprId = std::uint32_t; }

struct ParsedFunctionalStage {
  FunctionalNodeKind kind = FunctionalNodeKind::Source;
  std::string raw;
  std::vector<std::string> arguments;
  std::vector<est::ExprId> argument_nodes;
  const est::Arena* expression_arena = nullptr;
  std::string expression_source;
};

struct ParsedFunctionalPipeline {
  std::string source;
  std::vector<ParsedFunctionalStage> stages;
  const est::Arena* expression_arena = nullptr;
  est::ExprId source_node = 0;
  std::string expression_source;
};

}
