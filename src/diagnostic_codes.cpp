#include "diagnostic_codes.hpp"

namespace moss {

std::string diagnostic_code_for_message(const std::string& message) {
  if (message.find("duplicate function") != std::string::npos ||
      message.find("duplicate domain") != std::string::npos ||
      message.find("duplicate object type") != std::string::npos ||
      message.find("duplicate trait") != std::string::npos ||
      message.find("duplicate exported name") != std::string::npos)
    return "DUPLICATE_SYMBOL";
  if (message.find("duplicate test name") != std::string::npos)
    return "TEST_DISCOVERY_ERROR";
  if (message.find("duplicate benchmark name") != std::string::npos)
    return "BENCHMARK_CONFIGURATION_ERROR";
  if (message.rfind("assert", 0) == 0 ||
      message.find("assertEqual") != std::string::npos ||
      message.find("test assertions") != std::string::npos)
    return "TEST_ASSERTION_CONFIGURATION_ERROR";
  if (message.find("recursive local call cycle") != std::string::npos)
    return "RECURSION_CYCLE";
  if (message.find("conflicting domain types across control-flow paths") !=
      std::string::npos)
    return "TYPE_BRANCH_CONFLICT";
  if (message.find("was transferred") != std::string::npos ||
      message.find("was consumed") != std::string::npos)
    return "OWNERSHIP_USE_AFTER_CONSUME";
  if (message.find("conflicting") != std::string::npos &&
      message.find("access") != std::string::npos)
    return "OWNERSHIP_CONFLICTING_ACCESS";
  if (message.find("cannot mutate immutable local") != std::string::npos)
    return "IMMUTABLE_LOCAL_MUTATION";
  if (message.find("cannot infer") != std::string::npos)
    return "TYPE_INFERENCE_FAILED";
  if (message.find("unknown") != std::string::npos)
    return "UNKNOWN_SYMBOL_OR_TYPE";
  if (message.find("expects") != std::string::npos ||
      message.find("type mismatch") != std::string::npos ||
      message.find("has type") != std::string::npos)
    return "TYPE_MISMATCH";
  if (message.find("functional") != std::string::npos)
    return "FUNCTIONAL_SEMANTIC_ERROR";
  return "MOSS_COMPILE_ERROR";
}

} // namespace moss
