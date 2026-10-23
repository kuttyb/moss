#include <cassert>
#include <string>

#include "native_cache_identity.hpp"

int main() {
  const auto colliding_fast_hash = [](const std::string&) {
    return std::string("injected-collision");
  };
  const auto first = moss::native_cache::artifact_identity(
      "provider bytes A", colliding_fast_hash);
  const auto second = moss::native_cache::artifact_identity(
      "provider bytes B", colliding_fast_hash);
  assert(first != second);
  assert(first.find("fast:injected-collision:sha256:") == 0);
  assert(second.find("fast:injected-collision:sha256:") == 0);
}
