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

void refresh_provider_marker(const std::string& archive_path,
                             const std::string& old_digest,
                             const std::string& new_digest) {
  auto archive = read_file(archive_path);
  const auto old_marker = moss::abi_v10::provider_marker("Service", old_digest);
  const auto new_marker = moss::abi_v10::provider_marker("Service", new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker); position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements > 0);
  moss::abi_v10::validate_selected_rlib(archive, "Service", new_digest);
  write_file(archive_path, archive);
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 4);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string mode = argv[3];
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const std::string old_digest = sealed.digest;
  const std::string supported = "callable-v1:" + sealed.value.module_id +
      ":fn:supported";
  const std::string unsupported = "callable-v1:" + sealed.value.module_id +
      ":fn:unsupported";

  if (mode == "missing-supported-body") {
    const std::string body_id = "checked-body-v1:" + supported;
    auto& records = sealed.value.private_semantic_dependencies;
    const auto before = records.size();
    records.erase(std::remove_if(records.begin(), records.end(),
        [&](const auto& record) { return record.id == body_id; }), records.end());
    assert(records.size() + 1 == before);
    assert(std::any_of(sealed.value.semantic_contract.begin(),
        sealed.value.semantic_contract.end(), [&](const auto& record) {
      return moss::abi_v10::hex_hash(record.id) && !record.payload.empty() &&
          moss::abi_v10::typed_summary_payload(record.payload) &&
          moss::abi_v10::read_summary(record.payload).callable_id == supported;
    }));
    assert(std::any_of(sealed.value.semantic_contract.begin(),
        sealed.value.semantic_contract.end(),
        [&](const auto& record) {
      return record.payload.find("export fn supported ") == 0 &&
          record.payload.find("generic_stmt") != std::string::npos;
    }));
  } else if (mode == "forged-unsupported-provenance") {
    auto& records = sealed.value.provenance;
    auto selected = records.end();
    for (auto current = records.begin(); current != records.end(); ++current) {
      const auto provenance = moss::abi_v10::read_provenance(current->payload);
      if (provenance.declaration_id != unsupported) continue;
      assert(selected == records.end());
      selected = current;
    }
    assert(selected != records.end());
    auto provenance = moss::abi_v10::read_provenance(selected->payload);
    provenance.declaration_id += ":forged";
    *selected = moss::abi_v10::provenance_record(provenance);
    std::sort(records.begin(), records.end(), [](const auto& left,
                                                  const auto& right) {
      return left.id < right.id;
    });
  } else {
    assert(false && "unknown mixed admission artifact mutation mode");
  }

  const auto rewritten = moss::abi_v10::encode(sealed.value);
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  refresh_provider_marker(archive_path, old_digest, new_digest);
  write_file(interface_path, rewritten);
}
