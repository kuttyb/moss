#include "checked_body_ir.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) return 3;
  const std::string bytes((std::istreambuf_iterator<char>(input)), {});
  moss::TypeContext context;
  const auto variable = moss::Type::var(moss::TypeVarId{0, 0, 0});
  const auto vector_type = moss::Type::builtin("vector", {variable});
  const auto int_type = moss::Type::primitive("int");
  const std::vector<moss::TypeRef> parameters =
      std::string(argv[2]) == "count"
          ? std::vector<moss::TypeRef>{vector_type}
          : std::string(argv[2]) == "pass"
              ? std::vector<moss::TypeRef>{}
              : std::vector<moss::TypeRef>{vector_type, int_type};
  const auto result = std::string(argv[2]) == "count" ||
          std::string(argv[2]) == "pass" ? int_type : variable;
  const auto hash = "body-semantic-v1:sha256:" +
      moss::source_free_contract_sha256(bytes);
  try {
    (void)moss::checked_ir::decode_h3_checked_body_v1(
        bytes, context, parameters, result, hash);
  } catch (const std::invalid_argument&) {
    std::cout << "pre-D2 H3 consumer rejected new checked-body " << argv[2]
              << " node\n";
    return 0;
  }
  std::cerr << "pre-D2 H3 consumer unexpectedly accepted new node\n";
  return 1;
}
