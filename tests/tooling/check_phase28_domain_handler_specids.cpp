#define main moss_cli_entry_for_domain_handler_specids_test
#include "../../src/moss.cpp"
#undef main

#include <cassert>
#include <sstream>

using namespace moss;

static Program check_source(const string& source) {
  std::istringstream input(source);
  Program program = moss::parse_source(input, "<phase28-domain-handler-specids>");
  (void)moss::check_program(program);
  return program;
}

int main() {
  const string source = R"MOSS(domain Echoer:
  value
  fn Set(x):
    value = copy(x)

fn main():
  int_echoer = Echoer()
  string_echoer = Echoer()
  message int_echoer.Set(3)
  message string_echoer.Set("word")
)MOSS";
  Program program = check_source(source);
  const Domain* domain = nullptr;
  for (const auto& candidate : program.domains)
    if (candidate.name == "Echoer") domain = &candidate;
  assert(domain && domain->handlers.size() == 1);
  const auto int_instance = std::find_if(
      program.domain_specializations.begin(),
      program.domain_specializations.end(), [](const auto& instance) {
    return instance.instance == "int_echoer";
  });
  const auto string_instance = std::find_if(
      program.domain_specializations.begin(),
      program.domain_specializations.end(), [](const auto& instance) {
    return instance.instance == "string_echoer";
  });
  assert(int_instance != program.domain_specializations.end());
  assert(string_instance != program.domain_specializations.end());
  assert(int_instance->handler_spec_ids.size() == 1);
  assert(string_instance->handler_spec_ids.size() == 1);
  const SpecId int_handler = int_instance->handler_spec_ids.front();
  const SpecId string_handler = string_instance->handler_spec_ids.front();
  assert(int_handler != InvalidSpecId && string_handler != InvalidSpecId);
  assert(int_handler != string_handler);

  const auto int_parameter = int_instance->handler_parameter_types.find("Set");
  const auto string_parameter = string_instance->handler_parameter_types.find("Set");
  assert(int_parameter != int_instance->handler_parameter_types.end());
  assert(string_parameter != string_instance->handler_parameter_types.end());
  assert(int_parameter->second.size() == 1 && int_parameter->second.front() == "int");
  assert(string_parameter->second.size() == 1 &&
         string_parameter->second.front() == "string");

  const Handler& handler = domain->handlers.front();
  assert(handler.name == "Set" && !handler.body.empty());
  const Stmt& assignment = handler.body.back();
  assert(assignment.kind == Stmt::Kind::Assign);
  const auto handle = std::find_if(
      assignment.expression_handles.begin(), assignment.expression_handles.end(),
      [](const est::Handle& candidate) {
    return candidate.slot == est::ExprSlot::RhsValue;
  });
  assert(handle != assignment.expression_handles.end());
  assert(handle->arena_index < program.expression_arenas.size());
  auto& arena = *program.expression_arenas[handle->arena_index];
  const auto* int_facts = arena.facts(handle->expr, int_handler);
  const auto* string_facts = arena.facts(handle->expr, string_handler);
  assert(int_facts && int_facts->type &&
         int_facts->type->to_canonical_string() == "int");
  assert(string_facts && string_facts->type &&
         string_facts->type->to_canonical_string() == "string");
  assert(!arena.facts(handle->expr, handler.spec_id) ||
         arena.facts(handle->expr, handler.spec_id)->type == nullptr);

  // Fault injection is keyed to one exact instance. Dropping or corrupting
  // that record leaves the sibling specialization intact.
  assert(arena.test_drop_occurrence_fact(handle->expr, int_handler));
  assert(!arena.facts(handle->expr, int_handler));
  assert(arena.facts(handle->expr, string_handler));
  assert(arena.test_mutate_occurrence_type(
      handle->expr, string_handler, Type::primitive("bool")));
  const auto* corrupted = arena.facts(handle->expr, string_handler);
  assert(corrupted && corrupted->type &&
         corrupted->type->to_canonical_string() == "bool");
  assert(!arena.facts(handle->expr, handler.spec_id) ||
         arena.facts(handle->expr, handler.spec_id)->type == nullptr);

  auto int_call = std::find_if(program.semantic_call_edges.begin(),
      program.semantic_call_edges.end(), [&](const SemanticCallEdge& edge) {
    return edge.invocation_kind == "synchronous_message" &&
           edge.callee_spec_id == int_handler &&
           edge.call_est_arena_index != UINT32_MAX &&
           edge.call_expression_occurrence_id != UINT32_MAX;
  });
  auto string_call = std::find_if(program.semantic_call_edges.begin(),
      program.semantic_call_edges.end(), [&](const SemanticCallEdge& edge) {
    return edge.invocation_kind == "synchronous_message" &&
           edge.callee_spec_id == string_handler &&
           edge.call_est_arena_index != UINT32_MAX &&
           edge.call_expression_occurrence_id != UINT32_MAX;
  });
  assert(int_call != program.semantic_call_edges.end());
  assert(string_call != program.semantic_call_edges.end());
  assert(int_call->target_callable_identity ==
         program.specialization_table[int_handler].semantic_callable_identity);
  assert(string_call->target_callable_identity ==
         program.specialization_table[string_handler].semantic_callable_identity);
  const SpecId caller = int_call->caller_spec_id;
  const auto call_arena = int_call->call_est_arena_index;
  const auto call_expression = int_call->call_expression_occurrence_id;
  const auto require_exact_int_call = [&]() {
    const auto exact = select_exact_semantic_call_edge(
        program, caller, call_arena, call_expression);
    assert(exact.kind == ExactSemanticCallEdgeLookup::Kind::Unique);
    assert(exact.edge && exact.edge->callee_spec_id == int_handler);
    assert(exact.edge->target_callable_identity ==
           program.specialization_table[int_handler].semantic_callable_identity);
  };
  require_exact_int_call();
  const SemanticCallEdge original_int_call = *int_call;
  program.semantic_call_edges.push_back(original_int_call);
  const auto duplicated = select_exact_semantic_call_edge(
      program, caller, call_arena, call_expression);
  assert(duplicated.kind == ExactSemanticCallEdgeLookup::Kind::Duplicate);
  assert(!duplicated.edge);
  program.semantic_call_edges.pop_back();
  require_exact_int_call();
  auto int_call_row = std::find_if(program.semantic_call_edges.begin(),
      program.semantic_call_edges.end(), [&](const SemanticCallEdge& edge) {
    return edge.caller_spec_id == caller &&
           edge.call_est_arena_index == call_arena &&
           edge.call_expression_occurrence_id == call_expression;
  });
  assert(int_call_row != program.semantic_call_edges.end());
  int_call_row->callee_spec_id = string_handler;
  const auto wrong_owner = select_exact_semantic_call_edge(
      program, caller, call_arena, call_expression);
  assert(wrong_owner.kind == ExactSemanticCallEdgeLookup::Kind::Unique);
  assert(wrong_owner.edge && wrong_owner.edge->callee_spec_id != int_handler);
  assert(wrong_owner.edge->target_callable_identity !=
         program.specialization_table[string_handler].semantic_callable_identity);
  int_call_row->callee_spec_id = int_handler;
  require_exact_int_call();
  program.semantic_call_edges.erase(int_call_row);
  const auto dropped = select_exact_semantic_call_edge(
      program, caller, call_arena, call_expression);
  assert(dropped.kind == ExactSemanticCallEdgeLookup::Kind::Missing);
  program.semantic_call_edges.push_back(original_int_call);

  const string callback_source = R"MOSS(fn twice(x: Int) -> Int:
  return x * 2

