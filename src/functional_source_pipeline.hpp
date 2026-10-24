#pragma once

#include "functional_ir.hpp"

#include <string>
#include <vector>

namespace moss {

struct ParsedFunctionalStage {
  FunctionalNodeKind kind = FunctionalNodeKind::Source;
  std::string raw;
  std::vector<std::string> arguments;
};

struct ParsedFunctionalPipeline {
  std::string source;
  std::vector<ParsedFunctionalStage> stages;
};

}
