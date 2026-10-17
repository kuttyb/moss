#include "../../src/abi_v10_records.hpp"
#include "../../src/moss_runtime_linkage.hpp"

#include <cassert>
#include <iostream>
#include <string>

using moss::abi_v10::Error;
using moss::abi_v10::Interface;

static void expect_error(const std::string& input, const std::string& code) {
  try {
    (void)moss::abi_v10::decode(input);
  } catch (const Error& error) {
    assert(error.code == code);
    return;
  }
  assert(false && "expected ABI-v10 diagnostic");
}

static void replace_once(std::string& text, const std::string& old_text,
                         const std::string& new_text) {
  const auto at = text.find(old_text);
  assert(at != std::string::npos);
  text.replace(at, old_text.size(), new_text);
}

static std::string archive_with_marker(const std::string& marker,
                                       bool metadata = false) {
  auto member = marker;
  std::string header(60, ' ');
  const auto name = metadata ? "provider.rmeta/" : "provider.o/";
  header.replace(0, std::char_traits<char>::length(name), name);
  header.replace(48, std::to_string(member.size()).size(),
                 std::to_string(member.size()));
  header.replace(58, 2, "`\n");
  return "!<arch>\n" + header + member + (member.size() & 1 ? "\n" : "");
}

int main() {
  Interface value;
  value.module_id = "module-v1:boxes@1.0.0::Boxes";
  value.compiler = "moss-test";
  value.backend_fingerprint = "rustc-test";
  value.backend_rustc = "rustc 1.0";
  value.imports.push_back({"module-v1:base@1.0.0::Base",
                           "sha256:" + std::string(64, 'a'),
                           "sha256:" + std::string(64, 'b')});
  value.binary_contract.push_back({"nominal:Box", "provider-record\nfield 0\n"});
  value.semantic_contract.push_back({"summary:fresh", "bound-var 0\n"});
  value.private_semantic_dependencies.push_back({"helper:private", "body-ir\n"});
  value.provenance.push_back({"source:boxes", "boxes.moss:1:1"});

  const auto sealed = moss::abi_v10::encode(value);
  const auto decoded = moss::abi_v10::decode(sealed);
  assert(decoded.value.module_id == value.module_id);
  assert(decoded.value.semantic_contract[0].payload == "bound-var 0\n");
  assert(decoded.binary_hash != decoded.semantic_hash);
  assert(moss::abi_v10::encode(decoded.value) == sealed);

  auto changed = sealed;
  replace_once(changed, "bound-var 0", "bound-var 1");
  expect_error(changed, "SOURCE_FREE_CONTRACT_INVALID");
  changed = sealed;
  replace_once(changed, "native_abi 10", "native_abi 9");
  expect_error(changed, "SOURCE_FREE_ABI_REBUILD_REQUIRED");
  changed = sealed;
  replace_once(changed, "schema_version 10", "schema_version 9");
  expect_error(changed, "SOURCE_FREE_ABI_REBUILD_REQUIRED");
  changed = sealed;
  replace_once(changed, "moss-module-interface-v3", "moss-module-interface-v2");
  expect_error(changed, "SOURCE_FREE_ABI_REBUILD_REQUIRED");
  changed = sealed + "trailing";
  expect_error(changed, "SOURCE_FREE_CONTRACT_INVALID");

  value.binary_contract.push_back(value.binary_contract.front());
  try {
    (void)moss::abi_v10::encode(value);
    assert(false && "duplicate record ID accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_CONTRACT_INVALID");
  }
  value.binary_contract.pop_back();
  value.semantic_contract[0].payload = std::string("\xc0\xaf", 2);
  try {
    (void)moss::abi_v10::encode(value);
    assert(false && "invalid UTF-8 accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_CONTRACT_INVALID");
  }

  const auto marker = moss::abi_v10::provider_marker("Boxes", decoded.digest);
  moss::abi_v10::validate_selected_rlib(archive_with_marker(marker), "Boxes",
                                         decoded.digest);
  try {
    moss::abi_v10::validate_selected_rlib(archive_with_marker(marker, true),
                                           "Boxes", decoded.digest);
    assert(false && "metadata-only seal accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");
  }
  try {
    moss::abi_v10::validate_selected_rlib(
        archive_with_marker(marker + marker), "Boxes", decoded.digest);
    assert(false && "duplicate seal accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");
  }

  using namespace moss::abi_v10;
  const WireType int_type{WireTypeKind::Primitive, "int", {}, 0};
  const WireType var_type{WireTypeKind::BoundVar, "", {}, 0};
  const WireType box_of_var{WireTypeKind::Nominal,
                            "sha256:" + std::string(64, 'c'), {var_type}, 0};
  const auto nested = type_bytes(
      {WireTypeKind::Builtin, "Vector", {box_of_var}, 0}, 1);
  WireReader type_reader(nested);
  const auto parsed_type = read_type(type_reader, 1);
  assert(type_reader.done() && parsed_type.arguments[0].arguments[0].ordinal == 0);
  try {
    (void)type_bytes(var_type, 0);
    assert(false && "unbound wire type variable accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_SUMMARY_INVALID");
  }

  NominalTemplate nominal;
  nominal.template_id = "sha256:" + std::string(64, 'c');
  nominal.module_id = value.module_id;
  nominal.declaration_name = "Box";
  nominal.variable_count = 1;
  nominal.fields.push_back({"value", var_type, true, "boxes:field:1"});
  nominal.representation_hash = "sha256:" + std::string(64, 'd');
  nominal.provenance_id = "boxes:1";
  const auto nominal_record = template_record(nominal);
  validate_template(nominal_record);
  assert(read_template(nominal_record.payload).fields[0].name == "value");
  nominal.fields.push_back(nominal.fields[0]);
  try {
    (void)template_payload(nominal);
    assert(false && "duplicate field accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
  }
  nominal.fields.pop_back();
  ConcreteInstance instance;
  instance.template_id = nominal.template_id;
  instance.substitution.push_back(int_type);
  instance.resolved_fields.push_back(int_type);
  instance.provider_binary_id = "sha256:" + std::string(64, '6');
  instance.provider_rust_type_symbol = "provider::Box<int>";
  instance.representation_hash = nominal.representation_hash;
  const auto instance_wire = instance_record(instance);
  validate_instance(instance_wire, nominal, instance.provider_binary_id);
  auto wrong_instance = instance;
  wrong_instance.resolved_fields[0] = {WireTypeKind::Primitive, "string", {}, 0};
  try {
    validate_instance(instance_record(wrong_instance), nominal,
                      instance.provider_binary_id);
    assert(false && "forged concrete field substitution accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
  }
  try {
    validate_instance(instance_wire, nominal,
                      "sha256:" + std::string(64, '7'));
    assert(false && "wrong provider binary accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");
  }

  Summary summary;
  summary.callable_id = "module-v1:boxes@1.0.0::Boxes::fresh";
  summary.variable_count = 1;
  summary.result = box_of_var;
  summary.body_semantic_hash = "sha256:" + std::string(64, 'e');
  summary.provenance_id = "boxes:fn:2";
  summary.requirements.push_back({var_type, "to_string", Access::Read, 0,
                                   "boxes:fn:3"});
  const auto open_record = summary_record(summary);
  validate_summary(open_record);
  assert(read_summary(open_record.payload).variable_count == 1);
  auto forged_summary = open_record;
  forged_summary.id = "sha256:" + std::string(64, '0');
  try {
    validate_summary(forged_summary);
    assert(false && "forged summary ID accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_SUMMARY_INVALID");
  }
  summary.requirements[0].parent = 1;
  try {
    (void)summary_payload(summary);
    assert(false && "self-parent requirement accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_SUMMARY_INVALID");
  }
  summary.requirements[0].parent = 0;
  summary.observable_effect_bits = 0b101;
  summary.observable_effect_tags = {"panic:division", "raise:ParseError"};
  auto weakened_summary = summary;
  weakened_summary.observable_effect_bits = 0b001;
  try {
    validate_summary_nonweakening(summary, weakened_summary);
    assert(false && "weakened imported effects accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_SUMMARY_INVALID");
  }
  weakened_summary = summary;
  weakened_summary.requirements.clear();
  try {
    validate_summary_nonweakening(summary, weakened_summary);
    assert(false && "erased imported requirement accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_SUMMARY_INVALID");
  }
  weakened_summary = summary;
  weakened_summary.observable_effect_tags.pop_back();
  try {
    validate_summary_nonweakening(summary, weakened_summary);
    assert(false && "erased imported raise effect accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_SUMMARY_INVALID");
  }
  auto strengthened_summary = summary;
  strengthened_summary.observable_effect_bits |= 0b010;
  strengthened_summary.observable_effect_tags.push_back("warning:recovery");
  strengthened_summary.requirements.push_back(
      {var_type, "hash", Access::Read, 1, "boxes:fn:4"});
  validate_summary_nonweakening(summary, strengthened_summary);

  Specialization specialization;
  specialization.callable_id = summary.callable_id;
  specialization.arguments.push_back(int_type);
  specialization.argument_access.push_back(Access::Read);
  specialization.result = {WireTypeKind::Nominal, nominal.template_id,
                           {int_type}, 0};
  specialization.materialization_owner = "consumer-free-function";
  specialization.native_symbol = "fresh_int";
  specialization.semantic_hash = "sha256:" + std::string(64, 'f');
  const auto specialization_wire = specialization_record(specialization);
  validate_specialization(specialization_wire);
  auto forged_specialization = specialization_wire;
  forged_specialization.id = "sha256:" + std::string(64, '0');
  try {
    validate_specialization(forged_specialization);
    assert(false && "forged specialization key accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_SPECIALIZATION_MISMATCH");
  }

  KeyProof proof;
  proof.key_type = int_type;
  proof.equality_callable_id = "builtin::int_isequal";
  proof.equality_specialization_key = "sha256:" + std::string(64, '1');
  proof.equality_semantic_hash = "sha256:" + std::string(64, '2');
  proof.hash_callable_id = "builtin::int_hash";
  proof.hash_specialization_key = "sha256:" + std::string(64, '3');
  proof.hash_semantic_hash = "sha256:" + std::string(64, '4');
  proof.law_contract_version = "key-laws-v1";
  proof.provenance_id = "builtin:int";
  const auto proof_wire = key_proof_record(proof);
  validate_key_proof(proof_wire);
  std::vector<Record> proofs{proof_wire};
  validate_key_proof_coherence(proofs);
  auto impure_proof = proof;
  impure_proof.hash_effect_bits = 1;
  try {
    (void)key_proof_record(impure_proof);
    assert(false && "impure key hash proof accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_CONTRACT_INVALID");
  }
  impure_proof = proof;
  impure_proof.equality_effect_tags.push_back("raise:KeyError");
  try {
    (void)key_proof_record(impure_proof);
    assert(false && "raising key equality proof accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_CONTRACT_INVALID");
  }
  auto alternate_proof = proof;
  alternate_proof.hash_semantic_hash = "sha256:" + std::string(64, '8');
  proofs.push_back(key_proof_record(alternate_proof));
  try {
    validate_key_proof_coherence(proofs);
    assert(false && "conflicting key proofs accepted");
  } catch (const Error& error) {
    assert(error.code == "KEY_PROOF_COHERENCE_MISMATCH");
  }
  auto forged_proof = proof_wire;
  forged_proof.id = "sha256:" + std::string(64, '5');
  try {
    validate_key_proof(forged_proof);
    assert(false && "forged key-proof identity accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_CONTRACT_INVALID");
  }

  // Test SourceProvenance record
  SourceProvenance prov;
  prov.provenance_id = "provenance:boxes:1";
  prov.module_id = value.module_id;
  prov.logical_source_id = "sha256:" + std::string(64, '9');
  prov.source_file_display = "src/boxes.moss";
  prov.start_line = 10;
  prov.start_column = 1;
  prov.end_line = 20;
  prov.end_column = 15;
  prov.declaration_id = "Box";
  const auto prov_rec = provenance_record(prov);
  validate_provenance(prov_rec);
  const auto decoded_prov = read_provenance(prov_rec.payload);
  assert(decoded_prov.provenance_id == prov.provenance_id);
  assert(decoded_prov.start_line == 10 && decoded_prov.end_line == 20);

  // Negative: invalid line range
  auto bad_prov = prov;
  bad_prov.start_line = 30;
  bad_prov.end_line = 20;
  try {
    (void)provenance_payload(bad_prov);
    assert(false && "inverted line range accepted");
  } catch (const Error& error) {
    assert(error.code == "SOURCE_FREE_CONTRACT_INVALID");
  }

  // Test MethodTemplate record
  MethodTemplate method;
  method.method_id = "method:Box.unbox";
  method.template_id = nominal.template_id;
  method.method_name = "unbox";
  method.exported = true;
  method.variable_count = 1;
  method.parameters = {var_type};
  method.parameter_access = {Access::Read};
  method.result = var_type;
  method.receiver_access = Access::Read;
  method.provenance_id = prov.provenance_id;
  const auto method_rec = method_template_record(method);
  validate_method_template(method_rec);
  const auto decoded_method = read_method_template(method_rec.payload);
  assert(decoded_method.method_name == "unbox");
  assert(decoded_method.template_id == nominal.template_id);

  // Test ModuleRecord
  ModuleRecord mod_rec_data;
  mod_rec_data.module_id = value.module_id;
  mod_rec_data.package_name = "boxes";
  mod_rec_data.package_version = "1.0.0";
  mod_rec_data.module_name = "Boxes";
  mod_rec_data.provider_binary_id = "sha256:" + std::string(64, 'b');
  const auto mod_rec = module_record(mod_rec_data);
  validate_module_record(mod_rec);
  const auto decoded_mod = read_module_record(mod_rec.payload);
  assert(decoded_mod.package_name == "boxes");

  // Test Builtin KeyProofs
  const auto int_proof = make_builtin_key_proof("Int");
  assert(int_proof.equality_callable_id == "builtin:key-protocol-v1:isequal:Int");
  assert(int_proof.hash_callable_id == "builtin:key-protocol-v1:hash:Int");
  validate_key_proof(key_proof_record(int_proof));

  const auto float_proof = make_builtin_key_proof("Float");
  assert(float_proof.equality_callable_id == "builtin:key-protocol-v1:isequal:Float");
  validate_key_proof(key_proof_record(float_proof));

  const auto char_proof = make_builtin_key_proof("Char");
  validate_key_proof(key_proof_record(char_proof));

  const auto str_proof = make_builtin_key_proof("String");
  validate_key_proof(key_proof_record(str_proof));

  const auto bool_proof = make_builtin_key_proof("Bool");
  validate_key_proof(key_proof_record(bool_proof));

  // Build a complete valid SealedInterface and run validate_complete_interface
  Interface full_iface;
  full_iface.module_id = value.module_id;
  full_iface.compiler = value.compiler;
  full_iface.backend_fingerprint = value.backend_fingerprint;
  full_iface.backend_rustc = value.backend_rustc;
  full_iface.imports = value.imports;
  full_iface.provenance.push_back(prov_rec);
  nominal.provenance_id = prov.provenance_id;
  nominal.fields[0].provenance_id = prov.provenance_id;
  nominal.method_template_ids.push_back(method.method_id);
  full_iface.binary_contract.push_back(template_record(nominal));
  full_iface.binary_contract.push_back(instance_record(instance));
  full_iface.semantic_contract.push_back(method_rec);
  summary.provenance_id = prov.provenance_id;
  summary.requirements[0].provenance_id = prov.provenance_id;
  summary.dependencies.clear(); // self-contained
  full_iface.semantic_contract.push_back(summary_record(summary));
  full_iface.semantic_contract.push_back(key_proof_record(int_proof));

  auto sort_records = [](std::vector<Record>& recs) {
    std::sort(recs.begin(), recs.end(), [](const Record& a, const Record& b) {
      return a.id < b.id;
    });
  };
  sort_records(full_iface.binary_contract);
  sort_records(full_iface.semantic_contract);
  sort_records(full_iface.provenance);

  const auto sealed_full = decode(encode(full_iface));
  validate_complete_interface(sealed_full, instance.provider_binary_id);

  // Linkage tests (Workstream B5)
  using moss::abi_v10::RuntimeCrateIdentity;
  using moss::abi_v10::ContractChangeKind;
  RuntimeCrateIdentity rt_provider;
  rt_provider.name = "moss_runtime";
  rt_provider.version = "0.1.0";
  rt_provider.artifact_hash = "sha256:" + std::string(64, 'a');
  rt_provider.profile = "debug";

  RuntimeCrateIdentity rt_consumer = rt_provider;
  moss::abi_v10::verify_runtime_compatibility(rt_provider, rt_consumer);

  // Mismatch name
  rt_consumer.name = "other_runtime";
  try {
    moss::abi_v10::verify_runtime_compatibility(rt_provider, rt_consumer);
    assert(false && "name mismatch accepted");
  } catch (const Error& err) {
    assert(err.code == "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");
  }
  rt_consumer.name = rt_provider.name;

  // Mismatch version
  rt_consumer.version = "0.2.0";
  try {
    moss::abi_v10::verify_runtime_compatibility(rt_provider, rt_consumer);
    assert(false && "version mismatch accepted");
  } catch (const Error& err) {
    assert(err.code == "SOURCE_FREE_ABI_REBUILD_REQUIRED");
  }
  rt_consumer.version = rt_provider.version;

  // Mismatch hash
  rt_consumer.artifact_hash = "sha256:" + std::string(64, 'b');
  try {
    moss::abi_v10::verify_runtime_compatibility(rt_provider, rt_consumer);
    assert(false && "artifact hash mismatch accepted");
  } catch (const Error& err) {
    assert(err.code == "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH");
  }

  // Contract change detection
  auto altered_iface = sealed_full;
  assert(moss::abi_v10::detect_contract_change(sealed_full, altered_iface) == ContractChangeKind::None);

  altered_iface.binary_hash = "sha256:" + std::string(64, '1');
  assert(moss::abi_v10::detect_contract_change(sealed_full, altered_iface) == ContractChangeKind::BinaryChange);

  altered_iface = sealed_full;
  altered_iface.semantic_hash = "sha256:" + std::string(64, '2');
  assert(moss::abi_v10::detect_contract_change(sealed_full, altered_iface) == ContractChangeKind::SemanticChange);

  altered_iface = sealed_full;
  altered_iface.interface_hash = "sha256:" + std::string(64, '3');
  assert(moss::abi_v10::detect_contract_change(sealed_full, altered_iface) == ContractChangeKind::InterfaceHashOnly);

  // Cache key derivation and sensitivity
  const std::string base_key = moss::abi_v10::compute_cache_dependency_key(
      sealed_full, rt_provider, "moss-0.1.0", "backend-x86_64");
  assert(!base_key.empty());
  assert(base_key.rfind("sha256:", 0) == 0);

  // Stable across invocations with same inputs
  const std::string base_key_repeat = moss::abi_v10::compute_cache_dependency_key(
      sealed_full, rt_provider, "moss-0.1.0", "backend-x86_64");
  assert(base_key == base_key_repeat);

  // Invalidated when runtime artifact hash changes
  rt_consumer.artifact_hash = "sha256:" + std::string(64, 'f');
  const std::string rt_changed_key = moss::abi_v10::compute_cache_dependency_key(
      sealed_full, rt_consumer, "moss-0.1.0", "backend-x86_64");
  assert(base_key != rt_changed_key);

  // Invalidated when provider interface changes
  altered_iface = sealed_full;
  altered_iface.binary_hash = "sha256:" + std::string(64, '9');
  const std::string iface_changed_key = moss::abi_v10::compute_cache_dependency_key(
      altered_iface, rt_provider, "moss-0.1.0", "backend-x86_64");
  assert(base_key != iface_changed_key);

  // Flags generation
  const auto flags = moss::abi_v10::generate_compiler_runtime_flags("/path/to/libmoss_runtime.rlib");
  assert(flags.size() == 4);
  assert(flags[0] == "-L");
  assert(flags[1] == "dependency=/path/to");
  assert(flags[2] == "--extern");
  assert(flags[3] == "moss_runtime=/path/to/libmoss_runtime.rlib");

  std::cout << "ABI-v10 staged container tests passed\n";
}
