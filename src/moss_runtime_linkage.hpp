#pragma once

// Compiler-owned shared runtime linkage and artifact management unit (Workstream B5).
//
// Provides clean, independently testable APIs for:
// - Resolving and building the shared `moss_runtime` rlib
// - Validating artifact seals and runtime crate identities
// - Verifying ABI and runtime compatibility between providers and consumers
// - Generating compiler/linker flags (--extern moss_runtime=... -L dependency=...)
// - Computing cache keys and detecting provider-contract changes

#include "source_free_abi_v10.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace moss::abi_v10 {

struct RuntimeCrateIdentity {
  std::string name = "moss_runtime";
  std::string version = "0.1.0";
  std::string artifact_hash;  // sha256:<64 hex>
  std::string toolchain_fingerprint;
  std::string profile = "debug";
};

inline std::string compute_file_sha256(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw Error("MOSS_INTERNAL_OR_IO_ERROR", "Cannot open artifact for hashing: " + path.string());
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  return "sha256:" + source_free_contract_sha256(ss.str());
}

inline std::string query_rustc_fingerprint(const std::string& rustc_bin = "rustc") {
  std::string command = rustc_bin + " -V";
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) return "rustc-unknown";
  char buffer[256];
  std::string result;
  if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result = buffer;
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
      result.pop_back();
    }
  }
  pclose(pipe);
  return result.empty() ? "rustc-unknown" : result;
}

inline std::pair<std::filesystem::path, RuntimeCrateIdentity>
build_or_resolve_moss_runtime(
    const std::filesystem::path& runtime_crate_dir,
    const std::filesystem::path& output_dir,
    const std::string& profile = "debug",
    const std::string& rustc_bin = "rustc") {
  std::filesystem::create_directories(output_dir);
  const auto rlib_path = output_dir / "libmoss_runtime.rlib";

  // Check if rebuilding is necessary based on source vs artifact modification time
  const auto src_path = runtime_crate_dir / "src" / "lib.rs";
  if (!std::filesystem::exists(src_path)) {
    throw Error("MOSS_INTERNAL_OR_IO_ERROR",
                "moss_runtime source does not exist: " + src_path.string());
  }

  bool needs_build = !std::filesystem::exists(rlib_path);
  if (!needs_build) {
    // Check if any source file in runtime_crate_dir/src is newer than rlib
    auto rlib_time = std::filesystem::last_write_time(rlib_path);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(runtime_crate_dir / "src")) {
      if (entry.is_regular_file() && entry.path().extension() == ".rs") {
        if (std::filesystem::last_write_time(entry.path()) > rlib_time) {
          needs_build = true;
          break;
        }
      }
    }
  }

  if (needs_build) {
    std::string opt_flag = (profile == "release") ? "-O" : "-g";
    std::string command = rustc_bin + " --edition=2021 --crate-type=rlib --crate-name=moss_runtime " +
                          opt_flag + " " + src_path.string() + " -o " + rlib_path.string();
    int ret = std::system(command.c_str());
    if (ret != 0 || !std::filesystem::exists(rlib_path)) {
      throw Error("BUILD_BACKEND_ERROR",
                  "Failed to build shared moss_runtime crate: exit code " + std::to_string(ret));
    }
  }

  RuntimeCrateIdentity identity;
  identity.name = "moss_runtime";
  identity.version = "0.1.0";
  identity.profile = profile;
  identity.artifact_hash = compute_file_sha256(rlib_path);
  identity.toolchain_fingerprint = query_rustc_fingerprint(rustc_bin);

  return {rlib_path, identity};
}

inline std::vector<std::string>
generate_compiler_runtime_flags(const std::filesystem::path& rlib_path) {
  std::vector<std::string> flags;
  flags.push_back("-L");
  flags.push_back("dependency=" + rlib_path.parent_path().string());
  flags.push_back("--extern");
  flags.push_back("moss_runtime=" + rlib_path.string());
  return flags;
}

inline void verify_runtime_compatibility(const RuntimeCrateIdentity& provider_runtime,
                                        const RuntimeCrateIdentity& consumer_runtime) {
  if (provider_runtime.name != consumer_runtime.name) {
    throw Error("SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
                "Runtime crate name mismatch: provider uses '" + provider_runtime.name +
                    "', consumer uses '" + consumer_runtime.name + "'");
  }
  if (provider_runtime.version != consumer_runtime.version) {
    throw Error("SOURCE_FREE_ABI_REBUILD_REQUIRED",
                "Runtime crate version mismatch: provider uses '" + provider_runtime.version +
                    "', consumer uses '" + consumer_runtime.version + "'");
  }
  if (provider_runtime.artifact_hash != consumer_runtime.artifact_hash) {
    throw Error("SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
                "Shared moss_runtime artifact content hash mismatch: provider bound to " +
                    provider_runtime.artifact_hash + ", consumer bound to " +
                    consumer_runtime.artifact_hash);
  }
}

enum class ContractChangeKind {
  None,
  InterfaceHashOnly,
  SemanticChange,
  BinaryChange
};

inline ContractChangeKind detect_contract_change(const SealedInterface& before,
                                                 const SealedInterface& after) {
  if (before.binary_hash != after.binary_hash) {
    return ContractChangeKind::BinaryChange;
  }
  if (before.semantic_hash != after.semantic_hash) {
    return ContractChangeKind::SemanticChange;
  }
  if (before.interface_hash != after.interface_hash || before.digest != after.digest) {
    return ContractChangeKind::InterfaceHashOnly;
  }
  return ContractChangeKind::None;
}

inline std::string compute_cache_dependency_key(
    const SealedInterface& interface,
    const RuntimeCrateIdentity& runtime_id,
    const std::string& compiler_version,
    const std::string& backend_fingerprint) {
  std::string material = compiler_version;
  material += "\nbackend " + backend_fingerprint;
  material += "\nschema_version 10\nnative_abi 10";
  material += "\nruntime " + runtime_id.name + " " + runtime_id.version + " " + runtime_id.artifact_hash;
  material += "\nmodule " + interface.value.module_id;
  material += "\nprovider_contract_digest " + interface.digest;
  material += "\nbinary_contract_hash " + interface.binary_hash;
  material += "\nsemantic_contract_hash " + interface.semantic_hash;
  material += "\ninterface_hash " + interface.interface_hash;
  for (const auto& imp : interface.value.imports) {
    material += "\nimport " + imp.module_id + " " + imp.binary_hash + " " + imp.semantic_hash;
  }
  return "sha256:" + source_free_contract_sha256(material);
}

}  // namespace moss::abi_v10
