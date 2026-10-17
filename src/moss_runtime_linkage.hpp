#pragma once

// Compiler-owned shared runtime linkage and artifact management unit (Workstream B1 & B5).
//
// Provides clean, deterministic, content-addressed APIs for:
// - Resolving and building the shared `moss_runtime` rlib with content-addressed cache
// - Validating artifact seals and runtime crate identities
// - Verifying ABI and runtime compatibility between providers and consumers
// - Generating compiler/linker flags (--extern moss_runtime=... -L dependency=...)
// - Computing cache keys and detecting provider-contract changes
// - Safe process execution without shell-command injection vulnerabilities

#include "source_free_abi_v10.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace moss::abi_v10 {

struct ProcessResult {
  int exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
};

// Safe process execution with direct execvp (no shell expansion or injection)
inline ProcessResult run_command_safe(const std::vector<std::string>& args) {
  if (args.empty()) return {-1, "", "empty command"};

  int stdout_pipe[2];
  int stderr_pipe[2];
  if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
    return {-1, "", "failed to create pipes"};
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    return {-1, "", "fork failed"};
  }

  if (pid == 0) {
    // Child process: connect pipes to standard streams
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    std::vector<char*> c_args;
    c_args.reserve(args.size() + 1);
    for (const auto& a : args) {
      c_args.push_back(const_cast<char*>(a.c_str()));
    }
    c_args.push_back(nullptr);

    execvp(c_args[0], c_args.data());
    _exit(127);
  }

  // Parent process
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  std::string stdout_out;
  std::string stderr_out;
  char buf[4096];

  struct pollfd fds[2];
  fds[0].fd = stdout_pipe[0];
  fds[0].events = POLLIN;
  fds[1].fd = stderr_pipe[0];
  fds[1].events = POLLIN;

  int active_pipes = 2;
  while (active_pipes > 0) {
    int ret = poll(fds, 2, -1);
    if (ret <= 0) break;
    for (int i = 0; i < 2; ++i) {
      if (fds[i].fd >= 0 && (fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
        ssize_t bytes_read = read(fds[i].fd, buf, sizeof(buf));
        if (bytes_read > 0) {
          if (i == 0) stdout_out.append(buf, bytes_read);
          else stderr_out.append(buf, bytes_read);
        } else {
          close(fds[i].fd);
          fds[i].fd = -1;
          --active_pipes;
        }
      }
    }
  }

  int status = 0;
  waitpid(pid, &status, 0);
  int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return {exit_code, std::move(stdout_out), std::move(stderr_out)};
}

struct RuntimeCrateIdentity {
  std::string name = "moss_runtime";
  std::string version = "0.1.0";
  std::string build_key;      // sha256:<64 hex> composite key
  std::string artifact_hash;  // sha256:<64 hex> hash of .rlib file
  std::string source_hash;    // sha256:<64 hex> content hash of source files
  std::string toolchain_fingerprint;
  std::string target_arch;
  std::string profile = "debug";
  std::vector<std::string> compiler_flags;
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

struct RustcDetails {
  std::string release;
  std::string host_target;
  std::string commit_hash;
  std::string full_fingerprint;
};

inline RustcDetails query_rustc_details(const std::string& rustc_bin = "rustc") {
  ProcessResult res = run_command_safe({rustc_bin, "-vV"});
  RustcDetails details;
  if (res.exit_code != 0 || res.stdout_text.empty()) {
    details.release = "unknown";
    details.host_target = "unknown";
    details.full_fingerprint = "rustc-unknown";
    return details;
  }
  details.full_fingerprint = res.stdout_text;
  std::istringstream stream(res.stdout_text);
  std::string line;
  while (std::getline(stream, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.rfind("release: ", 0) == 0) {
      details.release = line.substr(9);
    } else if (line.rfind("host: ", 0) == 0) {
      details.host_target = line.substr(6);
    } else if (line.rfind("commit-hash: ", 0) == 0) {
      details.commit_hash = line.substr(13);
    }
  }
  return details;
}

inline std::string query_rustc_fingerprint(const std::string& rustc_bin = "rustc") {
  return query_rustc_details(rustc_bin).full_fingerprint;
}

// Compute deterministic content hash across all source files and build inputs.
// Ignores timestamps entirely; purely content-addressed.
inline std::string compute_runtime_source_hash(const std::filesystem::path& runtime_crate_dir) {
  const auto src_dir = runtime_crate_dir / "src";
  if (!std::filesystem::exists(src_dir)) {
    throw Error("MOSS_INTERNAL_OR_IO_ERROR",
                "moss_runtime src directory does not exist: " + src_dir.string());
  }

  std::map<std::string, std::string> file_contents;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(src_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".rs") {
      const auto rel_path = std::filesystem::relative(entry.path(), runtime_crate_dir).string();
      std::ifstream f(entry.path(), std::ios::binary);
      if (!f) {
        throw Error("MOSS_INTERNAL_OR_IO_ERROR",
                    "Cannot open runtime source file: " + entry.path().string());
      }
      std::ostringstream ss;
      ss << f.rdbuf();
      file_contents[rel_path] = ss.str();
    }
  }

  const auto cargo_toml = runtime_crate_dir / "Cargo.toml";
  if (std::filesystem::exists(cargo_toml) && std::filesystem::is_regular_file(cargo_toml)) {
    std::ifstream f(cargo_toml, std::ios::binary);
    if (f) {
      std::ostringstream ss;
      ss << f.rdbuf();
      file_contents["Cargo.toml"] = ss.str();
    }
  }

  std::string material = "moss_runtime_source_v1\n";
  for (const auto& pair : file_contents) {
    material += "file " + pair.first + " bytes " + std::to_string(pair.second.size()) + "\n";
    material += pair.second;
    material += "\n";
  }
  return "sha256:" + source_free_contract_sha256(material);
}

