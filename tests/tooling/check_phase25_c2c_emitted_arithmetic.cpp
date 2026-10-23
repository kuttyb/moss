#include "abi_v10_records.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1], std::ios::binary);
  assert(input.good());
  const std::string bytes((std::istreambuf_iterator<char>(input)), {});
  const auto sealed = moss::abi_v10::decode(bytes);
  moss::abi_v10::validate_complete_interface(sealed);
  const auto& iface = sealed.value;
  const std::string compute_id =
      "callable-v1:" + iface.module_id + ":fn:compute";
  const std::string nested_id =
      "callable-v1:" + iface.module_id + ":fn:nested";
  const std::string subtract_id =
      "callable-v1:" + iface.module_id + ":fn:subtract";

  std::map<std::string, moss::abi_v10::Summary> summaries;
  for (const auto& record : iface.semantic_contract) {
    if (record.payload.empty() || !moss::abi_v10::typed_summary_payload(record.payload) ||
        !moss::abi_v10::hex_hash(record.id)) continue;
    moss::abi_v10::validate_summary(record);
    auto summary = moss::abi_v10::read_summary(record.payload);
    if (summary.callable_id == compute_id ||
        summary.callable_id == nested_id ||
        summary.callable_id == subtract_id)
      assert(summaries.emplace(summary.callable_id,
                               std::move(summary)).second);
  }
  assert(summaries.size() == 3);
  assert(summaries.at(compute_id).requirements.size() == 2);
  assert(summaries.at(nested_id).requirements.size() == 3);
  assert(summaries.at(subtract_id).requirements.size() == 2);

  std::map<std::string, std::size_t> body_counts;
  for (const auto& record : iface.private_semantic_dependencies) {
    if (record.id.rfind("checked-body-v1:", 0) == 0) {
      auto body = moss::abi_v10::read_checked_body_record(record.payload);
      if (body.callable_id == compute_id || body.callable_id == nested_id ||
          body.callable_id == subtract_id) {
        assert(summaries.count(body.callable_id) == 1 &&
               body.semantic_hash ==
                   summaries.at(body.callable_id).body_semantic_hash);
        ++body_counts[body.callable_id];
      }
    }
  }
  assert(body_counts[compute_id] == 1 && body_counts[nested_id] == 1 &&
         body_counts[subtract_id] == 1);

  std::map<std::string, std::size_t> generic_statement_counts;
  for (const auto& record : iface.semantic_contract) {
    if (record.payload.rfind("export fn ", 0) != 0) continue;
    std::istringstream lines(record.payload);
    std::string first_line, line, name;
    std::getline(lines, first_line);
    std::istringstream declaration(first_line.substr(10));
    declaration >> name;
    while (std::getline(lines, line))
      if (line.rfind("  generic_stmt ", 0) == 0)
        ++generic_statement_counts[name];
  }
  for (const auto* name : {"compute", "nested", "subtract"})
    assert(generic_statement_counts[name] == 0);
  std::cout << "PASS canonical staged ABI-v10 arithmetic summaries/bodies=3/3; "
               "all three legacy generic_stmt bodies removed\n";
}
