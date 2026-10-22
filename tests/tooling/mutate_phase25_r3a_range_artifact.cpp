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
  const std::string root_id = "callable-v1:" + sealed.value.module_id +
      ":fn:from_range";
  auto find_summary = [&]() {
    for (auto* records : {&sealed.value.semantic_contract,
                          &sealed.value.private_semantic_dependencies})
      for (auto it = records->begin(); it != records->end(); ++it)
        if (moss::abi_v10::hex_hash(it->id) &&
            moss::abi_v10::typed_summary_payload(it->payload) &&
            moss::abi_v10::read_summary(it->payload).callable_id == root_id)
          return std::make_pair(records, it);
    assert(false && "from_range checked summary is missing");
    return std::make_pair(
        static_cast<std::vector<moss::abi_v10::Record>*>(nullptr),
        std::vector<moss::abi_v10::Record>::iterator{});
  };
  auto find_body = [&]() {
    auto& records = sealed.value.private_semantic_dependencies;
    return std::find_if(records.begin(), records.end(), [&](const auto& record) {
      return record.id == "checked-body-v1:" + root_id;
    });
  };
  auto [summary_records, summary_position] = find_summary();
  assert(summary_records && summary_position != summary_records->end());
  auto summary = moss::abi_v10::read_summary(summary_position->payload);
  auto body_position = find_body();
  assert(body_position != sealed.value.private_semantic_dependencies.end());
  auto body = moss::abi_v10::read_checked_body_record(body_position->payload);

  if (mode == "iterate-result") {
    assert(summary.requirements.size() == 1 && summary.dependencies.size() == 1);
    summary.requirements.front().result = {
        moss::abi_v10::WireTypeKind::BoundVar, {}, {}, 0};
    *summary_position = moss::abi_v10::summary_record(summary);
  } else if (mode == "assert-effect") {
    auto& tags = summary.observable_effect_tags;
    assert(std::find(tags.begin(), tags.end(), "may_panic") != tags.end());
    tags.erase(std::remove(tags.begin(), tags.end(), "may_panic"), tags.end());
    *summary_position = moss::abi_v10::summary_record(summary);
  } else if (mode == "missing-call-dependency") {
    assert(summary.dependencies.size() == 1 && body.dependencies.size() == 1);
    summary.dependencies.clear();
    body.dependencies.clear();
    *summary_position = moss::abi_v10::summary_record(summary);
    body_position->payload = moss::abi_v10::checked_body_record(body).payload;
  } else {
    assert(false && "unknown R3A range mutation mode");
  }

  std::sort(summary_records->begin(), summary_records->end(),
            [](const auto& left, const auto& right) {
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
  const auto old_marker = moss::abi_v10::provider_marker(provider_name, old_digest);
  const auto new_marker = moss::abi_v10::provider_marker(provider_name, new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker); position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements > 0);
  moss::abi_v10::validate_selected_rlib(archive, provider_name, new_digest);
  write_file(interface_path, rewritten);
  write_file(archive_path, archive);
}