fn transform(xs, f):
  return xs |> map(f)

fn main():
  echo transform([1, 2, 3], twice) |> sum
)MOSS";
  Program callbacks = check_source(callback_source);
  const auto dynamic_stage = std::find_if(
      callbacks.semantic_call_edges.begin(),
      callbacks.semantic_call_edges.end(), [](const SemanticCallEdge& edge) {
    return edge.invocation_kind == "dynamic_callback";
  });
  assert(dynamic_stage != callbacks.semantic_call_edges.end());
  assert(dynamic_stage->target_kind == SemanticCallTargetKind::DynamicCallback);
  assert(dynamic_stage->caller_spec_id != InvalidSpecId);
  assert(dynamic_stage->call_est_arena_index != UINT32_MAX);
  assert(dynamic_stage->call_expression_occurrence_id != UINT32_MAX);
  assert(dynamic_stage->callback_argument_index == 0);
  assert(dynamic_stage->argument_callable_spec_ids.size() == 1);
  assert(dynamic_stage->argument_callable_spec_ids.front() == InvalidSpecId);
  const auto exact_dynamic = select_exact_semantic_call_edge(
      callbacks, dynamic_stage->caller_spec_id,
      dynamic_stage->call_est_arena_index,
      dynamic_stage->call_expression_occurrence_id);
  assert(exact_dynamic.kind == ExactSemanticCallEdgeLookup::Kind::Unique);

  const auto outer_transform = std::find_if(
      callbacks.semantic_call_edges.begin(),
      callbacks.semantic_call_edges.end(), [](const SemanticCallEdge& edge) {
    return edge.target == "fn:transform" &&
           edge.target_kind == SemanticCallTargetKind::Static;
  });
  assert(outer_transform != callbacks.semantic_call_edges.end());
  assert(outer_transform->argument_callable_spec_ids.size() == 2);
  assert(outer_transform->argument_callable_spec_ids[1] != InvalidSpecId);

  const string method_source = R"MOSS(type Holder:
  value: Int

  fn select(x: Int) -> Int:
    return x

  fn select(x: Float) -> Float:
    return x

