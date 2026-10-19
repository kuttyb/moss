#include "abi_v10_records.hpp"
#include "moss_runtime_linkage.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <utility>

using namespace moss::abi_v10;

template <typename Action>
void expect_error(Action&& action, const std::string& code) {
  try {
    std::forward<Action>(action)();
  } catch (const Error& error) {
    if (error.code != code) {
      std::cerr << "Expected error code '" << code << "', got '" << error.code << "': " << error.what() << "\n";
    }
    assert(error.code == code);
    return;
  }
  assert(false && "malformed ABI-v10 record was accepted");
}

int main() {
  const auto digest = "sha256:" + std::string(64, 'a');
  const auto body_digest = "body-semantic-v1:" + digest;
  const WireType variable{WireTypeKind::BoundVar, "", {}, 0};
  Summary valid;
  valid.callable_id = "module-v1:provider@1.0.0::Provider::outer";
  valid.variable_count = 1;
  valid.parameters = {variable};
  valid.result = variable;
  valid.parameter_access = {Access::Write};
  valid.requirements = {
      {variable, "relay", Access::Write, 0, "provider:outer:1"},
      {variable, "required", Access::Read, 1, "provider:inner:2"},
  };
  valid.dependencies = {{"helper:inner", body_digest}};
  valid.observable_effect_bits = 1;
  valid.observable_effect_tags = {"raise:ProviderError"};
  valid.body_semantic_hash = body_digest;
  valid.provenance_id = "provider:outer:1";

  const auto record = summary_record(valid);
  validate_summary(record);
  const auto roundtrip = read_summary(record.payload);
  assert(roundtrip.parameter_access[0] == Access::Write);
  assert(roundtrip.requirements[1].parent == 1);
  assert(roundtrip.requirements[1].provenance_id == "provider:inner:2");
  assert(roundtrip.observable_effect_tags == valid.observable_effect_tags);

  validate_summary_nonweakening(valid, roundtrip);
  auto weakened = valid;
  weakened.parameter_access[0] = Access::Read;
  expect_error([&] { validate_summary_nonweakening(valid, weakened); },
               "SOURCE_FREE_SUMMARY_INVALID");
  weakened = valid;
  weakened.requirements.pop_back();
  expect_error([&] { validate_summary_nonweakening(valid, weakened); },
               "SOURCE_FREE_SUMMARY_INVALID");
  weakened = valid;
  weakened.dependencies.clear();
  expect_error([&] { validate_summary_nonweakening(valid, weakened); },
               "SOURCE_FREE_SUMMARY_INVALID");
  weakened = valid;
  weakened.observable_effect_bits = 0;
  expect_error([&] { validate_summary_nonweakening(valid, weakened); },
               "SOURCE_FREE_SUMMARY_INVALID");
  weakened = valid;
  weakened.observable_effect_tags.clear();
  expect_error([&] { validate_summary_nonweakening(valid, weakened); },
               "SOURCE_FREE_SUMMARY_INVALID");

  auto malformed = valid;
  malformed.requirements[0].parent = 1;
  expect_error([&] { (void)summary_payload(malformed); },
               "SOURCE_FREE_SUMMARY_INVALID");
  malformed = valid;
  malformed.requirements[1].parent = 2;
  expect_error([&] { (void)summary_payload(malformed); },
               "SOURCE_FREE_SUMMARY_INVALID");

  malformed = valid;
  malformed.parameter_access.clear();
  expect_error([&] { (void)summary_payload(malformed); },
               "SOURCE_FREE_SUMMARY_INVALID");
  malformed = valid;
  malformed.parameter_access[0] = static_cast<Access>(0);
  expect_error([&] { (void)summary_payload(malformed); },
               "SOURCE_FREE_SUMMARY_INVALID");
  malformed = valid;
  malformed.requirements[1].access = static_cast<Access>(4);
  expect_error([&] { (void)summary_payload(malformed); },
               "SOURCE_FREE_SUMMARY_INVALID");

  malformed = valid;
  malformed.requirements[1].provenance_id.clear();
  expect_error([&] { (void)summary_payload(malformed); },
               "SOURCE_FREE_SUMMARY_INVALID");
  malformed = valid;
  malformed.provenance_id.clear();
  expect_error([&] { (void)summary_payload(malformed); },
               "SOURCE_FREE_SUMMARY_INVALID");

  malformed = valid;
  malformed.dependencies[0].second = "sha256:bad";
  expect_error([&] { (void)summary_payload(malformed); },
               "SOURCE_FREE_SUMMARY_INVALID");
  malformed = valid;
  malformed.dependencies.push_back(malformed.dependencies[0]);
  expect_error([&] { (void)summary_payload(malformed); },
               "SOURCE_FREE_SUMMARY_INVALID");

  auto forged = record;
  forged.id = "sha256:" + std::string(64, 'b');
  expect_error([&] { validate_summary(forged); },
               "SOURCE_FREE_SUMMARY_INVALID");
  expect_error([&] { (void)read_summary(record.payload + "trailing"); },
               "SOURCE_FREE_SUMMARY_INVALID");

  KeyProof proof;
  proof.key_type = {WireTypeKind::Primitive, "int", {}, 0};
  proof.equality_callable_id = "builtin::int_isequal";
  proof.equality_specialization_key = digest;
  proof.equality_semantic_hash = digest;
  proof.hash_callable_id = "builtin::int_hash";
  proof.hash_specialization_key = digest;
  proof.hash_semantic_hash = digest;
  proof.law_contract_version = "key-laws-v1";
  proof.provenance_id = "builtin:int";
  validate_key_proof(key_proof_record(proof));
  auto impure = proof;
  impure.equality_effect_bits = 1;
  expect_error([&] { (void)key_proof_record(impure); },
               "SOURCE_FREE_CONTRACT_INVALID");
  impure = proof;
  impure.hash_effect_bits = 1;
  expect_error([&] { (void)key_proof_record(impure); },
               "SOURCE_FREE_CONTRACT_INVALID");
  impure = proof;
  impure.equality_effect_tags = {"raise:KeyError"};
  expect_error([&] { (void)key_proof_record(impure); },
               "SOURCE_FREE_CONTRACT_INVALID");
  impure = proof;
  impure.hash_effect_tags = {"panic"};
  expect_error([&] { (void)key_proof_record(impure); },
               "SOURCE_FREE_CONTRACT_INVALID");

  const auto base_proof = key_proof_record(proof);
  auto conflicting = proof;
  conflicting.hash_semantic_hash = "sha256:" + std::string(64, 'b');
  expect_error([&] {
    validate_key_proof_coherence({base_proof, key_proof_record(conflicting)});
  }, "KEY_PROOF_COHERENCE_MISMATCH");

  proof.key_type = {WireTypeKind::Nominal,
                    "nominal-template-v1:module-v1:provider@1.0.0::Provider:type:Code",
                    {}, 0};
  auto distinct_nominal = proof;
  distinct_nominal.key_type.identity =
      "nominal-template-v1:module-v1:provider@1.0.0::Provider:type:Other";
  validate_key_proof_coherence({key_proof_record(proof),
                                key_proof_record(distinct_nominal)});

  // Adversarial tests for SourceProvenance
  SourceProvenance prov;
  prov.provenance_id = "provenance:provider:1";
  prov.module_id = "module-v1:provider@1.0.0::Provider";
  prov.logical_source_id = digest;
  prov.source_file_display = "src/provider.moss";
  prov.start_line = 5;
  prov.start_column = 1;
  prov.end_line = 10;
  prov.end_column = 2;
  prov.declaration_id = "item";
  validate_provenance(provenance_record(prov));

  auto malformed_prov = prov;
  malformed_prov.provenance_id.clear();
  expect_error([&] { (void)provenance_payload(malformed_prov); },
               "SOURCE_FREE_CONTRACT_INVALID");
  malformed_prov = prov;
  malformed_prov.module_id = "invalid-module";
  expect_error([&] { (void)provenance_payload(malformed_prov); },
               "SOURCE_FREE_CONTRACT_INVALID");
  malformed_prov = prov;
  malformed_prov.start_line = 15;
  malformed_prov.end_line = 10;
  expect_error([&] { (void)provenance_payload(malformed_prov); },
               "SOURCE_FREE_CONTRACT_INVALID");

  // Adversarial tests for MethodTemplate
  MethodTemplate method;
  method.method_id = "method:Provider.func";
  method.template_id =
      "nominal-template-v1:module-v1:provider@1.0.0::Provider:type:Code";
  method.method_name = "func";
  method.exported = true;
  method.variable_count = 1;
  method.parameters = {variable};
  method.parameter_access = {Access::Read};
  method.result = variable;
  method.receiver_access = Access::Read;
  method.provenance_id = prov.provenance_id;
  validate_method_template(method_template_record(method));

  auto malformed_method = method;
  malformed_method.method_id.clear();
  expect_error([&] { (void)method_template_payload(malformed_method); },
               "SOURCE_FREE_CONTRACT_INVALID");
  malformed_method = method;
  malformed_method.template_id = "not-a-hash";
  expect_error([&] { (void)method_template_payload(malformed_method); },
               "SOURCE_FREE_CONTRACT_INVALID");
  malformed_method = method;
  malformed_method.parameter_access.clear();
  expect_error([&] { (void)method_template_payload(malformed_method); },
               "SOURCE_FREE_CONTRACT_INVALID");

  // Adversarial tests for ModuleRecord
  ModuleRecord mod;
  mod.module_id = "module-v1:provider@1.0.0::Provider";
  mod.package_name = "provider";
  mod.package_version = "1.0.0";
  mod.module_name = "Provider";
  mod.provider_binary_id = digest;
  validate_module_record(module_record(mod));

  auto malformed_mod = mod;
  malformed_mod.module_id = "bad-mod";
  expect_error([&] { (void)module_record_payload(malformed_mod); },
               "SOURCE_FREE_CONTRACT_INVALID");
  malformed_mod = mod;
  malformed_mod.provider_binary_id = "bad-hash";
  expect_error([&] { (void)module_record_payload(malformed_mod); },
               "SOURCE_FREE_CONTRACT_INVALID");

  // Whole-interface validation with missing private dependency
  Interface iface;
  iface.module_id = "module-v1:provider@1.0.0::Provider";
  iface.compiler = "moss-test";
  iface.backend_fingerprint = "rustc-test";
  iface.backend_rustc = "rustc 1.0";
  iface.provenance.push_back(provenance_record(prov));

  Summary summary_with_dep = valid;
  summary_with_dep.provenance_id = prov.provenance_id;
  summary_with_dep.requirements[0].provenance_id = prov.provenance_id;
  summary_with_dep.requirements[1].provenance_id = prov.provenance_id;
  summary_with_dep.dependencies = {{"missing:private_helper", body_digest}};
  iface.semantic_contract.push_back(summary_record(summary_with_dep));

  const auto sealed_bad_dep = decode(encode(iface));
  expect_error([&] { validate_complete_interface(sealed_bad_dep); },
               "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING");

  // Test dangling provenance when provenance section is empty
  Interface iface_bad_prov = iface;
  Summary valid_summary = valid;
  valid_summary.dependencies.clear();
  valid_summary.provenance_id = prov.provenance_id;
  valid_summary.requirements[0].provenance_id = prov.provenance_id;
  valid_summary.requirements[1].provenance_id = prov.provenance_id;
  iface_bad_prov.semantic_contract = {summary_record(valid_summary)};
  iface_bad_prov.provenance.clear();
  const auto sealed_bad_prov = decode(encode(iface_bad_prov));
  expect_error([&] { validate_complete_interface(sealed_bad_prov); },
               "SOURCE_FREE_CONTRACT_INVALID");

  SourceProvenance dummy_prov = prov;
  dummy_prov.provenance_id = "provenance:other:2";
  iface_bad_prov.provenance = {provenance_record(dummy_prov)};
  const auto sealed_bad_prov2 = decode(encode(iface_bad_prov));
  expect_error([&] { validate_complete_interface(sealed_bad_prov2); },
               "SOURCE_FREE_CONTRACT_INVALID");

  // A sealed container must bind an expected callable dependency hash to
  // the actual private summary, not merely prove that its ID exists.
  Summary private_helper = valid_summary;
  private_helper.callable_id = "helper:inner";
  private_helper.body_semantic_hash = body_digest;
  Summary public_with_helper = valid_summary;
  public_with_helper.dependencies = {
      {private_helper.callable_id, private_helper.body_semantic_hash}};
  Interface iface_with_helper = iface;
  iface_with_helper.semantic_contract = {summary_record(public_with_helper)};
  iface_with_helper.private_semantic_dependencies = {
      summary_record(private_helper)};
  validate_complete_interface(decode(encode(iface_with_helper)));

  Summary mismatched_helper = private_helper;
  mismatched_helper.body_semantic_hash =
      "body-semantic-v1:sha256:" + std::string(64, 'b');
  Interface iface_bad_hash = iface_with_helper;
  iface_bad_hash.private_semantic_dependencies = {
      summary_record(mismatched_helper)};
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_bad_hash)));
  }, "SOURCE_FREE_CONTRACT_INVALID");

  Interface iface_unresolved_import = iface_with_helper;
  iface_unresolved_import.imports.push_back({
      "module-v1:other@1.0.0::Other", digest, digest});
  Summary external_dependency = public_with_helper;
  external_dependency.dependencies = {{
      "callable-v1:module-v1:other@1.0.0::Other:fn:helper", body_digest}};
  iface_unresolved_import.semantic_contract = {
      summary_record(external_dependency)};
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_unresolved_import)));
  }, "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING");

  Summary foreign_hash_kind = public_with_helper;
  foreign_hash_kind.dependencies[0].second = digest;
  expect_error([&] { (void)summary_record(foreign_hash_kind); },
               "SOURCE_FREE_SUMMARY_INVALID");
  foreign_hash_kind.dependencies[0].second =
      "nominal-instance-v1:" + digest;
  expect_error([&] { (void)summary_record(foreign_hash_kind); },
               "SOURCE_FREE_SUMMARY_INVALID");
  foreign_hash_kind = public_with_helper;
  foreign_hash_kind.body_semantic_hash = digest;
  expect_error([&] { (void)summary_record(foreign_hash_kind); },
               "SOURCE_FREE_SUMMARY_INVALID");

  // A syntactically canonical nominal ID cannot invent a local declaration.
  Summary forged_nominal_summary = valid_summary;
  forged_nominal_summary.result = {
      WireTypeKind::Nominal,
      "nominal-template-v1:module-v1:provider@1.0.0::Provider:type:Missing",
      {variable}, 0};
  Interface iface_bad_nominal = iface;
  iface_bad_nominal.semantic_contract = {summary_record(forged_nominal_summary)};
  const auto sealed_bad_nominal = decode(encode(iface_bad_nominal));
  expect_error([&] { validate_complete_interface(sealed_bad_nominal); },
               "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");

  // Duplicate records within a section
  Interface iface_dup = iface;
  iface_dup.semantic_contract = {summary_record(summary_with_dep), summary_record(summary_with_dep)};
  expect_error([&] { (void)encode(iface_dup); }, "SOURCE_FREE_CONTRACT_INVALID");

  // Out of order records within a section
  Record rec_b{"sha256:" + std::string(64, 'b'), "payload"};
  Record rec_a{"sha256:" + std::string(64, 'a'), "payload"};
  Interface iface_ooo = iface;
  iface_ooo.semantic_contract = {rec_b, rec_a};
  expect_error([&] { (void)encode(iface_ooo); }, "SOURCE_FREE_CONTRACT_INVALID");

  // Bound variable escaping scope
  WireType esc_var{WireTypeKind::BoundVar, "", {}, 5};
  expect_error([&] { (void)type_bytes(esc_var, 2); }, "SOURCE_FREE_SUMMARY_INVALID");

  // Bound variable in closed ConcreteInstance
  ConcreteInstance ci;
  ci.template_id =
      "nominal-template-v1:module-v1:provider@1.0.0::Provider:type:Code";
  ci.substitution = {esc_var};
  expect_error([&] { (void)instance_key(ci); }, "SOURCE_FREE_SUMMARY_INVALID");

  // Truncated wire payload
  const auto full_pl = summary_record(summary_with_dep).payload;
  expect_error([&] { (void)read_summary(full_pl.substr(0, 10)); },
               "SOURCE_FREE_CONTRACT_INVALID");

  // Trailing data on record payload
  expect_error([&] { (void)read_summary(full_pl + "trailing"); },
               "SOURCE_FREE_SUMMARY_INVALID");

  // MethodTemplate referencing unknown template ID
  MethodTemplate mt;
  mt.method_id = "method:unattached";
  mt.template_id =
      "nominal-template-v1:module-v1:provider@1.0.0::Provider:type:Unknown";
  mt.method_name = "orphan";
  mt.provenance_id = prov.provenance_id;
  Interface iface_bad_mt = iface;
  iface_bad_mt.semantic_contract = {method_template_record(mt)};
  const auto sealed_bad_mt = decode(encode(iface_bad_mt));
  expect_error([&] { validate_complete_interface(sealed_bad_mt); },
               "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");

  // Corrupted rlib archive and mismatched marker
  auto archive_with_marker = [](const std::string& marker, bool metadata = false) {
    auto member = marker;
    std::string header(60, ' ');
    const auto name = metadata ? "provider.rmeta/" : "provider.o/";
    header.replace(0, std::char_traits<char>::length(name), name);
    header.replace(48, std::to_string(member.size()).size(),
                   std::to_string(member.size()));
    header.replace(58, 2, "`\n");
    return "!<arch>\n" + header + member + (member.size() & 1 ? "\n" : "");
  };

  expect_error([&] { validate_complete_interface(sealed_bad_dep, "", "not an archive"); },
               "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");

  const std::string wrong_marker_rlib =
      archive_with_marker("MOSS_ABI_V10_CONTRACT::Provider::" + std::string(64, '0') + "::END");
  expect_error([&] { validate_complete_interface(sealed_bad_dep, "", wrong_marker_rlib); },
               "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");

  // ABI-v9 container rejection
  expect_error([&] { (void)parse("moss-module-interface-v2\nmodule_id test\n"); },
               "SOURCE_FREE_ABI_REBUILD_REQUIRED");

  // Runtime identity mismatch checks
  RuntimeCrateIdentity prov_rt, cons_rt;
  prov_rt.name = "moss_runtime";
  prov_rt.version = "0.1.0";
  prov_rt.artifact_hash = "sha256:" + std::string(64, '1');
  prov_rt.build_key = "sha256:" + std::string(64, 'a');

  cons_rt = prov_rt;
  cons_rt.name = "diff_runtime";
  expect_error([&] { verify_runtime_compatibility(prov_rt, cons_rt); },
               "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");

  cons_rt = prov_rt;
  cons_rt.version = "0.2.0";
  expect_error([&] { verify_runtime_compatibility(prov_rt, cons_rt); },
               "SOURCE_FREE_ABI_REBUILD_REQUIRED");

  cons_rt = prov_rt;
  cons_rt.artifact_hash = "sha256:" + std::string(64, '2');
  expect_error([&] { verify_runtime_compatibility(prov_rt, cons_rt); },
               "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");

  cons_rt = prov_rt;
  cons_rt.build_key = "sha256:" + std::string(64, 'b');
  expect_error([&] { verify_runtime_compatibility(prov_rt, cons_rt); },
               "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");

  // Content-addressed runtime caching tests
  const std::filesystem::path runtime_src_dir = "runtime/moss_runtime";
  const std::filesystem::path test_cache_dir = "tmp/phase25-test-runtime-cache";
  std::filesystem::remove_all(test_cache_dir);

  // 1. Initial build (debug)
  const auto [rlib_debug, id_debug] =
      build_or_resolve_moss_runtime(runtime_src_dir, test_cache_dir, "debug");
  assert(std::filesystem::exists(rlib_debug));
  assert(!id_debug.build_key.empty());
  assert(!id_debug.artifact_hash.empty());
  assert(!id_debug.source_hash.empty());

  // 2. Cache hit: identical inputs produce identical build_key and reuse artifact
  const auto [rlib_debug_hit, id_debug_hit] =
      build_or_resolve_moss_runtime(runtime_src_dir, test_cache_dir, "debug");
  assert(rlib_debug == rlib_debug_hit);
  assert(id_debug.build_key == id_debug_hit.build_key);
  assert(id_debug.artifact_hash == id_debug_hit.artifact_hash);

  // 3. Timestamp touch without content change: must produce identical source hash and cache hit
  const auto lib_rs_path = runtime_src_dir / "src" / "lib.rs";
  const auto old_time = std::filesystem::last_write_time(lib_rs_path);
  std::filesystem::last_write_time(lib_rs_path, old_time + std::chrono::hours(1));
  const auto [rlib_touch, id_touch] =
      build_or_resolve_moss_runtime(runtime_src_dir, test_cache_dir, "debug");
  assert(id_debug.source_hash == id_touch.source_hash);
  assert(id_debug.build_key == id_touch.build_key);
  assert(id_debug.artifact_hash == id_touch.artifact_hash);

  // 4. Corrupted cached artifact: overwrite rlib with junk, verify detection and safe rebuild
  {
    std::ofstream junk(rlib_debug, std::ios::binary | std::ios::trunc);
    junk << "CORRUPTED_RLIB_JUNK";
  }
  const auto [rlib_restored, id_restored] =
      build_or_resolve_moss_runtime(runtime_src_dir, test_cache_dir, "debug");
  assert(id_restored.build_key == id_debug.build_key);
  assert(id_restored.artifact_hash == id_debug.artifact_hash);
  assert(compute_file_sha256(rlib_restored) == id_debug.artifact_hash);

  // 5. Profile separation: release build uses distinct build_key and directory
  const auto [rlib_release, id_release] =
      build_or_resolve_moss_runtime(runtime_src_dir, test_cache_dir, "release");
  assert(std::filesystem::exists(rlib_release));
  assert(rlib_release != rlib_debug);
  assert(id_release.profile == "release");
  assert(id_release.build_key != id_debug.build_key);
  // Re-verify compatibility check rejects debug vs release
  expect_error([&] { verify_runtime_compatibility(id_debug, id_release); },
               "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");

  std::filesystem::remove_all(test_cache_dir);

  std::cout << "ABI-v10 staged summary adversarial tests passed\n";
}
