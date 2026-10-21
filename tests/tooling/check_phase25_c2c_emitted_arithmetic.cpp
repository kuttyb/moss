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

  std::vector<moss::abi_v10::Summary> summaries;
  for (const auto& record : iface.semantic_contract) {
    if (record.payload.empty() || !moss::abi_v10::typed_summary_payload(record.payload) ||
        !moss::abi_v10::hex_hash(record.id)) continue;
    moss::abi_v10::validate_summary(record);
    auto summary = moss::abi_v10::read_summary(record.payload);
    if (summary.callable_id == compute_id)
      summaries.push_back(std::move(summary));
  }
  assert(summaries.size() == 1);
  assert(summaries.front().requirements.size() == 2);

  std::size_t body_count = 0;
  for (const auto& record : iface.private_semantic_dependencies)
    if (record.id == "checked-body-v1:" + compute_id) {
      auto body = moss::abi_v10::read_checked_body_record(record.payload);
      assert(body.callable_id == compute_id &&
             body.semantic_hash == summaries.front().body_semantic_hash);
      ++body_count;
    }
  assert(body_count == 1);

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
  assert(generic_statement_counts["compute"] == 0);
  std::cout << "PASS canonical staged ABI-v10 compute summary/body=1/1, "
               "compute generic_stmt=0\n";
}
