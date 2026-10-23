#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "source_free_abi_v9.hpp"

namespace moss::native_cache {

// The fast hash supports compact diagnostics and transient lookup only. Cache
// trust always includes a collision-resistant digest of the exact bytes.
template <class FastHash>
std::string artifact_identity(std::string_view bytes, FastHash&& fast_hash) {
  const std::string material(bytes);
  return "fast:" + std::forward<FastHash>(fast_hash)(material) +
         ":sha256:" + source_free_contract_sha256(bytes);
}

}  // namespace moss::native_cache
