#include "abi_v10_records.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), {}};
}

void write_file(const std::string& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output.good());
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  assert(output.good());
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 4);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string mode = argv[3];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const std::string old_digest = sealed.digest;
  const std::string callable = "callable-v1:" + sealed.value.module_id +
      ":fn:contains";
  auto summary_record = std::find_if(sealed.value.semantic_contract.begin(),
      sealed.value.semantic_contract.end(), [&](const auto& record) {
    return moss::abi_v10::hex_hash(record.id) && !record.payload.empty() &&
        moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::read_summary(record.payload).callable_id == callable;
  });
  assert(summary_record != sealed.value.semantic_contract.end());
  auto summary = moss::abi_v10::read_summary(summary_record->payload);
  if (mode == "bad-requirement") {
    summary.requirements.front().operation = "builtin:forged-index";
  } else if (mode == "bad-effect") {
    summary.observable_effect_tags.clear();
  } else {
    assert(false && "unknown D4 collection artifact mutation mode");
  }
  *summary_record = moss::abi_v10::summary_record(summary);
  std::sort(sealed.value.semantic_contract.begin(),
            sealed.value.semantic_contract.end(),
            [](const auto& left, const auto& right) {
    return left.id < right.id;
  });

  const auto rewritten = moss::abi_v10::encode(sealed.value);
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  auto archive = read_file(archive_path);
  const auto old_marker = moss::abi_v10::provider_marker("collections", old_digest);
  const auto new_marker = moss::abi_v10::provider_marker("collections", new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker); position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements > 0);
  moss::abi_v10::validate_selected_rlib(archive, "collections", new_digest);
  write_file(interface_path, rewritten);
  write_file(archive_path, archive);
}
