#include "native_clone_provenance.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
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
  first.generated_callable_symbols = {"first", "__moss_view_first"};
  typed.specialization_identity = "spec:second";
  typed.specialization_context = typed.est_fact_context = "fn:second";
  auto second = native_checked_copy_fact_from_typed(typed, source);
  second.generated_callable_symbols = {"second", "__moss_view_second"};
  const auto first_id = native_checked_copy_identity(first);
  auto rehomed = first;
  rehomed.source_file = "/alternate-checkout/source.moss";
  rehomed.provenance_source_file = rehomed.source_file;
  if (native_checked_copy_fact_json(rehomed) ==
      native_checked_copy_fact_json(first)) return 36;
  if (native_checked_copy_identity(rehomed) != first_id) return 37;
  const auto second_id = native_checked_copy_identity(second);
  const auto first_helper = "__moss_copy_value_" + first_id.substr(19, 16);
  const auto second_helper = "__moss_copy_value_" + second_id.substr(19, 16);
  const std::string rust =
      std::string("// Moss tooling begin|function|fn:first|first|\n") +
      "fn first() { fn " + first_helper +
      "(value: &i64) -> i64 { (*value).clone() }\n" +
      "/* Moss copy fact call: " + first_id + " */ " + first_helper + "(&v); }\n" +
      "// Moss tooling end|fn:first\n" +
      "// Moss tooling begin|function|fn:second|second|\n" +
      "fn second() { fn " + second_helper +
      "(value: &i64) -> i64 { (*value).clone() }\n" +
      "/* Moss copy fact call: " + second_id + " */ " + second_helper + "(&v); }\n" +
      "// Moss tooling end|fn:second\n" +
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

  // One checked fact may be emitted once in each distinct generated function
  // variant (for example, an owned body and its borrowed-view body). The
  // builder must authorize both exact helper/call pairs, while rejecting a
  // repeated fact marker in the same variant.
  const std::string variant_rust =
      std::string("// Moss tooling begin|function|fn:first|first|\n") +
      "fn first() { fn " + first_helper +
      "(value: &i64) -> i64 { (*value).clone() }\n" +
      "/* Moss copy fact call: " + first_id + " */ " + first_helper + "(&v); }\n" +
      "// Moss tooling end|fn:first\n" +
      "// Moss tooling begin|function|fn:first|__moss_view_first|\n" +
      "fn __moss_view_first() { fn " + first_helper +
      "(value: &i64) -> i64 { (*value).clone() }\n" +
      "/* Moss copy fact call: " + first_id + " */ " + first_helper + "(&view); }\n" +
      "// Moss tooling end|fn:first\n";
  NativeCloneCheckedCopyFactV3 one_fact;
  one_fact.copy_id = source_row.copy_id;
  one_fact.facts = {first};
  const auto variant_manifest = build_native_clone_provenance_v3(
      "debug", variant_rust, source, {source_row}, {one_fact});
  if (variant_manifest.occurrences.size() != 2 ||
      variant_manifest.source_copy_occurrences[0].rust_clone_spans.size() != 2)
    return 28;
  std::ostringstream variant_json;
  write_native_clone_provenance_v3(
      variant_json, variant_rust, source, variant_manifest);
  const auto serialized_variants = variant_json.str();
  if (serialized_variants.find("rust-function-variant-v1:") == std::string::npos ||
      serialized_variants.find("__moss_view_first") == std::string::npos ||
      serialized_variants.find("generated_function_span") == std::string::npos)
    return 31;
  auto duplicate_variant = variant_rust;
  const auto second_call = duplicate_variant.find(
      "/* Moss copy fact call: " + first_id + " */", duplicate_variant.find("__moss_view_first"));
  if (second_call == std::string::npos) return 29;
  duplicate_variant.insert(second_call,
      "/* Moss copy fact call: " + first_id + " */ " + first_helper + "(&view);\n");
  bool duplicate_rejected = false;
  try {
    (void)build_native_clone_provenance_v3(
        "debug", duplicate_variant, source, {source_row}, {one_fact});
  } catch (const std::exception&) {
    duplicate_rejected = true;
  }
  if (!duplicate_rejected) return 30;
  const auto unrelated_variant = variant_rust +
      "// Moss tooling begin|function|fn:unrelated|unrelated|\n" +
      "fn unrelated() { fn " + first_helper +
      "(value: &i64) -> i64 { (*value).clone() }\n" +
      "/* Moss copy fact call: " + first_id + " */ " + first_helper + "(&v); }\n" +
      "// Moss tooling end|fn:unrelated\n";
  bool unrelated_rejected = false;
  try {
    (void)build_native_clone_provenance_v3(
        "debug", unrelated_variant, source, {source_row}, {one_fact});
  } catch (const std::exception&) {
    unrelated_rejected = true;
  }
  if (!unrelated_rejected) return 32;

  auto specialized_typed = typed;
  specialized_typed.specialization_identity = "specialization:checked-first-int";
  specialized_typed.specialization_context =
      specialized_typed.est_fact_context = "fn:first<int>";
  auto specialized_fact = native_checked_copy_fact_from_typed(
      specialized_typed, source);
  specialized_fact.generated_callable_symbols = {
      "__moss_specialize_first_7", "__moss_view___moss_specialize_first_7"};
  const auto specialized_id = native_checked_copy_identity(specialized_fact);
  const auto specialized_helper = "__moss_copy_value_" +
      specialized_id.substr(19, 16);
  NativeCloneCheckedCopyFactV3 specialized_group;
  specialized_group.copy_id = source_row.copy_id;
  specialized_group.facts = {specialized_fact};
  const std::string specialized_rust =
      std::string("// Moss tooling begin|function|fn:first<int>@2#spec|__moss_specialize_first_7|\n") +
      "fn __moss_specialize_first_7() { fn " + specialized_helper +
      "(value: &i64) -> i64 { (*value).clone() }\n" +
      "/* Moss copy fact call: " + specialized_id + " */ " +
      specialized_helper + "(&v); }\n" +
      "// Moss tooling end|fn:first<int>@2#spec\n";
  const auto specialized_manifest = build_native_clone_provenance_v3(
      "debug", specialized_rust, source, {source_row}, {specialized_group});
  if (specialized_manifest.occurrences.size() != 1) return 33;
  auto altered_specialization_name = specialized_rust;
  const auto altered_name_at = altered_specialization_name.find(
      "__moss_specialize_first_7");
  altered_specialization_name.replace(altered_name_at,
      std::string("__moss_specialize_first_7").size(),
      "__moss_specialize_first_999");
  bool altered_specialization_rejected = false;
  try {
    (void)build_native_clone_provenance_v3(
        "debug", altered_specialization_name, source,
        {source_row}, {specialized_group});
  } catch (const std::exception&) {
    altered_specialization_rejected = true;
  }
  if (!altered_specialization_rejected) return 34;

  auto method_typed = typed;
  method_typed.specialization_identity.clear();
  method_typed.specialization_context =
      method_typed.est_fact_context = "method:Record.check";
  auto method_fact = native_checked_copy_fact_from_typed(method_typed, source);
  method_fact.generated_callable_symbols = {
      "Record::check", "Record::__moss_view_check"};
  const auto method_id = native_checked_copy_identity(method_fact);
  const auto method_helper = "__moss_copy_value_" + method_id.substr(19, 16);
  NativeCloneCheckedCopyFactV3 method_group;
  method_group.copy_id = source_row.copy_id;
  method_group.facts = {method_fact};
  const std::string method_rust =
      std::string("// Moss tooling begin|method|method:Record.check@4|Record::check|\n") +
      "impl Record { fn check() { fn " + method_helper +
      "(value: &i64) -> i64 { (*value).clone() }\n" +
      "/* Moss copy fact call: " + method_id + " */ " +
      method_helper + "(&v); } }\n" +
      "// Moss tooling end|method:Record.check@4\n" +
      "// Moss tooling begin|method|method:Record.check@4|Record::__moss_view_check|\n" +
      "impl Record { fn __moss_view_check() { fn " + method_helper +
      "(value: &i64) -> i64 { (*value).clone() }\n" +
      "/* Moss copy fact call: " + method_id + " */ " +
      method_helper + "(&v); } }\n" +
      "// Moss tooling end|method:Record.check@4\n";
  const auto method_manifest = build_native_clone_provenance_v3(
      "debug", method_rust, source, {source_row}, {method_group});
  if (method_manifest.occurrences.size() != 2) return 35;

  // The builder must derive authorization from the exact fact/helper/call
  // relation. Each corrupted input below must fail before a manifest is
  // returned; a caller cannot make a self-asserted sidecar authoritative.
  const auto rejects = [&](const std::string& bad_rust,
                           const std::vector<NativeCloneSourceCopyV3>& rows,
                           const std::vector<NativeCloneCheckedCopyFactV3>& groups) {
    try {
      (void)build_native_clone_provenance_v3("debug", bad_rust, source, rows, groups);
    } catch (const std::exception&) {
      return true;
    }
    return false;
  };
  if (!rejects(rust, {source_row}, {})) return 4; // missing facts
  if (!rejects(rust, {}, {facts})) return 5; // missing lexical occurrence
  if (!rejects(rust, {source_row, source_row}, {facts, facts})) return 16;
  if (!rejects(rust, {source_row}, {facts, facts})) return 6; // duplicate group

  auto duplicate_fact = facts;
  duplicate_fact.facts.push_back(duplicate_fact.facts.front());
  if (!rejects(rust, {source_row}, {duplicate_fact})) return 7;

  auto missing_marker = rust;
  const auto marker = "/* Moss copy fact call: " + first_id + " */ ";
  const auto marker_at = missing_marker.find(marker);
  if (marker_at == std::string::npos) return 8;
  missing_marker.erase(marker_at, marker.size());
  if (!rejects(missing_marker, {source_row}, {facts})) return 9;

  auto forged_marker = rust;
  const auto id_at = forged_marker.find(first_id);
  if (id_at == std::string::npos) return 10;
  forged_marker.replace(id_at, first_id.size(),
      "typed-copy-fact-v1:" + std::string(64, '0'));
  if (!rejects(forged_marker, {source_row}, {facts})) return 11;

  auto duplicate_marker = rust;
  duplicate_marker.insert(marker_at, marker);
  if (!rejects(duplicate_marker, {source_row}, {facts})) return 12;

  auto extra_caller = rust;
  extra_caller += "\nfn extra() { " + first_helper + "(&v); }\n";
  if (!rejects(extra_caller, {source_row}, {facts})) return 13;

  auto corrupt_fact_group = facts;
  corrupt_fact_group.facts.front().required_source_access = "CONSUME";
  if (!rejects(rust, {source_row}, {corrupt_fact_group})) return 14;

  auto mismatched_source = facts;
  mismatched_source.facts.front().source_bytes += "\n";
  if (!rejects(rust, {source_row}, {mismatched_source})) return 15;

  auto corrupt_fact_type = facts;
  corrupt_fact_type.facts.front().result_type_key = "record:Forged";
  if (!rejects(rust, {source_row}, {corrupt_fact_type})) return 17;

  TypedRootIngressDomainCaptureFactV1 root_fact;
  root_fact.statement_occurrence_id = 42;
  root_fact.est_arena_index = 3;
  root_fact.message_expression_occurrence_id = 7;
  root_fact.receiver_expression_occurrence_id = 8;
  root_fact.source_fingerprint = 901;
  root_fact.origin_id = 902;
  root_fact.specialization_context = "main";
  root_fact.est_fact_context = "main";
  root_fact.receiver_binding = "w";
  root_fact.receiver_definition_identity = "domain-instance:main::w";
  root_fact.domain_instance_identity = "main::w";
  root_fact.domain_type_identity = "Worker";
  root_fact.concrete_graph_identity = "concrete-domain-graph|I:main::w=Worker";
  root_fact.route_capability_identity = "main::w";
  root_fact.selected_call_edge_source = "main";
  root_fact.selected_call_edge_target = "handler:Worker.Process";
  root_fact.selected_call_edge_invocation_kind = "synchronous_message";
  root_fact.selected_call_edge_receiver = "w";
  root_fact.selected_call_edge_source_file = source_path;
  root_fact.selected_call_edge_line = 2;
  root_fact.selected_call_edge_occurrence = 0;
  root_fact.selected_call_edge_est_arena_index = 3;
  root_fact.selected_call_edge_message_expression_occurrence_id = 7;
  root_fact.selected_call_edge_receiver_expression_occurrence_id = 8;
  root_fact.selected_call_edge_source_fingerprint = 901;
  root_fact.selected_call_edge_identity =
      "main|handler:Worker.Process|" + source_path +
      ":2:0|receiver=w|est=3:7:8";
  root_fact.selected_handler_identity = "handler:Worker.Process";
  root_fact.source_file = source_path;
  root_fact.source_line = 2;
  root_fact.synchronous_root_ingress = true;
  root_fact.compiler_generated_capture_only = true;
  root_fact.permits_moss_value_copy = false;
  root_fact.availability =
      TypedRootIngressDomainCaptureFactV1::Availability::Present;
  if (!root_fact.is_authoritative()) return 18;
  const auto root_id = native_root_ingress_capture_identity(root_fact);
  const std::string root_rust =
      "fn main(){ let __d = w/* Moss root ingress capture fact: " +
      root_id + " */.clone(); }\n";
  NativeRootIngressCaptureInputV1 root_input{root_fact, source};
  auto root_manifest = build_native_clone_provenance_v3(
      "debug", root_rust, source, {}, {}, {root_input});
  if (root_manifest.root_ingress_domain_captures.size() != 1 ||
      root_manifest.occurrences.size() != 1 ||
      root_manifest.occurrences.front().origin_kind !=
          "root_ingress_domain_capture" ||
      root_manifest.occurrences.front().semantic_identity != root_id ||
      root_manifest.occurrences.front().ownership_fact.find(
          "permits_moss_value_copy=false") == std::string::npos)
    return 19;
  std::ostringstream root_json;
  write_native_clone_provenance_v3(root_json, root_rust, source, root_manifest);
  if (root_json.str().find("\"root_ingress_domain_captures\":[{\"capture_id\":") ==
          std::string::npos ||
      root_json.str().find("\"permits_moss_value_copy\":false") ==
          std::string::npos)
    return 20;

  const auto root_rejects = [&](const std::string& bad_rust,
                                const std::vector<NativeRootIngressCaptureInputV1>& inputs) {
    try {
      (void)build_native_clone_provenance_v3(
          "debug", bad_rust, source, {}, {}, inputs);
    } catch (const std::exception&) {
      return true;
    }
    return false;
  };
  auto missing_root_marker = root_rust;
  auto root_marker_at = missing_root_marker.find(
      "/* Moss root ingress capture fact: ");
  const auto root_marker_end = missing_root_marker.find(" */", root_marker_at) + 3;
  missing_root_marker.erase(root_marker_at, root_marker_end - root_marker_at);
  if (!root_rejects(missing_root_marker, {root_input})) return 21;
  auto forged_root_marker = root_rust;
  const auto root_id_at = forged_root_marker.find(root_id);
  forged_root_marker.replace(root_id_at, root_id.size(),
      "root-ingress-domain-capture-fact-v1:" + std::string(64, '0'));
  if (!root_rejects(forged_root_marker, {root_input})) return 22;
  auto duplicate_root_marker = root_rust;
  duplicate_root_marker.insert(root_marker_at,
      "/* Moss root ingress capture fact: " + root_id + " */");
  if (!root_rejects(duplicate_root_marker, {root_input})) return 23;
  auto wrong_root_receiver = root_rust;
  const auto receiver_at = wrong_root_receiver.find("w/* Moss root");
  wrong_root_receiver.replace(receiver_at, 1, "x");
  if (!root_rejects(wrong_root_receiver, {root_input})) return 24;
  if (!root_rejects(root_rust, {root_input, root_input})) return 25;
  auto copied_fact = root_fact;
  copied_fact.permits_moss_value_copy = true;
  if (!root_rejects(root_rust, {{copied_fact, source}})) return 26;
  if (!root_rejects(root_rust, {{root_fact, source + "\n"}})) return 27;

  write_native_clone_provenance_v3(std::cout, rust, source, manifest);
}
