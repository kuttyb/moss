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

  const ResolvedTypeAbi int_type{ResolvedTypeKind::Scalar, "Int", {}};
  const ResolvedTypeAbi string_type{
      ResolvedTypeKind::OwnedUnbounded, "String", {}};
  const ResolvedTypeAbi file_type{
      ResolvedTypeKind::Capability, "FileIO", {}};
  const ResolvedTypeAbi borrow_type{
      ResolvedTypeKind::Borrowed, "&ProviderPrivate", {}};
  const ResolvedTypeAbi private_type{
      ResolvedTypeKind::Aggregate, "ProviderPrivate", {int_type, string_type}};
  const ResolvedTypeAbi private_with_file{
      ResolvedTypeKind::Aggregate, "BadPrivate", {int_type, file_type}};

  assert(failure_capture_allowed({
      "private_value", "ProviderPrivate", "private_value", true, true,
      private_type}));
  assert(failure_capture_allowed(
      {"text", "String", "text", true, true, string_type}));
  assert(!failure_capture_allowed(
      {"file", "FileIO", "file", true, true, file_type}));
  assert(!failure_capture_allowed({
      "nested_file", "BadPrivate", "nested_file", true, true,
      private_with_file}));
  assert(!failure_capture_allowed(
      {"borrow", "&ProviderPrivate", "borrow", true, true, borrow_type}));
  assert(!failure_capture_allowed(
      {"late", "Int", "late", true, false, int_type}));

  RaisingNodeAbi raising{
      7, {full, io}, {5}, false, 0, 0};
  ExceptionalDestinationAbi recover{
      11, ExceptionalDestinationKind::RecoveryArm, 5, 0, {full}};
  ExceptionalDestinationAbi exit{
      12, ExceptionalDestinationKind::HandlerExit, 0, 0, {io}};
  ExceptionalCfgEdgeAbi recover_edge{
      7, 11, ExceptionalEdgeKind::RaiseToRecover, {full},
      kAllExceptionalCfgConsumers};
  ExceptionalCfgEdgeAbi exit_edge{
      7, 12, ExceptionalEdgeKind::RaiseToHandlerExit, {io},
      kAllExceptionalCfgConsumers};
  assert(verify_exceptional_cfg(
      {raising}, {recover, exit}, {recover_edge, exit_edge}).ok);
  RaisingNodeAbi lexical_raising{
      9, {full}, {5, 3}, false, 0, 0};
  ExceptionalDestinationAbi outer_recover{
      14, ExceptionalDestinationKind::RecoveryArm, 3, 0, {full}};
  ExceptionalDestinationAbi lexical_exit{
      15, ExceptionalDestinationKind::HandlerExit, 0, 0, {full}};
  ExceptionalCfgEdgeAbi skipped_inner{
      9, 14, ExceptionalEdgeKind::RaiseToRecover, {full},
      kAllExceptionalCfgConsumers};
  ExceptionalCfgEdgeAbi bypassed_to_exit{
      9, 15, ExceptionalEdgeKind::RaiseToHandlerExit, {full},
      kAllExceptionalCfgConsumers};
  auto inner_edge = skipped_inner;
  inner_edge.destination_node = 11;
  assert(verify_exceptional_cfg(
      {lexical_raising}, {recover, outer_recover, lexical_exit},
      {inner_edge}).ok);
  assert(!verify_exceptional_cfg(
      {lexical_raising}, {recover, outer_recover, lexical_exit},
      {skipped_inner}).ok);
  assert(!verify_exceptional_cfg(
      {lexical_raising}, {recover, outer_recover, lexical_exit},
      {bypassed_to_exit}).ok);
  auto duplicate_match_order = outer_recover;
  duplicate_match_order.node = 16;
  duplicate_match_order.lexical_scope = recover.lexical_scope;
  duplicate_match_order.match_order = recover.match_order;
  assert(!verify_exceptional_cfg(
      {raising}, {recover, duplicate_match_order, exit},
      {recover_edge, exit_edge}).ok);
  auto duplicate_scope_chain = lexical_raising;
  duplicate_scope_chain.recovery_scope_chain = {5, 5, 3};
  assert(!verify_exceptional_cfg(
      {duplicate_scope_chain}, {recover, outer_recover, lexical_exit},
      {inner_edge}).ok);
  assert(!verify_exceptional_cfg(
      {raising}, {recover, exit}, {recover_edge}).ok);
  auto impossible = exit_edge;
  impossible.alternatives = {invalid};
  assert(!verify_exceptional_cfg(
      {raising}, {recover, exit}, {recover_edge, impossible}).ok);
  assert(!verify_exceptional_cfg(
      {raising}, {recover, exit}, {recover_edge, exit_edge, exit_edge}).ok);
  auto bad_destination = exit_edge;
  bad_destination.destination_node = 99;
  assert(!verify_exceptional_cfg(
      {raising}, {recover, exit}, {recover_edge, bad_destination}).ok);
  auto contradictory = exit_edge;
  contradictory.kind = ExceptionalEdgeKind::RaiseToRecover;
  assert(!verify_exceptional_cfg(
      {raising}, {recover, exit}, {recover_edge, contradictory}).ok);
  auto missing_consumer = exit_edge;
  missing_consumer.consumers &= ~exceptional_consumer_bit(
      ExceptionalCfgConsumer::GuardCancellation);
  assert(!verify_exceptional_cfg(
      {raising}, {recover, exit}, {recover_edge, missing_consumer}).ok);
  RaisingNodeAbi reraising{8, {full}, {}, true, 9, 5};
  ExceptionalDestinationAbi outer{
      13, ExceptionalDestinationKind::OuterRecoveryOrExit, 5, 0, {full}};
  ExceptionalCfgEdgeAbi reraised{
      8, 13, ExceptionalEdgeKind::ReRaiseToOuter, {full},
      kAllExceptionalCfgConsumers};
  assert(verify_exceptional_cfg({reraising}, {outer}, {reraised}).ok);
  outer.lexical_scope = 9;
  assert(!verify_exceptional_cfg({reraising}, {outer}, {reraised}).ok);

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
      {"job_id", "ProviderPrivate", "job_id", true, true, private_type},
  };
  arm.state_writes = {"x"};

  const auto projection = [](const std::string& suffix) {
    return RaisedProjectionAbi{
        "__MossPayload_" + suffix,
        "__MossResidual_" + suffix,
        "__MossProjection_" + suffix,
        "__moss_project_" + suffix,
        "__moss_rebuild_" + suffix};
  };

  ProviderHandlerAbi provider;
  provider.handler = "Run";
  provider.body_symbol = "__moss_body_Worker_Run";
  provider.opaque_failure_frame_type = "__MossFailureFrame_Worker_Run";
  provider.raised_carrier_rust_type = "__MossRaisedCarrier_Worker_Run";
  provider.outcome = {
      "__MossBodyOutcome_Worker_Run", "Int",
      {{full, 0, {}, projection("full")},
       {io, 1, {}, projection("io")}},
  };
  provider.imported_raise_bridges = {{
      0, "provider::Store.Load", "provider::__MossOutcome_Store_Load",
      "provider::__MossFailureFrame_Store_Load", "FromStoreLoad",
      "__moss_bridge_Worker_Run_from_Store_Load", {{0, 1, io}}}};
  provider.owned_type_shapes = {int_type, private_type, string_type};
  provider.failure_arms = {arm};
  provider.reply = {"Int", false, true};
  RaisingNodeAbi handler_raising{
      17, {full, io, invalid}, {15}, false, 0, 0};
  ExceptionalDestinationAbi local_recover{
      21, ExceptionalDestinationKind::RecoveryArm, 15, 0, {invalid}};
  ExceptionalDestinationAbi handler_exit{
      22, ExceptionalDestinationKind::HandlerExit, 0, 0, {full, io}};
  ExceptionalCfgEdgeAbi local_edge{
      17, 21, ExceptionalEdgeKind::RaiseToRecover, {invalid},
      kAllExceptionalCfgConsumers};
  ExceptionalCfgEdgeAbi handler_exit_edge{
      17, 22, ExceptionalEdgeKind::RaiseToHandlerExit, {full, io},
      kAllExceptionalCfgConsumers};
  provider.raising_nodes = {handler_raising};
  provider.exceptional_destinations = {local_recover, handler_exit};
  provider.exceptional_edges = {local_edge, handler_exit_edge};
  assert(provider_handler_abi_valid(provider));
  TaggedOutcomeAbi callee_outcome{
      "provider::__MossOutcome_Store_Load", "Int",
      {{io, 0, {}, projection("callee_io")}}};
  const auto& bridge = provider.imported_raise_bridges.front();
  assert(imported_raise_bridge_matches_callee(
      bridge, provider.outcome, "provider::Store.Load",
      "provider::__MossFailureFrame_Store_Load", callee_outcome));
  auto wrong_callee = callee_outcome;
  wrong_callee.raised[0].identity = full;
  assert(!imported_raise_bridge_matches_callee(
      bridge, provider.outcome, "provider::Store.Load",
      "provider::__MossFailureFrame_Store_Load", wrong_callee));
  OwnedPayloadFieldAbi wrong_payload;
  wrong_payload.name = "code";
  wrong_payload.moss_type = "Int";
  wrong_payload.resolved_type = int_type;
  wrong_callee = callee_outcome;
  wrong_callee.raised[0].payload = {wrong_payload};
  assert(!imported_raise_bridge_matches_callee(
      bridge, provider.outcome, "provider::Store.Load",
      "provider::__MossFailureFrame_Store_Load", wrong_callee));
  assert(!imported_raise_bridge_matches_callee(
      bridge, provider.outcome, "provider::Store.Other",
      "provider::__MossFailureFrame_Store_Load", callee_outcome));
  assert(!verify_root_totality(provider, {full, io}).ok);
  FailureArmAbi catch_all;
  catch_all.source_order = 1;
  catch_all.pattern.kind = FailurePatternKind::CatchAll;
  catch_all.callable_symbol = "__moss_on_fail_Worker_Run_1";
  provider.failure_arms.push_back(catch_all);
  assert(provider_handler_abi_valid(provider));
  assert(verify_root_totality(provider, {full, io}).ok);
  assert(!verify_root_totality(provider, {}).ok);
  assert(!verify_root_totality(provider, {full}).ok);
  assert(!verify_root_totality(provider, {full, io, invalid}).ok);

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
  code.resolved_type = int_type;
  outcome.raised = {
      {full, 0, {code}, projection("outcome_full")},
      {invalid, 1, {}, projection("outcome_invalid")}};
  assert(outcome.raised[0].identity.type != outcome.raised[1].identity.type);
  assert(outcome.raised[0].stable_tag != outcome.raised[1].stable_tag);
  assert(tagged_outcome_abi_valid(outcome));
  assert(tagged_outcome_matches_raise_set(outcome, {full, invalid}));
  outcome.raised[0].payload[0].resolved_type = file_type;
  assert(!tagged_outcome_abi_valid(outcome));
  outcome.raised[0].payload[0].moss_type = "NestedError";
  outcome.raised[0].payload[0].kind = OwnedPayloadFieldAbi::Kind::ErrorEnum;
  outcome.raised[0].payload[0].resolved_type = {
      ResolvedTypeKind::ErrorEnum, "NestedError", {int_type}};
  assert(tagged_outcome_abi_valid(outcome));
  outcome.raised[0].payload[0].resolved_type = {
      ResolvedTypeKind::ErrorEnum, "NestedError", {file_type}};
  assert(!tagged_outcome_abi_valid(outcome));
  outcome.raised[0].payload[0].moss_type = "String";
  outcome.raised[0].payload[0].kind = OwnedPayloadFieldAbi::Kind::Scalar;
  outcome.raised[0].payload[0].resolved_type = string_type;
  assert(!tagged_outcome_abi_valid(outcome));

  TaggedOutcomeAbi qualified_outcome{
      "__MossOutcome_Qualified", "Int",
      {{qualified, 0, {}, projection("qualified")}}};
  assert(tagged_outcome_abi_valid(qualified_outcome));

  provider.reply.every_normal_arm_replies = false;
  assert(!provider_handler_abi_valid(provider));

  return 0;
}
