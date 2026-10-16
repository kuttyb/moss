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
  valid.body_semantic_hash = digest;
  valid.provenance_id = "provider:outer:1";

  const auto record = summary_record(valid);
  validate_summary(record);
  const auto roundtrip = read_summary(record.payload);
  assert(roundtrip.parameter_access[0] == Access::Write);
  assert(roundtrip.requirements[1].parent == 1);
  assert(roundtrip.requirements[1].provenance_id == "provider:inner:2");

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

  std::cout << "ABI-v10 staged summary adversarial tests passed\n";
}
