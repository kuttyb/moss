#include "abi_v10_records.hpp"
#include "checked_body_ir.hpp"
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

static std::uint32_t read_number(const std::string& bytes, std::size_t& offset) {
  assert(offset + 4 <= bytes.size());
  std::uint32_t result = 0;
  for (int i = 0; i < 4; ++i)
    result = (result << 8) | static_cast<unsigned char>(bytes[offset++]);
  return result;
}

static void skip_string(const std::string& bytes, std::size_t& offset) {
  const auto size = read_number(bytes, offset);
  assert(size <= bytes.size() - offset);
  offset += size;
}

static void skip_type(const std::string& bytes, std::size_t& offset) {
  assert(offset < bytes.size());
  const auto tag = static_cast<unsigned char>(bytes[offset++]);
  if (tag == static_cast<unsigned char>(WireTypeKind::BoundVar)) {
    (void)read_number(bytes, offset);
    return;
  }
  if (tag == static_cast<unsigned char>(WireTypeKind::Unit)) return;
  skip_string(bytes, offset);
  const auto count = read_number(bytes, offset);
  for (std::uint32_t i = 0; i < count; ++i) skip_type(bytes, offset);
}

struct FirstRequirementOffsets {
  std::size_t argument_type = 0;
  std::size_t result_type = 0;
  std::size_t argument_access_count = 0;
};

static FirstRequirementOffsets first_requirement_offsets(
    const std::string& bytes) {
  std::size_t offset = 1;  // Summary record version.
  skip_string(bytes, offset);
  (void)read_number(bytes, offset);  // Variable count.
  const auto parameters = read_number(bytes, offset);
  for (std::uint32_t i = 0; i < parameters; ++i) {
    skip_type(bytes, offset);
    ++offset;  // Parameter access.
  }
  skip_type(bytes, offset);  // Summary result.
  assert(read_number(bytes, offset) > 0);
  skip_type(bytes, offset);  // Requirement receiver.
  skip_string(bytes, offset);
  ++offset;  // Requirement access.
  (void)read_number(bytes, offset);  // Parent.
  skip_string(bytes, offset);
  const auto arguments = read_number(bytes, offset);
  assert(arguments > 0);
  FirstRequirementOffsets result;
  result.argument_type = offset;
  for (std::uint32_t i = 0; i < arguments; ++i) skip_type(bytes, offset);
  const auto has_result = static_cast<unsigned char>(bytes[offset++]);
  assert(has_result == 1);
  result.result_type = offset;
  skip_type(bytes, offset);
  result.argument_access_count = offset;
  return result;
}

