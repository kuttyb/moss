#include "native_clone_provenance.hpp"

#include <fstream>
#include <iostream>
#include <string>

using namespace moss;

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string source_path = argv[1];
  const std::string source = "fn main():\n  let x = copy(v)\n";
  {
    std::ofstream file(source_path, std::ios::binary);
    if (!file) return 2;
    file << source;
  }
  TypedCopyFactV1 typed;
  typed.est_arena_index = 1;
  typed.copy_occurrence_id = 10;
  typed.source_occurrence_id = 11;
  typed.statement_occurrence_id = 2;
  typed.specialization_context = "fn:first";
  typed.est_fact_context = "fn:first";
  typed.provenance = {source_path, 2, "module:test", "fn:main"};
  typed.copy_occurrence_span = {2, 17, 0, 21, 28};
  typed.source_occurrence_span = {2, 16, 0, 26, 27};
  typed.source_type = typed.result_type = Type::primitive("int");
  typed.required_source_access = Effect::Read;
  typed.creates_independent_owner = true;
  typed.availability = TypedCopyFactV1::Availability::Present;
  auto first = native_checked_copy_fact_from_typed(typed, source);
  typed.specialization_identity = "spec:second";
  typed.specialization_context = typed.est_fact_context = "fn:second";
  auto second = native_checked_copy_fact_from_typed(typed, source);
  const auto first_id = native_checked_copy_identity(first);
  const auto second_id = native_checked_copy_identity(second);
  const auto first_helper = "__moss_copy_value_" + first_id.substr(19, 16);
  const auto second_helper = "__moss_copy_value_" + second_id.substr(19, 16);
  const std::string rust =
      "fn " + first_helper + "(value: &i64) -> i64 { (*value).clone() }\n" +
      "/* Moss copy fact call: " + first_id + " */ " + first_helper + "(&v);\n" +
      "fn " + second_helper + "(value: &i64) -> i64 { (*value).clone() }\n" +
      "/* Moss copy fact call: " + second_id + " */ " + second_helper + "(&v);\n" +
      "fn runtime_clone(value: &i64) -> i64 { value.clone() } // fake .clone(\n";
  NativeCloneSourceCopyV3 source_row;
  source_row.copy_id = native_clone_source_copy_id(first.copy_occurrence_span);
  source_row.source_span = first.copy_occurrence_span;
  NativeCloneCheckedCopyFactV3 facts;
  facts.copy_id = source_row.copy_id;
  facts.facts = {first, second};
  auto manifest = build_native_clone_provenance_v3(
      "debug", rust, source, {source_row}, {facts});
  if (manifest.occurrences.size() != 3 ||
      manifest.occurrences[0].origin_kind != "explicit_copy" ||
      manifest.occurrences[1].origin_kind != "explicit_copy" ||
      manifest.occurrences[2].origin_kind != "unclassified" ||
      manifest.source_copy_occurrences[0].rust_clone_spans.size() != 2)
    return 3;
  write_native_clone_provenance_v3(std::cout, rust, source, manifest);
}
