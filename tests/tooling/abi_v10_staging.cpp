#include "../../src/source_free_abi_v10.hpp"

#include <cassert>
#include <iostream>
#include <string>

using moss::abi_v10::Error;
using moss::abi_v10::Interface;

static void expect_error(const std::string& input, const std::string& code) {
  try {
    (void)moss::abi_v10::decode(input);
  } catch (const Error& error) {
    assert(error.code == code);
    return;
  }
  assert(false && "expected ABI-v10 diagnostic");
}

static void replace_once(std::string& text, const std::string& old_text,
                         const std::string& new_text) {
  const auto at = text.find(old_text);
  assert(at != std::string::npos);
  text.replace(at, old_text.size(), new_text);
}

static std::string archive_with_marker(const std::string& marker,
                                       bool metadata = false) {
  auto member = marker;
  std::string header(60, ' ');
  const auto name = metadata ? "provider.rmeta/" : "provider.o/";
  header.replace(0, std::char_traits<char>::length(name), name);
  header.replace(48, std::to_string(member.size()).size(),
                 std::to_string(member.size()));
  header.replace(58, 2, "`\n");
  return "!<arch>\n" + header + member + (member.size() & 1 ? "\n" : "");
}

int main() {
  Interface value;
  value.module_id = "module-v1:boxes@1.0.0::Boxes";
  value.compiler = "moss-test";
  value.backend_fingerprint = "rustc-test";
  value.backend_rustc = "rustc 1.0";
  value.imports.push_back({"module-v1:base@1.0.0::Base",
                           "sha256:" + std::string(64, 'a'),
                           "sha256:" + std::string(64, 'b')});
  value.binary_contract.push_back({"nominal:Box", "provider-record\nfield 0\n"});
  value.semantic_contract.push_back({"summary:fresh", "bound-var 0\n"});
  value.private_semantic_dependencies.push_back({"helper:private", "body-ir\n"});
  value.provenance.push_back({"source:boxes", "boxes.moss:1:1"});

  const auto sealed = moss::abi_v10::encode(value);
  const auto decoded = moss::abi_v10::decode(sealed);
  assert(decoded.value.module_id == value.module_id);
  assert(decoded.value.semantic_contract[0].payload == "bound-var 0\n");
  assert(decoded.binary_hash != decoded.semantic_hash);
  assert(moss::abi_v10::encode(decoded.value) == sealed);

  auto changed = sealed;
  replace_once(changed, "bound-var 0", "bound-var 1");
  expect_error(changed, "SOURCE_FREE_CONTRACT_INVALID");
  changed = sealed;
  replace_once(changed, "native_abi 10", "native_abi 9");
  expect_error(changed, "SOURCE_FREE_ABI_REBUILD_REQUIRED");
  changed = sealed;
  replace_once(changed, "schema_version 10", "schema_version 9");
  expect_error(changed, "SOURCE_FREE_ABI_REBUILD_REQUIRED");
  changed = sealed;
  replace_once(changed, "moss-module-interface-v3", "moss-module-interface-v2");
  expect_error(changed, "SOURCE_FREE_ABI_REBUILD_REQUIRED");
  changed = sealed + "trailing";
  expect_error(changed, "SOURCE_FREE_CONTRACT_INVALID");

  value.binary_contract.push_back(value.binary_contract.front());
  try {
    (void)moss::abi_v10::encode(value);
    assert(false && "duplicate record ID accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_CONTRACT_INVALID");
  }
  value.binary_contract.pop_back();
  value.semantic_contract[0].payload = std::string("\xc0\xaf", 2);
  try {
    (void)moss::abi_v10::encode(value);
    assert(false && "invalid UTF-8 accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_CONTRACT_INVALID");
  }

  const auto marker = moss::abi_v10::provider_marker("Boxes", decoded.digest);
  moss::abi_v10::validate_selected_rlib(archive_with_marker(marker), "Boxes",
                                         decoded.digest);
  try {
    moss::abi_v10::validate_selected_rlib(archive_with_marker(marker, true),
                                           "Boxes", decoded.digest);
    assert(false && "metadata-only seal accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");
  }
  try {
    moss::abi_v10::validate_selected_rlib(
        archive_with_marker(marker + marker), "Boxes", decoded.digest);
    assert(false && "duplicate seal accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");
  }
  std::cout << "ABI-v10 staged container tests passed\n";
}