static void write_number(std::string& bytes, std::size_t offset,
                         std::uint32_t value) {
  assert(offset + 4 <= bytes.size());
  for (int shift = 24; shift >= 0; shift -= 8)
    bytes[offset++] = static_cast<char>((value >> shift) & 0xff);
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
      {variable, "operator:*", Access::Read, 0, "provider:outer:1",
       {{WireTypeKind::Primitive, "int", {}, 0}}, variable, {Access::Read}},
      {variable, "operator:+", Access::Read, 1, "provider:inner:2",
       {{WireTypeKind::Primitive, "int", {}, 0}}, variable, {Access::Read}},
  };
  valid.dependencies = {{"helper:inner", body_digest}};
  valid.observable_effect_bits = 1;
  valid.observable_effect_tags = {"raise:ProviderError"};
  valid.body_semantic_hash = body_digest;
  valid.provenance_id = "provider:outer:1";

  const auto record = summary_record(valid);
  validate_summary(record);
  const auto roundtrip = read_summary(record.payload);
  assert(summary_payload(roundtrip) == record.payload);
  assert(roundtrip.parameter_access[0] == Access::Write);
  assert(roundtrip.requirements[1].parent == 1);
  assert(roundtrip.requirements[1].provenance_id == "provider:inner:2");
  assert(roundtrip.requirements[0].operation == "operator:*");
  assert(roundtrip.requirements[1].operation == "operator:+");
  for (std::size_t i = 0; i < roundtrip.requirements.size(); ++i) {
    const auto& requirement = roundtrip.requirements[i];
    assert(requirement.receiver.kind == WireTypeKind::BoundVar &&
           requirement.receiver.ordinal == 0);
    assert(requirement.arguments.size() == 1 &&
           requirement.arguments[0].kind == WireTypeKind::Primitive &&
           requirement.arguments[0].identity == "int");
    assert(requirement.result &&
           requirement.result->kind == WireTypeKind::BoundVar &&
           requirement.result->ordinal == 0);
    assert(requirement.argument_access == std::vector<Access>{Access::Read});
  }
  assert(roundtrip.observable_effect_tags == valid.observable_effect_tags);

  auto callback_summary = valid;
  callback_summary.requirements = {
      {variable, "static callable", Access::Read, 0, "provider:callback:1",
       {{WireTypeKind::Primitive, "int", {}, 0}}, variable,
       {Access::Read}},
  };
  callback_summary.dependencies.clear();
  callback_summary.observable_effect_bits = 0;
  callback_summary.observable_effect_tags.clear();
  callback_summary.effect_dependencies = {1};
  const auto callback_v4 = summary_record(callback_summary);
  assert(static_cast<unsigned char>(callback_v4.payload.front()) == 4);
  validate_summary(callback_v4);
  assert(read_summary(callback_v4.payload).effect_dependencies ==
         std::vector<std::uint32_t>{1});
  callback_summary.effect_dependencies.clear();
  const auto callback_v3 = summary_record(callback_summary);
  assert(static_cast<unsigned char>(callback_v3.payload.front()) == 3);
  const auto callback_v2_payload = summary_payload(callback_summary, 2);
  assert(static_cast<unsigned char>(callback_v2_payload.front()) == 2);
  assert(summary_payload(read_summary(callback_v2_payload), 2) ==
         callback_v2_payload);
  assert(summary_payload(read_summary(callback_v3.payload)) ==
         callback_v3.payload);
  auto unsupported_summary = callback_v4.payload;
  unsupported_summary.front() = 5;
  expect_error([&] { (void)read_summary(unsupported_summary); },
               "SOURCE_FREE_SUMMARY_INVALID");
  auto invalid_callback = callback_summary;
  invalid_callback.effect_dependencies = {1, 1};
  expect_error([&] { (void)summary_payload(invalid_callback, 4); },
               "SOURCE_FREE_SUMMARY_INVALID");
  invalid_callback.effect_dependencies = {2};
  expect_error([&] { (void)summary_payload(invalid_callback, 4); },
               "SOURCE_FREE_SUMMARY_INVALID");
  invalid_callback.effect_dependencies = {1};
  invalid_callback.requirements.push_back(callback_summary.requirements.front());
  invalid_callback.effect_dependencies = {2, 1};
  expect_error([&] { (void)summary_payload(invalid_callback, 4); },
               "SOURCE_FREE_SUMMARY_INVALID");
  auto provider_callback = callback_summary;
  provider_callback.effect_dependencies = {1};
  auto consumer_callback = provider_callback;
  consumer_callback.requirements.push_back(
      {variable, "operator:+", Access::Read, 0, "consumer:extra:1",
       {{WireTypeKind::Primitive, "int", {}, 0}}, variable,
       {Access::Read}});
  validate_summary_nonweakening(provider_callback, consumer_callback);
  consumer_callback.effect_dependencies.clear();
  expect_error([&] {
    validate_summary_nonweakening(provider_callback, consumer_callback);
  }, "SOURCE_FREE_SUMMARY_INVALID");

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
  weakened.requirements[0].arguments[0].identity = "float";
  expect_error([&] { validate_summary_nonweakening(valid, weakened); },
               "SOURCE_FREE_SUMMARY_INVALID");
  weakened = valid;
  weakened.requirements[0].result =
      WireType{WireTypeKind::Primitive, "int", {}, 0};
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
  malformed.requirements[0].argument_access[0] = static_cast<Access>(0);
  expect_error([&] { (void)summary_payload(malformed); },
               "SOURCE_FREE_SUMMARY_INVALID");
  malformed = valid;
  malformed.requirements[0].arguments.resize(65);
  malformed.requirements[0].argument_access.resize(65, Access::Read);
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

  auto old_version = record.payload;
  old_version[0] = 1;
  expect_error([&] { (void)read_summary(old_version); },
               "SOURCE_FREE_SUMMARY_INVALID");
  const auto offsets = first_requirement_offsets(record.payload);
  auto corrupt_type = record.payload;
  corrupt_type[offsets.argument_type] = 0;
  expect_error([&] { (void)read_summary(corrupt_type); },
               "SOURCE_FREE_CONTRACT_INVALID");
  corrupt_type = record.payload;
  corrupt_type[offsets.result_type] = 0;
  expect_error([&] { (void)read_summary(corrupt_type); },
               "SOURCE_FREE_CONTRACT_INVALID");
  auto bad_access_count = record.payload;
  write_number(bad_access_count, offsets.argument_access_count, 0);
  expect_error([&] { (void)read_summary(bad_access_count); },
               "SOURCE_FREE_SUMMARY_INVALID");

  auto out_of_scope = valid;
  out_of_scope.requirements[0].arguments[0] =
      WireType{WireTypeKind::BoundVar, "", {}, 1};
  expect_error([&] { (void)summary_payload(out_of_scope); },
               "SOURCE_FREE_SUMMARY_INVALID");
  out_of_scope = valid;
  out_of_scope.requirements[0].result =
      WireType{WireTypeKind::BoundVar, "", {}, 1};
  expect_error([&] { (void)summary_payload(out_of_scope); },
               "SOURCE_FREE_SUMMARY_INVALID");

  auto argument_only_var = valid;
  argument_only_var.parameters = {
      WireType{WireTypeKind::Primitive, "int", {}, 0}};
  argument_only_var.result = WireType{WireTypeKind::Unit, "", {}, 0};
  argument_only_var.requirements.resize(1);
  argument_only_var.requirements[0].receiver =
      WireType{WireTypeKind::Primitive, "int", {}, 0};
  argument_only_var.requirements[0].arguments[0] = variable;
  argument_only_var.requirements[0].result.reset();
  assert(summary_ordinals_dense(argument_only_var));
  assert(!summary_payload(argument_only_var).empty());
  auto result_only_var = argument_only_var;
  result_only_var.requirements[0].arguments[0] =
      WireType{WireTypeKind::Primitive, "int", {}, 0};
  result_only_var.requirements[0].result = variable;
  assert(summary_ordinals_dense(result_only_var));
  assert(!summary_payload(result_only_var).empty());

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

  // Re-sealed private records cannot extend the provider beyond the
  // dependency closure rooted at the exported callable.
  Summary orphan_private = private_helper;
  orphan_private.callable_id = "helper:unreachable";
  Interface iface_with_orphan = iface_with_helper;
  iface_with_orphan.private_semantic_dependencies.push_back(
      summary_record(orphan_private));
  std::sort(iface_with_orphan.private_semantic_dependencies.begin(),
            iface_with_orphan.private_semantic_dependencies.end(),
            [](const Record& left, const Record& right) {
              return left.id < right.id;
            });
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_orphan)));
  }, "SOURCE_FREE_PRIVATE_DEPENDENCY_UNEXPECTED");

  // Replacing the authorized helper with a distinct, correctly resealed
  // summary leaves the authorized dependency unresolved and the replacement
  // private record outside the closure.
  Interface iface_with_substitute = iface_with_helper;
  orphan_private.callable_id = private_helper.callable_id + ":substitute";
  iface_with_substitute.private_semantic_dependencies = {
      summary_record(orphan_private)};
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_substitute)));
  }, "SOURCE_FREE_PRIVATE_DEPENDENCY_UNEXPECTED");

  const auto private_template = [&](const std::string& name) {
    NominalTemplate value;
    value.template_id = "nominal-template-v1:" + iface.module_id +
        ":type:" + name;
    value.module_id = iface.module_id;
    value.declaration_name = name;
    value.representation_hash = "sha256:" + std::string(64, 'c');
    value.provenance_id = prov.provenance_id;
    return value;
  };
  const auto private_enum = [&](const std::string& name) {
    PrivateEnumDeclaration value;
    value.declaration_id = "nominal-enum-v1:" + iface.module_id +
        ":enum:" + name;
    value.module_id = iface.module_id;
    value.declaration_name = name;
    value.variants.push_back({
        "Only", "nominal-enum-variant-v1:" + value.declaration_id +
            ":case:Only", {}});
    value.provenance_id = prov.provenance_id;
    return value;
  };
  const auto canonicalize_record_order = [](Interface& value) {
    const auto by_id = [](const Record& left, const Record& right) {
      return left.id < right.id;
    };
    std::sort(value.binary_contract.begin(), value.binary_contract.end(), by_id);
    std::sort(value.semantic_contract.begin(), value.semantic_contract.end(), by_id);
    std::sort(value.private_semantic_dependencies.begin(),
              value.private_semantic_dependencies.end(), by_id);
    std::sort(value.provenance.begin(), value.provenance.end(), by_id);
  };

  auto orphan_template = private_template("Unreachable");
  Interface iface_with_orphan_template = iface_with_helper;
  iface_with_orphan_template.private_semantic_dependencies.push_back(
      template_record(orphan_template));
  canonicalize_record_order(iface_with_orphan_template);
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_orphan_template)));
  }, "SOURCE_FREE_PRIVATE_DEPENDENCY_UNEXPECTED");

  auto missing_nominal_root = private_template("MissingNominalRoot");
  missing_nominal_root.fields.push_back({
      "child", {WireTypeKind::Nominal,
          "nominal-template-v1:" + iface.module_id + ":type:MissingChild",
          {}, 0}, true, prov.provenance_id});
  Interface iface_with_missing_nominal = iface_with_helper;
  iface_with_missing_nominal.binary_contract.push_back(
      template_record(missing_nominal_root));
  canonicalize_record_order(iface_with_missing_nominal);
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_missing_nominal)));
  }, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");

  auto missing_enum_root = private_template("MissingEnumRoot");
  missing_enum_root.fields.push_back({
      "state", {WireTypeKind::Enum,
          "nominal-enum-v1:" + iface.module_id + ":enum:MissingEnum",
          {}, 0}, true, prov.provenance_id});
  Interface iface_with_missing_enum = iface_with_helper;
  iface_with_missing_enum.binary_contract.push_back(
      template_record(missing_enum_root));
  canonicalize_record_order(iface_with_missing_enum);
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_missing_enum)));
  }, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");

  auto public_enum_root = private_template("PublicEnumRoot");
  const std::string public_enum_id =
      "nominal-enum-v1:" + iface.module_id + ":enum:PublicError";
  public_enum_root.fields.push_back({
      "state", {WireTypeKind::Enum, public_enum_id, {}, 0}, true,
      prov.provenance_id});
  Interface iface_with_public_enum = iface_with_helper;
  iface_with_public_enum.binary_contract.push_back(
      template_record(public_enum_root));
  iface_with_public_enum.semantic_contract.push_back({
      "00000000 export PublicError",
      "export enum PublicError\n  declaration_id " + public_enum_id + "\n"});
  canonicalize_record_order(iface_with_public_enum);
  validate_complete_interface(decode(encode(iface_with_public_enum)));

  Interface iface_with_mismatched_public_enum_id = iface_with_public_enum;
  auto public_enum_record = std::find_if(
      iface_with_mismatched_public_enum_id.semantic_contract.begin(),
      iface_with_mismatched_public_enum_id.semantic_contract.end(),
      [](const Record& record) {
        return record.id == "00000000 export PublicError";
      });
  assert(public_enum_record !=
         iface_with_mismatched_public_enum_id.semantic_contract.end());
  public_enum_record->payload =
      "export enum PublicError\n  declaration_id nominal-enum-v1:" +
      iface.module_id + ":enum:ForgedError\n";
  canonicalize_record_order(iface_with_mismatched_public_enum_id);
  expect_error([&] {
    validate_complete_interface(
        decode(encode(iface_with_mismatched_public_enum_id)));
  }, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");

  Interface iface_with_mismatched_public_enum_record = iface_with_public_enum;
  auto mismatched_record = std::find_if(
      iface_with_mismatched_public_enum_record.semantic_contract.begin(),
      iface_with_mismatched_public_enum_record.semantic_contract.end(),
      [](const Record& record) {
        return record.id == "00000000 export PublicError";
      });
  assert(mismatched_record !=
         iface_with_mismatched_public_enum_record.semantic_contract.end());
  mismatched_record->id =
      "00000000 export ForgedError";
  expect_error([&] {
    validate_complete_interface(
        decode(encode(iface_with_mismatched_public_enum_record)));
  }, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");

  Interface iface_with_duplicate_public_enum_id = iface_with_public_enum;
  iface_with_duplicate_public_enum_id.semantic_contract.push_back(
      *std::find_if(iface_with_public_enum.semantic_contract.begin(),
          iface_with_public_enum.semantic_contract.end(),
          [](const Record& record) {
            return record.id == "00000000 export PublicError";
          }));
  iface_with_duplicate_public_enum_id.semantic_contract.back().id =
      "00000001 export PublicError";
  canonicalize_record_order(iface_with_duplicate_public_enum_id);
  expect_error([&] {
    validate_complete_interface(
        decode(encode(iface_with_duplicate_public_enum_id)));
  }, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");

  const std::string imported_module = "module-v1:other@1.0.0::Other";
  auto imported_type_root = private_template("ImportedTypeRoot");
  imported_type_root.fields.push_back({
      "child", {WireTypeKind::Nominal,
          "nominal-template-v1:" + imported_module + ":type:Record",
          {}, 0}, true, prov.provenance_id});
  Interface iface_with_imported_type = iface_with_helper;
  iface_with_imported_type.imports.push_back({imported_module, digest, digest});
  iface_with_imported_type.binary_contract.push_back(
      template_record(imported_type_root));
  canonicalize_record_order(iface_with_imported_type);
  validate_complete_interface(decode(encode(iface_with_imported_type)));

  auto imported_enum_root = private_template("ImportedEnumRoot");
  imported_enum_root.fields.push_back({
      "state", {WireTypeKind::Enum,
          "nominal-enum-v1:" + imported_module + ":enum:ExternalError",
          {}, 0}, true, prov.provenance_id});
  Interface iface_with_imported_enum = iface_with_helper;
  iface_with_imported_enum.imports.push_back({imported_module, digest, digest});
  iface_with_imported_enum.binary_contract.push_back(
      template_record(imported_enum_root));
  canonicalize_record_order(iface_with_imported_enum);
  validate_complete_interface(decode(encode(iface_with_imported_enum)));

  auto orphan_enum = private_enum("Unreachable");
  Interface iface_with_orphan_enum = iface_with_helper;
  iface_with_orphan_enum.private_semantic_dependencies.push_back(
      private_enum_record(orphan_enum));
  canonicalize_record_order(iface_with_orphan_enum);
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_orphan_enum)));
  }, "SOURCE_FREE_PRIVATE_DEPENDENCY_UNEXPECTED");

  auto method_owner = private_template("MethodOwner");
  method_owner.template_id = "nominal-template-v1:" + iface.module_id +
      ":type:MethodOwner";
  MethodTemplate orphan_method;
  orphan_method.method_id = "method-v1:" + method_owner.template_id +
      ":method:unreachable";
  orphan_method.template_id = method_owner.template_id;
  orphan_method.method_name = "unreachable";
  orphan_method.result = {WireTypeKind::Primitive, "int", {}, 0};
  orphan_method.provenance_id = prov.provenance_id;
  Interface iface_with_orphan_method = iface_with_helper;
  iface_with_orphan_method.binary_contract.push_back(
      template_record(method_owner));
  iface_with_orphan_method.private_semantic_dependencies.push_back(
      method_template_record(orphan_method));
  canonicalize_record_order(iface_with_orphan_method);
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_orphan_method)));
  }, "SOURCE_FREE_PRIVATE_DEPENDENCY_UNEXPECTED");

  // Reachability follows public template -> private method -> nested private
  // nominal result, and private enum payload -> nested private enum.
  auto public_owner = private_template("PublicOwner");
  const std::string nested_method_id =
      "method-v1:" + public_owner.template_id + ":method:lookup";
  public_owner.method_template_ids = {nested_method_id};
  auto nested_type = private_template("NestedResult");
  MethodTemplate nested_method;
  nested_method.method_id = nested_method_id;
  nested_method.template_id = public_owner.template_id;
  nested_method.method_name = "lookup";
  nested_method.result = {WireTypeKind::Nominal,
                          nested_type.template_id, {}, 0};
  nested_method.provenance_id = prov.provenance_id;
  Interface iface_transitive_method = iface_with_helper;
  iface_transitive_method.binary_contract.push_back(
      template_record(public_owner));
  iface_transitive_method.private_semantic_dependencies.push_back(
      method_template_record(nested_method));
  iface_transitive_method.private_semantic_dependencies.push_back(
      template_record(nested_type));
  canonicalize_record_order(iface_transitive_method);
  validate_complete_interface(decode(encode(iface_transitive_method)));

  auto enum_owner = private_template("EnumOwner");
  auto outer_enum = private_enum("Outer");
  auto inner_enum = private_enum("Inner");
  outer_enum.variants.front().fields.push_back({
      "nested", {WireTypeKind::Enum, inner_enum.declaration_id, {}, 0},
      false, prov.provenance_id});
  enum_owner.fields.push_back({
      "state", {WireTypeKind::Enum, outer_enum.declaration_id, {}, 0},
      true, prov.provenance_id});
  Interface iface_transitive_enum = iface_with_helper;
  iface_transitive_enum.binary_contract.push_back(template_record(enum_owner));
  iface_transitive_enum.private_semantic_dependencies.push_back(
      private_enum_record(outer_enum));
  iface_transitive_enum.private_semantic_dependencies.push_back(
      private_enum_record(inner_enum));
  canonicalize_record_order(iface_transitive_enum);
  validate_complete_interface(decode(encode(iface_transitive_enum)));

  auto substituted_enum = private_enum("Substituted");
  auto enum_root = private_template("EnumRoot");
  enum_root.fields.push_back({
      "state", {WireTypeKind::Enum, "nominal-enum-v1:" + iface.module_id +
                   ":enum:Expected", {}, 0}, true, prov.provenance_id});
  Interface iface_with_substituted_enum = iface_with_helper;
  iface_with_substituted_enum.binary_contract.push_back(
      template_record(enum_root));
  iface_with_substituted_enum.private_semantic_dependencies.push_back(
      private_enum_record(substituted_enum));
  canonicalize_record_order(iface_with_substituted_enum);
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_substituted_enum)));
  }, "SOURCE_FREE_TYPE_IDENTITY_MISMATCH");

  moss::checked_ir::Expr literal;
  literal.kind = moss::checked_ir::ExprKind::IntegerLiteral;
  literal.type = moss::Type::primitive("int");
  literal.literal_value = "7";
  moss::checked_ir::Stmt returned;
  returned.kind = moss::checked_ir::StmtKind::Return;
  returned.type = literal.type;
  returned.expr = literal;
  const std::vector<moss::checked_ir::Stmt> checked_statements{returned};
  Summary body_owner = valid_summary;
  body_owner.callable_id = "callable-v1:module-v1:provider@1.0.0::Provider:fn:body_owner";
  body_owner.parameters.clear();
  body_owner.parameter_access.clear();
  body_owner.requirements.clear();
  body_owner.variable_count = 0;
  body_owner.result = {WireTypeKind::Primitive, "int", {}, 0};
  body_owner.body_semantic_hash =
      moss::checked_ir::compute_checked_body_hash_v1(checked_statements);
  CheckedBodyRecord checked_body;
  checked_body.callable_id = body_owner.callable_id;
  checked_body.semantic_hash = body_owner.body_semantic_hash;
  checked_body.checked_ir_bytes =
      moss::checked_ir::encode_checked_body_v1(checked_statements);
  Interface iface_with_body = iface;
  iface_with_body.semantic_contract = {summary_record(body_owner)};
  iface_with_body.private_semantic_dependencies = {
      checked_body_record(checked_body)};
  validate_complete_interface(decode(encode(iface_with_body)));

  Summary orphan_body_owner = body_owner;
  orphan_body_owner.callable_id = "callable-v1:module-v1:provider@1.0.0::Provider:fn:orphan_body";
  CheckedBodyRecord orphan_body = checked_body;
  orphan_body.callable_id = orphan_body_owner.callable_id;
  orphan_body_owner.body_semantic_hash = orphan_body.semantic_hash;
  Interface iface_with_orphan_body = iface_with_body;
  iface_with_orphan_body.private_semantic_dependencies.push_back(
      summary_record(orphan_body_owner));
  iface_with_orphan_body.private_semantic_dependencies.push_back(
      checked_body_record(orphan_body));
  canonicalize_record_order(iface_with_orphan_body);
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_orphan_body)));
  }, "SOURCE_FREE_PRIVATE_DEPENDENCY_UNEXPECTED");

  Interface iface_with_substituted_body = iface_with_body;
  iface_with_substituted_body.private_semantic_dependencies = {
      summary_record(orphan_body_owner), checked_body_record(orphan_body)};
  canonicalize_record_order(iface_with_substituted_body);
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_substituted_body)));
  }, "SOURCE_FREE_PRIVATE_DEPENDENCY_UNEXPECTED");
  Interface malformed_body_iface = iface_with_body;
  malformed_body_iface.private_semantic_dependencies.front().payload.resize(12);
  expect_error([&] {
    validate_complete_interface(decode(encode(malformed_body_iface)));
  }, "SOURCE_FREE_CONTRACT_INVALID");
  Interface forged_body_id_iface = iface_with_body;
  forged_body_id_iface.private_semantic_dependencies.front().id += ":forged";
  expect_error([&] {
    validate_complete_interface(decode(encode(forged_body_id_iface)));
  }, "SOURCE_FREE_SUMMARY_INVALID");
  checked_body.checked_ir_bytes.back() =
      checked_body.checked_ir_bytes.back() == '0' ? '1' : '0';
  expect_error([&] { (void)checked_body_record(checked_body); },
               "SOURCE_FREE_SUMMARY_INVALID");
  checked_body.checked_ir_bytes =
      moss::checked_ir::encode_checked_body_v1(checked_statements);
  checked_body.dependencies.push_back({
      CheckedBodyRecord::Dependency::Kind::Callable,
      private_helper.callable_id, body_owner.body_semantic_hash});
  CheckedBodyRecord wrong_kind_body = checked_body;
  wrong_kind_body.dependencies[0].kind =
      CheckedBodyRecord::Dependency::Kind::NominalType;
  expect_error([&] { (void)checked_body_record(wrong_kind_body); },
               "SOURCE_FREE_SUMMARY_INVALID");
  Summary checked_private_helper = private_helper;
  checked_private_helper.body_semantic_hash = body_owner.body_semantic_hash;
  iface_with_body.private_semantic_dependencies.push_back(
      summary_record(checked_private_helper));
  iface_with_body.private_semantic_dependencies[0] =
      checked_body_record(checked_body);
  std::sort(iface_with_body.private_semantic_dependencies.begin(),
            iface_with_body.private_semantic_dependencies.end(),
            [](const Record& left, const Record& right) {
              return left.id < right.id;
            });
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_body)));
  }, "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING");
  CheckedBodyRecord private_checked_body = checked_body;
  private_checked_body.callable_id = checked_private_helper.callable_id;
  private_checked_body.dependencies.clear();
  iface_with_body.private_semantic_dependencies.push_back(
      checked_body_record(private_checked_body));
  std::sort(iface_with_body.private_semantic_dependencies.begin(),
            iface_with_body.private_semantic_dependencies.end(),
            [](const Record& left, const Record& right) {
              return left.id < right.id;
            });
  validate_complete_interface(decode(encode(iface_with_body)));
  checked_body.dependencies[0].expected_hash =
      "body-semantic-v1:sha256:" + std::string(64, 'b');
  for (auto& record : iface_with_body.private_semantic_dependencies)
    if (record.id == "checked-body-v1:" + body_owner.callable_id)
      record = checked_body_record(checked_body);
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_with_body)));
  }, "SOURCE_FREE_CONTRACT_INVALID");

  Interface malformed_public = iface_with_helper;
  malformed_public.semantic_contract.front().payload.resize(10);
  expect_error([&] {
    validate_complete_interface(decode(encode(malformed_public)));
  }, "SOURCE_FREE_CONTRACT_INVALID");
  Interface malformed_private = iface_with_helper;
  malformed_private.private_semantic_dependencies.front().payload.resize(10);
  expect_error([&] {
    validate_complete_interface(decode(encode(malformed_private)));
  }, "SOURCE_FREE_CONTRACT_INVALID");
  Interface unknown_private_version = iface_with_helper;
  assert(static_cast<unsigned char>(
      unknown_private_version.private_semantic_dependencies.front().payload[0]) == 3);
  validate_complete_interface(decode(encode(unknown_private_version)));
  unknown_private_version.private_semantic_dependencies.front().payload[0] = 4;
  expect_error([&] {
    validate_complete_interface(decode(encode(unknown_private_version)));
  }, "SOURCE_FREE_CONTRACT_INVALID");

  Summary mismatched_helper = private_helper;
  mismatched_helper.body_semantic_hash =
      "body-semantic-v1:sha256:" + std::string(64, 'b');
  Interface iface_bad_hash = iface_with_helper;
  iface_bad_hash.private_semantic_dependencies = {
      summary_record(mismatched_helper)};
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_bad_hash)));
  }, "SOURCE_FREE_CONTRACT_INVALID");

  Interface iface_duplicate_private = iface_with_helper;
  iface_duplicate_private.private_semantic_dependencies.push_back(
      summary_record(mismatched_helper));
  std::sort(iface_duplicate_private.private_semantic_dependencies.begin(),
            iface_duplicate_private.private_semantic_dependencies.end(),
            [](const Record& left, const Record& right) {
              return left.id < right.id;
            });
  expect_error([&] {
    validate_complete_interface(decode(encode(iface_duplicate_private)));
  }, "SOURCE_FREE_CONTRACT_INVALID");

  Interface iface_unresolved_import = iface_with_helper;
  iface_unresolved_import.imports.push_back({
      "module-v1:other@1.0.0::Other", digest, digest});
  Summary external_dependency = public_with_helper;
  external_dependency.dependencies = {{
      "callable-v1:module-v1:other@1.0.0::Other:fn:helper", body_digest}};
  iface_unresolved_import.semantic_contract = {
      summary_record(external_dependency)};
  iface_unresolved_import.private_semantic_dependencies.clear();
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
