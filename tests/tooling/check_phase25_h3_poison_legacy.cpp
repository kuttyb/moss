#include "abi_v10_records.hpp"

#include <cassert>
#include <fstream>
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
  assert(argc == 4);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string module_name = argv[3];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const auto old_digest = sealed.digest;
  std::size_t removed = 0;
  for (auto& record : sealed.value.semantic_contract) {
    if (record.payload.empty() || record.payload.front() == 1) continue;
    std::istringstream lines(record.payload);
    std::string line, replacement;
    while (std::getline(lines, line)) {
      if (line.find("generic_stmt ") != std::string::npos) {
        ++removed;
        continue;
      }
      replacement += line + "\n";
    }
    record.payload = std::move(replacement);
  }
  assert(removed == 2);
  const auto rewritten = moss::abi_v10::encode(sealed.value);
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
