#include "abi_v10_records.hpp"

#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static std::string read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), {}};
}

static void write_file(const std::string& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output.good());
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  assert(output.good());
}

int main(int argc, char** argv) {
  assert(argc == 5);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string module_name = argv[3];
  const std::string callable_name = argv[4];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const auto old_digest = sealed.digest;

  auto legacy_counts = [](const moss::abi_v10::Interface& interface) {
    std::map<std::string, std::size_t> counts;
    for (const auto& record : interface.semantic_contract) {
      if (record.payload.empty() || record.payload.front() == 1 ||
          (static_cast<unsigned char>(record.payload.front()) == 2 &&
           moss::abi_v10::hex_hash(record.id)) ||
          moss::abi_v10::is_key_proof_payload(record.payload))
        continue;
      std::istringstream lines(record.payload);
      std::string line;
      std::vector<std::string> scopes;
      while (std::getline(lines, line)) {
        if (line.rfind("export fn ", 0) == 0) {
          std::istringstream header(line.substr(10));
          std::string callable;
          header >> callable;
          scopes.clear();
          scopes.push_back(std::move(callable));
        } else if (line.find("generic_dependency_begin ") !=
                   std::string::npos) {
          std::istringstream header(line.substr(
              line.find("generic_dependency_begin ") + 25));
          std::string callable;
          header >> std::quoted(callable);
          assert(!callable.empty());
          scopes.push_back(std::move(callable));
        } else if (line.find("generic_dependency_end") !=
                   std::string::npos) {
          assert(scopes.size() > 1 &&
                 "generic dependency end has no matching private scope");
          scopes.pop_back();
        }
        if (line.find("generic_stmt ") != std::string::npos &&
            !scopes.empty())
          ++counts[scopes.back()];
      }
      assert(scopes.size() <= 1 &&
             "unterminated generic dependency scope");
    }
    return counts;
  };

  const auto counts_before = legacy_counts(sealed.value);
  const auto found_before = counts_before.find(callable_name);
  const std::size_t before = found_before == counts_before.end()
      ? 0 : found_before->second;
  assert(before > 0 && "requested callable had no legacy generic_stmt");
  std::size_t removed = 0;

  for (auto& record : sealed.value.semantic_contract) {
    if (record.payload.empty() || record.payload.front() == 1 ||
        (static_cast<unsigned char>(record.payload.front()) == 2 &&
         moss::abi_v10::hex_hash(record.id)) ||
        moss::abi_v10::is_key_proof_payload(record.payload))
      continue;
    std::istringstream lines(record.payload);
    std::string line, replacement;
    std::vector<std::string> scopes;
    while (std::getline(lines, line)) {
      if (line.rfind("export fn ", 0) == 0) {
        std::istringstream header(line.substr(10));
        std::string callable;
        header >> callable;
        scopes.clear();
        scopes.push_back(std::move(callable));
      } else if (line.find("generic_dependency_begin ") !=
                 std::string::npos) {
        std::istringstream header(line.substr(
            line.find("generic_dependency_begin ") + 25));
        std::string callable;
        header >> std::quoted(callable);
        assert(!callable.empty());
        scopes.push_back(std::move(callable));
      } else if (line.find("generic_dependency_end") != std::string::npos) {
        assert(scopes.size() > 1 &&
               "generic dependency end has no matching private scope");
        scopes.pop_back();
      }
      if (line.find("generic_stmt ") != std::string::npos &&
          !scopes.empty() && scopes.back() == callable_name) {
        ++removed;
        continue;
      }
      replacement += line + "\n";
    }
    assert(scopes.size() <= 1 && "unterminated generic dependency scope");
    record.payload = std::move(replacement);
  }
  assert(removed == before && "not all targeted legacy generic_stmt records were removed");
  const auto counts_after = legacy_counts(sealed.value);
  assert(counts_after.find(callable_name) == counts_after.end() &&
         "targeted callable still has a legacy generic_stmt");
  for (const auto& [callable, count] : counts_before) {
    if (callable == callable_name) continue;
    const auto found_after = counts_after.find(callable);
    const std::size_t after = found_after == counts_after.end()
        ? 0 : found_after->second;
    assert(after == count && "poisoning changed an unrelated callable body");
  }
  const auto rewritten = moss::abi_v10::encode(sealed.value);
  moss::abi_v10::validate_complete_interface(moss::abi_v10::decode(rewritten));
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
  std::cout << "removed " << removed << " generic_stmt records from "
            << callable_name << '\n';
}
