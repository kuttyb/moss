#include "abi_v10_records.hpp"

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
  std::size_t removed = 0;

  for (auto& record : sealed.value.semantic_contract) {
    if (record.payload.empty() || record.payload.front() == 1 ||
        (static_cast<unsigned char>(record.payload.front()) == 2 &&
         moss::abi_v10::hex_hash(record.id)) ||
        moss::abi_v10::is_key_proof_payload(record.payload))
      continue;
    std::istringstream lines(record.payload);
    std::string line, replacement, active_callable;
    while (std::getline(lines, line)) {
      if (line.rfind("export fn ", 0) == 0) {
        std::istringstream header(line.substr(10));
        header >> active_callable;
      }
      if (line.find("generic_stmt ") != std::string::npos &&
          active_callable == callable_name) {
        ++removed;
        continue;
      }
      replacement += line + "\n";
    }
    record.payload = std::move(replacement);
  }
  assert(removed > 0 && "requested callable had no legacy generic_stmt");
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
