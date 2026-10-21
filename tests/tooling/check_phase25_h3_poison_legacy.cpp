#include "abi_v10_records.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

static std::string read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return std::string((std::istreambuf_iterator<char>(input)), {});
}

static void write_file(const std::string& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output.good());
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  assert(output.good());
}

int main(int argc, char** argv) {
  assert(argc == 5 || argc == 6);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string module_name = argv[3];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const auto old_digest = sealed.digest;
  std::size_t removed = 0;
  for (auto& record : sealed.value.semantic_contract) {
    const bool typed_summary_v2 = moss::abi_v10::hex_hash(record.id) &&
        !record.payload.empty() &&
        moss::abi_v10::typed_summary_payload(record.payload);
    if (record.payload.empty() || record.payload.front() == 1 ||
        typed_summary_v2 ||
        moss::abi_v10::is_key_proof_payload(record.payload))
      continue;
    std::istringstream lines(record.payload);
    std::string line, replacement;
    std::string callable;
    while (std::getline(lines, line)) {
      if (line.rfind("export fn ", 0) == 0) {
        std::istringstream header(line.substr(10));
        header >> callable;
      } else if (line.find("generic_dependency_begin ") !=
                 std::string::npos) {
        std::istringstream header(line.substr(
            line.find("generic_dependency_begin ") + 25));
        header >> std::quoted(callable);
      } else if (line.find("generic_dependency_end") != std::string::npos) {
        callable.clear();
      }
      if (line.find("generic_stmt ") != std::string::npos &&
          (callable == "fresh" || callable == "private_box" ||
           callable == "nested_private_box")) {
        ++removed;
        continue;
      }
      replacement += line + "\n";
    }
    record.payload = std::move(replacement);
  }
  if (removed != static_cast<std::size_t>(std::stoul(argv[4]))) {
    std::cerr << "legacy statements removed=" << removed << " expected="
              << argv[4] << '\n';
    return 2;
  }
  const bool drop_private = argc == 6 &&
      std::string(argv[5]) == "drop-private-box-body";
  if (drop_private) {
    auto& records = sealed.value.private_semantic_dependencies;
    const auto before = records.size();
    records.erase(std::remove_if(records.begin(), records.end(),
        [](const moss::abi_v10::Record& record) {
      return record.id.rfind("checked-body-v1:", 0) == 0 &&
          record.id.size() >= std::string(":fn:private_box").size() &&
          record.id.substr(record.id.size() -
              std::string(":fn:private_box").size()) == ":fn:private_box";
    }), records.end());
    assert(before == records.size() + 1);
  }
  const auto rewritten = moss::abi_v10::encode(sealed.value);
  if (!drop_private)
    moss::abi_v10::validate_complete_interface(
        moss::abi_v10::decode(rewritten));
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  auto archive = read_file(archive_path);
  const auto old_marker = moss::abi_v10::provider_marker(module_name, old_digest);
  const auto new_marker = moss::abi_v10::provider_marker(module_name, new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker);
       position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements >= 1);
  moss::abi_v10::validate_selected_rlib(archive, module_name, new_digest);
  write_file(interface_path, rewritten);
  write_file(archive_path, archive);
}
