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
  assert(argc == 5);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string mode = argv[3];
  const std::string provider_name = argv[4];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const std::string old_digest = sealed.digest;
  const std::string callable_id = "callable-v1:" + sealed.value.module_id +
      ":fn:insert_and_read";
  auto summary_position = std::find_if(
      sealed.value.semantic_contract.begin(),
      sealed.value.semantic_contract.end(), [&](const auto& record) {
    return moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::hex_hash(record.id) &&
        moss::abi_v10::read_summary(record.payload).callable_id == callable_id;
  });
  assert(summary_position != sealed.value.semantic_contract.end());
  auto summary = moss::abi_v10::read_summary(summary_position->payload);
  auto body_position = std::find_if(
      sealed.value.private_semantic_dependencies.begin(),
      sealed.value.private_semantic_dependencies.end(), [&](const auto& record) {
    return record.id == "checked-body-v1:" + callable_id;
  });
  assert(body_position != sealed.value.private_semantic_dependencies.end());

  if (mode == "index-access") {
    assert(summary.requirements.size() == 2);
    summary.requirements.front().access = moss::abi_v10::Access::Read;
  } else if (mode == "value-relation") {
    assert(summary.requirements.size() == 2);
    auto& relation = summary.requirements.back();
    assert(relation.operation == "builtin:index-write-value" &&
           relation.arguments.size() == 2);
    relation.arguments.front() = relation.arguments.back();
  } else if (mode == "missing-may-panic") {
    auto& tags = summary.observable_effect_tags;
    const auto found = std::find(tags.begin(), tags.end(), "may_panic");
    assert(found != tags.end());
    tags.erase(found);
  } else if (mode == "missing-body") {
    sealed.value.private_semantic_dependencies.erase(body_position);
    body_position = sealed.value.private_semantic_dependencies.end();
  } else {
    assert(false && "unknown R3B Map mutation mode");
  }

  if (mode != "missing-body")
    *summary_position = moss::abi_v10::summary_record(summary);
  std::sort(sealed.value.semantic_contract.begin(),
            sealed.value.semantic_contract.end(), [](const auto& left,
                                                       const auto& right) {
    return left.id < right.id;
  });
  std::sort(sealed.value.private_semantic_dependencies.begin(),
            sealed.value.private_semantic_dependencies.end(),
            [](const auto& left, const auto& right) {
    return left.id < right.id;
  });
  const auto rewritten = moss::abi_v10::encode(sealed.value);
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  auto archive = read_file(archive_path);
  const auto old_marker = moss::abi_v10::provider_marker(provider_name,
                                                          old_digest);
  const auto new_marker = moss::abi_v10::provider_marker(provider_name,
                                                          new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker);
       position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements > 0);
  moss::abi_v10::validate_selected_rlib(archive, provider_name, new_digest);
  write_file(interface_path, rewritten);
  write_file(archive_path, archive);
}
