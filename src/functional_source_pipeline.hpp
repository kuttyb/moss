#pragma once

#include "functional_ir.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace moss {

namespace est { class Arena; using ExprId = std::uint32_t; }

struct ParsedFunctionalStage {
  FunctionalNodeKind kind = FunctionalNodeKind::Source;
  std::string raw;
  std::vector<std::string> arguments;
  std::vector<est::ExprId> argument_nodes;
  est::ExprId expression_node = UINT32_MAX;
  const est::Arena* expression_arena = nullptr;
  std::string expression_source;
  std::string checked_output_type;
};

struct ParsedFunctionalPipeline {
  std::string source;
  std::vector<ParsedFunctionalStage> stages;
  std::shared_ptr<est::Arena> owned_expression_arena;
  const est::Arena* expression_arena = nullptr;
  est::ExprId source_node = 0;
  std::string expression_source;
};

}
