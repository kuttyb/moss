#include "../../src/abi_v10_records.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) return 2;
  const std::string bytes(std::istreambuf_iterator<char>{input}, {});
  try {
    const auto sealed = moss::abi_v10::parse(bytes);
    moss::abi_v10::validate_complete_interface(sealed);
    std::size_t found = 0;
    for (const auto& record : sealed.value.semantic_contract) {
      if (!moss::abi_v10::is_key_proof_payload(record.payload)) continue;
      const auto proof = moss::abi_v10::read_key_proof(record);
      const auto& key_id = proof.key_type.identity;
      if (proof.key_type.kind != moss::abi_v10::WireTypeKind::Nominal ||
          !moss::abi_v10::nominal_template_id_valid(key_id) ||
          proof.equality_callable_id != key_id + ":method:isequal" ||
          proof.hash_callable_id != key_id + ":method:hash" ||
          proof.provenance_id != key_id ||
          proof.law_contract_version != "key-laws-v1") return 1;
      ++found;
      std::cout << key_id << '\t' << record.id << '\n';
    }
    return found == 1 ? 0 : 1;
  } catch (const moss::abi_v10::Error& error) {
    std::cerr << error.code << ": " << error.what() << '\n';
    return 1;
  }
}