fn main():
  holder = Holder(value = 0)
  echo holder.select(3)
  echo holder.select(2.5)
)MOSS";
  Program methods = check_source(method_source);
  const ObjectType* holder = nullptr;
  for (const auto& candidate : methods.objects)
    if (candidate.name == "Holder") holder = &candidate;
  assert(holder && holder->methods.size() == 2);
  const auto& int_method = holder->methods[0];
  const auto& float_method = holder->methods[1];
  assert(int_method.spec_id != InvalidSpecId);
  assert(float_method.spec_id != InvalidSpecId);
  assert(int_method.spec_id != float_method.spec_id);
  assert(int_method.specializations.size() == 1);
  assert(float_method.specializations.size() == 1);
  const SpecId int_body = int_method.specializations.front().spec_id;
  const SpecId float_body = float_method.specializations.front().spec_id;
  assert(int_body != InvalidSpecId && float_body != InvalidSpecId);
  assert(int_body != float_body);
  assert(int_body != int_method.spec_id && float_body != float_method.spec_id);
  auto int_edge = std::find_if(methods.semantic_call_edges.begin(),
      methods.semantic_call_edges.end(), [&](const SemanticCallEdge& edge) {
    return edge.callee_spec_id == int_body &&
        edge.call_est_arena_index != UINT32_MAX;
  });
  auto float_edge = std::find_if(methods.semantic_call_edges.begin(),
      methods.semantic_call_edges.end(), [&](const SemanticCallEdge& edge) {
    return edge.callee_spec_id == float_body &&
        edge.call_est_arena_index != UINT32_MAX;
  });
  assert(int_edge != methods.semantic_call_edges.end());
  assert(float_edge != methods.semantic_call_edges.end());
  assert(int_edge->caller_spec_id ==
         methods.moss_cli_entry_for_domain_handler_specids_test->spec_id);
  assert(float_edge->caller_spec_id ==
         methods.moss_cli_entry_for_domain_handler_specids_test->spec_id);
  assert(int_edge->target_callable_identity ==
         methods.specialization_table[int_body].semantic_callable_identity);
  assert(float_edge->target_callable_identity ==
         methods.specialization_table[float_body].semantic_callable_identity);
  assert(int_edge->callee_spec_id != int_method.spec_id);
  assert(float_edge->callee_spec_id != float_method.spec_id);

  // Missing, duplicated, or wrong-owner IDs never fall back to the method
  // declaration identity or to the sibling overload's concrete body.
  auto exact_int = select_exact_semantic_call_edge(
      methods, int_edge->caller_spec_id, int_edge->call_est_arena_index,
      int_edge->call_expression_occurrence_id);
  assert(exact_int.kind == ExactSemanticCallEdgeLookup::Kind::Unique);
  assert(exact_int.edge && exact_int.edge->callee_spec_id == int_body);
  const auto wrong_owner_count = std::count_if(
      int_method.specializations.begin(), int_method.specializations.end(),
      [&](const MethodSpecializationIdentity& specialization) {
    return specialization.spec_id == float_body;
  });
  assert(wrong_owner_count == 0);
  methods.semantic_call_edges.erase(std::remove_if(
      methods.semantic_call_edges.begin(), methods.semantic_call_edges.end(),
      [&](const SemanticCallEdge& edge) {
    return edge.caller_spec_id == int_edge->caller_spec_id &&
        edge.call_est_arena_index == int_edge->call_est_arena_index &&
        edge.call_expression_occurrence_id == int_edge->call_expression_occurrence_id;
  }), methods.semantic_call_edges.end());
  assert(select_exact_semantic_call_edge(
      methods, int_edge->caller_spec_id, int_edge->call_est_arena_index,
      int_edge->call_expression_occurrence_id).kind ==
      ExactSemanticCallEdgeLookup::Kind::Missing);
}