// Compute composite build key over source hash, toolchain, architecture, profile, and flags.
inline std::string compute_runtime_build_key(
    const std::string& crate_name,
    const std::string& crate_version,
    const std::string& source_hash,
    const std::string& toolchain_fingerprint,
    const std::string& target_arch,
    const std::string& profile,
    const std::vector<std::string>& flags) {
  std::string material = "moss_runtime_build_key_v1\n";
  material += "crate " + crate_name + " " + crate_version + "\n";
  material += "source " + source_hash + "\n";
  material += "toolchain " + toolchain_fingerprint + "\n";
  material += "target " + target_arch + "\n";
  material += "profile " + profile + "\n";
  material += "flags";
  for (const auto& f : flags) {
    material += " " + f;
  }
  material += "\n";
  return "sha256:" + source_free_contract_sha256(material);
}

// Content-addressed runtime resolution:
// Resolves or builds libmoss_runtime.rlib based on content hash, toolchain, flags, and profile.
// Corrupted or tampered artifacts are detected and rebuilt safely.
inline std::pair<std::filesystem::path, RuntimeCrateIdentity>
build_or_resolve_moss_runtime(
    const std::filesystem::path& runtime_crate_dir,
    const std::filesystem::path& output_dir,
    const std::string& profile = "debug",
    const std::string& rustc_bin = "rustc") {
  const auto src_path = runtime_crate_dir / "src" / "lib.rs";
  if (!std::filesystem::exists(src_path)) {
    throw Error("MOSS_INTERNAL_OR_IO_ERROR",
                "moss_runtime source does not exist: " + src_path.string());
  }

  const std::string source_hash = compute_runtime_source_hash(runtime_crate_dir);
  const RustcDetails rustc_details = query_rustc_details(rustc_bin);

  std::vector<std::string> compile_flags = {
      "--edition=2021",
      "--crate-type=rlib",
      "--crate-name=moss_runtime"
  };
  if (profile == "release") {
    compile_flags.push_back("-O");
  } else {
    compile_flags.push_back("-g");
  }

  const std::string build_key = compute_runtime_build_key(
      "moss_runtime", "0.1.0", source_hash, rustc_details.full_fingerprint,
      rustc_details.host_target, profile, compile_flags);

  // Content-addressed artifact directory based on profile and build key
  const std::string short_id = build_key.substr(7, 16);
  const auto cache_dir = output_dir / ("moss_runtime-" + profile + "-" + short_id);
  const auto rlib_path = cache_dir / "libmoss_runtime.rlib";
  const auto meta_path = cache_dir / "moss_runtime.meta";

  bool cache_valid = false;
  if (std::filesystem::exists(rlib_path) && std::filesystem::exists(meta_path)) {
    std::ifstream mf(meta_path);
    std::string meta_build_key, meta_source_hash, meta_artifact_hash;
    if (std::getline(mf, meta_build_key) &&
        std::getline(mf, meta_source_hash) &&
        std::getline(mf, meta_artifact_hash)) {
      if (meta_build_key == build_key && meta_source_hash == source_hash) {
        // Content-check the cached artifact to protect against corruption or tampering
        try {
          const std::string current_artifact_hash = compute_file_sha256(rlib_path);
          if (current_artifact_hash == meta_artifact_hash) {
            cache_valid = true;
          }
        } catch (const Error&) {
          cache_valid = false;
        }
      }
    }
  }

  if (!cache_valid) {
    std::filesystem::create_directories(cache_dir);
    // Safe execution without shell string injection
    std::vector<std::string> args = {rustc_bin};
    for (const auto& f : compile_flags) args.push_back(f);
    args.push_back(src_path.string());
    args.push_back("-o");
    args.push_back(rlib_path.string());

    ProcessResult res = run_command_safe(args);
    if (res.exit_code != 0 || !std::filesystem::exists(rlib_path)) {
      throw Error("BUILD_BACKEND_ERROR",
                  "Failed to build shared moss_runtime crate: exit code " +
                      std::to_string(res.exit_code) + "\n" + res.stderr_text);
    }

    const std::string artifact_hash = compute_file_sha256(rlib_path);
    std::ofstream mf(meta_path);
    mf << build_key << "\n"
       << source_hash << "\n"
       << artifact_hash << "\n"
       << rustc_details.full_fingerprint << "\n"
       << profile << "\n";
  }

  // Also maintain convenience rlib copy in output_dir
  const auto conv_rlib = output_dir / "libmoss_runtime.rlib";
  if (conv_rlib != rlib_path) {
    std::error_code ec;
    std::filesystem::remove(conv_rlib, ec);
    std::filesystem::copy_file(rlib_path, conv_rlib,
                               std::filesystem::copy_options::overwrite_existing, ec);
  }

  RuntimeCrateIdentity identity;
  identity.name = "moss_runtime";
  identity.version = "0.1.0";
  identity.build_key = build_key;
  identity.artifact_hash = compute_file_sha256(rlib_path);
  identity.source_hash = source_hash;
  identity.toolchain_fingerprint = rustc_details.full_fingerprint;
  identity.target_arch = rustc_details.host_target;
  identity.profile = profile;
  identity.compiler_flags = compile_flags;

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
  if (!provider_runtime.build_key.empty() && !consumer_runtime.build_key.empty() &&
      provider_runtime.build_key != consumer_runtime.build_key) {
    throw Error("SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH",
                "Shared moss_runtime build configuration mismatch: provider built with " +
                    provider_runtime.build_key + ", consumer built with " +
                    consumer_runtime.build_key);
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
  material += "\nruntime " + runtime_id.name + " " + runtime_id.version + " " +
              runtime_id.artifact_hash + " " + runtime_id.build_key;
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
