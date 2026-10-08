#pragma once

#include <string>

namespace moss {

// Compiler-owned semantic markers, not an identifier naming convention.
// User nominal types such as _Payload and _method_valueExtra are concrete.
inline bool internal_semantic_placeholder(const std::string& type) {
  auto has_prefix = [&](const char* prefix) {
    return type.rfind(prefix, 0) == 0;
  };
  return type == "_" || type == "_value" || type == "_none" ||
      type == "_method_value" || has_prefix("_generic:") ||
      has_prefix("_element:") || has_prefix("_field:") ||
      has_prefix("_method_result:") ||
      has_prefix("_specialized_result:") ||
      has_prefix("_iterator_element:") ||
      has_prefix("_functional_result:") ||
      has_prefix("_functional_element:") ||
      has_prefix("_functional_collection:") ||
      has_prefix("_functional_callable_result:");
}

} // namespace moss
