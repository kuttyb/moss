#include "abi_v10_records.hpp"

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
    assert(error.code == code);
    return;
  }
  assert(false && "malformed ABI-v10 record was accepted");
}

int main() {
  const auto digest = "sha256:" + std::string(64, 'a');
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
  valid.dependencies = {{"helper:inner", digest}};
  valid.observable_effect_bits = 1;
  valid.observable_effect_tags = {"raise:ProviderError"};
  valid.body_semantic_hash = digest;
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

  proof.key_type = {WireTypeKind::Nominal, "sha256:" + std::string(64, 'c'), {}, 0};
  auto distinct_nominal = proof;
  distinct_nominal.key_type.identity = "sha256:" + std::string(64, 'd');
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
  method.template_id = digest;
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
  summary_with_dep.dependencies = {{"missing:private_helper", digest}};
  iface.semantic_contract.push_back(summary_record(summary_with_dep));

  const auto sealed_bad_dep = decode(encode(iface));
  expect_error([&] { validate_complete_interface(sealed_bad_dep); },
               "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING");

  // Whole-interface validation with missing provenance reference
  Interface iface_bad_prov = iface;
  iface_bad_prov.provenance.clear(); // remove provenance record
  summary_with_dep.dependencies.clear();
  iface_bad_prov.semantic_contract = {summary_record(summary_with_dep)};
  const auto sealed_bad_prov = decode(encode(iface_bad_prov));
  // Without provenance record, summary has dangling provenance ID
  // When provenance is non-empty, dangling check triggers
  SourceProvenance dummy_prov = prov;
  dummy_prov.provenance_id = "provenance:other:2";
  iface_bad_prov.provenance.push_back(provenance_record(dummy_prov));
  const auto sealed_bad_prov2 = decode(encode(iface_bad_prov));
  expect_error([&] { validate_complete_interface(sealed_bad_prov2); },
               "SOURCE_FREE_CONTRACT_INVALID");

  std::cout << "ABI-v10 staged summary adversarial tests passed\n";
}
