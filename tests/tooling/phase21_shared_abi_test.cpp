#include "error_handling_abi.hpp"
#include "functional_ir.hpp"

#include <cassert>
#include <map>
#include <string>
#include <vector>

using namespace moss;

int main() {
  const auto full = RaisedIdentity::enum_variant("FileError", "Full");
  const auto io = RaisedIdentity::enum_variant("FileError", "IO");
  const auto invalid = RaisedIdentity::enum_variant("ParseError", "Invalid");
  const auto scalar = RaisedIdentity::scalar("LegacyCode");
  const auto qualified =
      RaisedIdentity::enum_variant("provider::FileError", "Full");

  RaiseSet normalized = {io, full, io, scalar};
  assert(normalized.size() == 3);
  assert(normalized.begin()->canonical() == "enum:FileError:Full");
  RaiseSet merged = raise_set_union(normalized, {invalid});
  assert(merged.size() == 4);
  RaiseSet remaining = raise_set_without(merged, {full, scalar});
  assert(remaining == RaiseSet({io, invalid}));

  ObservableEffects effects;
  effects.unresolved = false;
  effects.raise_set = {full};
  assert(!effects.fusion_safe());
  assert(effects.chunk_speculation_safe());
  effects.remove_handled({full});
  assert(effects.fusion_safe());
  effects.re_raise(io);
  assert(effects.raise_set == RaiseSet({io}));
  effects.may_panic = true;
  assert(!effects.chunk_speculation_safe());

  assert(failure_capture_allowed(
      {"private_value", "ProviderPrivate", "private_value", true, true}));
  assert(!failure_capture_allowed(
      {"file", "FileIO", "file", true, true}));
  assert(!failure_capture_allowed(
      {"view", "Range", "view", true, true}));
  assert(!failure_capture_allowed(
      {"borrow", "&ProviderPrivate", "borrow", true, true}));
  assert(!failure_capture_allowed(
      {"late", "Int", "late", true, false}));
  assert(!failure_capture_allowed(
      {"handle", "DomainHandle[Worker]", "handle", true, true}));

  ExceptionalCfgEdgeAbi edge;
  edge.source_node = 7;
  edge.destination_node = 11;
  edge.kind = ExceptionalEdgeKind::RaiseToRecover;
  edge.alternatives = {full};
  edge.consumers = kAllExceptionalCfgConsumers;
  assert(verify_exceptional_cfg({7}, {edge}).ok);
  edge.consumers &= ~exceptional_consumer_bit(
      ExceptionalCfgConsumer::GuardCancellation);
  assert(!verify_exceptional_cfg({7}, {edge}).ok);
  assert(!verify_exceptional_cfg({8}, {}).ok);

  const auto& sequence = root_failure_sequence();
  assert(sequence == std::vector<RootFailureStep>({
      RootFailureStep::JoinBranches,
      RootFailureStep::CloseRootFileIO,
      RootFailureStep::ReleaseAttemptGuards,
      RootFailureStep::AcquireFreshFailureLocks,
      RootFailureStep::ExecuteSelectedArm,
      RootFailureStep::FulfillReply,
  }));

  FailureArmAbi arm;
  arm.source_order = 0;
  arm.pattern.variants = {full};
  arm.callable_symbol = "__moss_on_fail_Worker_Run_0";
  arm.captures = {
      {"job_id", "ProviderPrivate", "job_id", true, true},
  };
  arm.state_writes = {"x"};

  ProviderHandlerAbi provider;
  provider.handler = "Run";
  provider.body_symbol = "__moss_body_Worker_Run";
  provider.opaque_failure_frame_type = "__MossFailureFrame_Worker_Run";
  provider.outcome = {
      "__MossBodyOutcome_Worker_Run", "Int",
      {{full, 0, {}}, {invalid, 1, {}}},
  };
  provider.failure_arms = {arm};
  provider.reply = {"Int", false, true};
  provider.raising_nodes = {7};
  edge.consumers = kAllExceptionalCfgConsumers;
  provider.exceptional_edges = {edge};
  assert(provider_handler_abi_valid(provider));

  // h writes x and y. Before h_fail, their complete signature vectors are
  // identical and may form one exclusive class. h_fail writes only x, so the
  // new pseudo-handler column splits them while h's own per-leaf requirements
  // remain exactly WRITE/WRITE.
  using Signature = std::vector<std::string>;
  std::map<std::string, Signature> before = {
      {"x", {"WRITE"}}, {"y", {"WRITE"}},
  };
  std::map<std::string, Signature> after = {
      {"x", {"WRITE", "WRITE"}}, {"y", {"WRITE", "NONE"}},
  };
  assert(before["x"] == before["y"]);
  assert(after["x"] != after["y"]);
  assert(after["x"][0] == "WRITE" && after["y"][0] == "WRITE");

  TaggedOutcomeAbi outcome;
  outcome.rust_type = "__MossOutcome_Worker_Run";
  outcome.normal_type = "Int";
  OwnedPayloadFieldAbi code;
  code.name = "code";
  code.moss_type = "Int";
  code.kind = OwnedPayloadFieldAbi::Kind::Scalar;
  outcome.raised = {{full, 0, {code}}, {invalid, 1, {}}};
  assert(outcome.raised[0].identity.type != outcome.raised[1].identity.type);
  assert(outcome.raised[0].stable_tag != outcome.raised[1].stable_tag);
  assert(tagged_outcome_abi_valid(outcome));
  assert(tagged_outcome_matches_raise_set(outcome, {full, invalid}));
  outcome.raised[0].payload[0].moss_type = "FileIO";
  assert(!tagged_outcome_abi_valid(outcome));
  outcome.raised[0].payload[0].moss_type = "string";
  assert(!tagged_outcome_abi_valid(outcome));
  outcome.raised[0].payload[0].moss_type = "Vector[Int]";
  assert(!tagged_outcome_abi_valid(outcome));

  TaggedOutcomeAbi qualified_outcome{
      "__MossOutcome_Qualified", "Int", {{qualified, 0, {}}}};
  assert(tagged_outcome_abi_valid(qualified_outcome));

  provider.reply.every_normal_arm_replies = false;
  assert(!provider_handler_abi_valid(provider));

  return 0;
}
