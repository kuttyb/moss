#include "abi_v10_records.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

static std::string read_file(const char* path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot read interface");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
  if ((argc != 3 && argc != 4) ||
      (std::string(argv[1]) != "inspect" && std::string(argv[1]) != "tamper"))
    return 2;
  try {
    const auto text = read_file(argv[2]);
    auto sealed = moss::abi_v10::decode(text);
    if (std::string(argv[1]) == "inspect") {
      for (const auto& record : sealed.value.semantic_contract)
        if (moss::abi_v10::is_key_proof_payload(record.payload)) {
          moss::abi_v10::validate_key_proof(record);
          std::cout << record.id << "\n";
        }
      return 0;
    }
    if (argc != 4) return 2;
    bool changed = false;
    for (auto& record : sealed.value.semantic_contract) {
      if (!moss::abi_v10::is_key_proof_payload(record.payload)) continue;
      auto proof = moss::abi_v10::read_key_proof(record);
      proof.equality_semantic_hash = moss::abi_v10::hash(
          "key-proof-test-mutation", proof.equality_semantic_hash);
      record = moss::abi_v10::key_proof_record(proof);
      changed = true;
      break;
    }
    if (!changed) throw std::runtime_error("no key proof to mutate");
    std::sort(sealed.value.semantic_contract.begin(),
              sealed.value.semantic_contract.end(),
              [](const auto& left, const auto& right) {
                return left.id < right.id;
              });
    const auto mutant = moss::abi_v10::encode(sealed.value);
    std::ofstream output(argv[3], std::ios::binary);
    if (!output) throw std::runtime_error("cannot write mutated interface");
    output << mutant;
    if (!output) throw std::runtime_error("cannot finish mutated interface");
    std::cout << sealed.digest << "\n"
              << moss::abi_v10::parse(mutant).digest << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
