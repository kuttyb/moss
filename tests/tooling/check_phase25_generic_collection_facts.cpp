// Verify checker-owned open facts for the bounded generic index_of workload.
#define main moss_cli_entry_for_generic_collection_facts_test
#include "../../src/moss.cpp"
#undef main

#include <cassert>
#include <sstream>

using namespace moss;

static Program check_source(const string& source) {
  std::istringstream input(source);
  Program program = Parser(lex_lines(input, "<generic-collection-facts>")).parse();
  Checker checker(program);
  checker.run();
  return program;
}

static bool rejects_with(const string& source, const string& code) {
  try {
    (void)check_source(source);
  } catch (const CompileError& error) {
    return error.code == code;
  }
  return false;
}

static checked_ir::Expr param(TypeRef type, uint32_t ordinal) {
  checked_ir::Expr value;
  value.kind = checked_ir::ExprKind::Parameter;
  value.type = std::move(type);
  value.ordinal = ordinal;
  return value;
}

static checked_ir::Expr local(TypeRef type, uint32_t ordinal) {
  checked_ir::Expr value;
  value.kind = checked_ir::ExprKind::LocalRead;
  value.type = std::move(type);
  value.ordinal = ordinal;
  return value;
}

static checked_ir::Expr integer(TypeRef type, const char* literal) {
  checked_ir::Expr value;
  value.kind = checked_ir::ExprKind::IntegerLiteral;
  value.type = std::move(type);
  value.literal_value = literal;
  return value;
}

static checked_ir::Expr binary(TypeRef type, checked_ir::BinaryOp op,
                               checked_ir::Expr left,
                               checked_ir::Expr right) {
  checked_ir::Expr value;
  value.kind = checked_ir::ExprKind::Binary;
  value.type = std::move(type);
  value.binary_op = op;
  value.operands = {std::move(left), std::move(right)};
  return value;
}

static vector<checked_ir::Stmt> expected_index_of_body(
    const OpenCallableSummaryFacts& summary) {
  const auto int_type = Type::primitive("int");
  const auto bool_type = Type::primitive("bool");
  const auto element = summary.parameter_types[1];
  const auto vector_type = summary.parameter_types[0];
  checked_ir::Expr count;
  count.kind = checked_ir::ExprKind::CollectionCount;
  count.type = int_type;
  count.semantic_target_id = string(checked_ir::kCollectionCountOperationId);
  count.operands.push_back(param(vector_type, 0));
  checked_ir::Stmt local_i;
  local_i.kind = checked_ir::StmtKind::Var;
  local_i.local_ordinal = 0;
  local_i.type = int_type;
  local_i.expr = integer(int_type, "0");
  checked_ir::Stmt local_n;
  local_n.kind = checked_ir::StmtKind::Var;
  local_n.local_ordinal = 1;
  local_n.type = int_type;
  local_n.expr = std::move(count);
  checked_ir::Expr indexed;
  indexed.kind = checked_ir::ExprKind::Index;
  indexed.type = element;
  indexed.semantic_target_id = string(checked_ir::kCollectionIndexOperationId);
  indexed.operands = {param(vector_type, 0), local(int_type, 0)};
  checked_ir::Stmt early_return;
  early_return.kind = checked_ir::StmtKind::Return;
  early_return.expr = local(int_type, 0);
  checked_ir::Stmt found;
  found.kind = checked_ir::StmtKind::If;
  found.expr = binary(bool_type, checked_ir::BinaryOp::Equal,
                      std::move(indexed), param(element, 1));
  found.then_branch.push_back(std::move(early_return));
  checked_ir::Stmt increment;
  increment.kind = checked_ir::StmtKind::Assign;
  increment.local_ordinal = 0;
  increment.expr = binary(int_type, checked_ir::BinaryOp::Add,
                          local(int_type, 0), integer(int_type, "1"));
  checked_ir::Stmt loop;
  loop.kind = checked_ir::StmtKind::While;
  loop.expr = binary(bool_type, checked_ir::BinaryOp::Less,
                     local(int_type, 0), local(int_type, 1));
  loop.body = {std::move(found), std::move(increment)};
  checked_ir::Stmt terminal;
  terminal.kind = checked_ir::StmtKind::Return;
  terminal.expr = binary(int_type, checked_ir::BinaryOp::Sub,
                         integer(int_type, "0"), integer(int_type, "1"));
  return {std::move(local_i), std::move(local_n), std::move(loop),
          std::move(terminal)};
}

int main() {
  // The provider's open contract must close without any consumer call sites.
  const auto provider_only = check_source(R"moss(export fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1
)moss");
  const auto provider_function = std::find_if(provider_only.functions.begin(),
      provider_only.functions.end(),
      [](const Function& candidate) { return candidate.name == "index_of"; });
  assert(provider_function != provider_only.functions.end());
  assert(provider_function->open_summary &&
         provider_function->open_summary->complete);
  assert(provider_function->checked_body_ir_v1_bytes &&
         provider_function->checked_body_ir_v1_hash);
  const auto expected_provider_body = expected_index_of_body(
      *provider_function->open_summary);
  assert(*provider_function->checked_body_ir_v1_bytes ==
         checked_ir::encode_checked_body_v1(expected_provider_body));
  assert(*provider_function->checked_body_ir_v1_hash ==
         checked_ir::compute_checked_body_hash_v1(expected_provider_body));
  const auto decoded_provider_body =
      checked_ir::decode_collection_loop_checked_body_v1(
          *provider_function->checked_body_ir_v1_bytes, *provider_only.type_context,
          provider_function->open_summary->parameter_types,
          provider_function->open_summary->result_type,
          *provider_function->checked_body_ir_v1_hash);
  assert(checked_ir::encode_checked_body_v1(decoded_provider_body) ==
         *provider_function->checked_body_ir_v1_bytes);
  assert(decoded_provider_body[0].kind == checked_ir::StmtKind::Var &&
         decoded_provider_body[0].local_ordinal == 0 &&
         decoded_provider_body[1].kind == checked_ir::StmtKind::Var &&
         decoded_provider_body[1].local_ordinal == 1 &&
         decoded_provider_body[2].kind == checked_ir::StmtKind::While &&
         decoded_provider_body[2].body[0].kind == checked_ir::StmtKind::If &&
         decoded_provider_body[2].body[0].then_branch[0].kind ==
             checked_ir::StmtKind::Return &&
         decoded_provider_body[2].body[1].kind == checked_ir::StmtKind::Assign &&
         decoded_provider_body[3].kind == checked_ir::StmtKind::Return);
  const auto provider_again = check_source(R"moss(export fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1
)moss");
  const auto provider_again_function = std::find_if(provider_again.functions.begin(),
      provider_again.functions.end(), [](const Function& candidate) {
    return candidate.name == "index_of";
  });
  assert(provider_again_function != provider_again.functions.end() &&
         provider_again_function->checked_body_ir_v1_bytes ==
             provider_function->checked_body_ir_v1_bytes &&
         provider_again_function->checked_body_ir_v1_hash ==
             provider_function->checked_body_ir_v1_hash);
  const auto typed_projection = project_typed_semantics_v1(provider_only);
  const auto projected_provider = typed_projection.open_summaries.find(
      provider_function->semantic_callable_id);
  assert(projected_provider != typed_projection.open_summaries.end());
  assert(projected_provider->second.checked_body_ir_available &&
         projected_provider->second.checked_body_ir_v1_hash ==
             provider_function->checked_body_ir_v1_hash);

  const auto program = check_source(R"moss(export fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1

fn main():
  index_of([1, 2], 2)
  index_of(["oak", "moss"], "moss")
)moss");
  auto function_it = std::find_if(program.functions.begin(), program.functions.end(),
      [](const Function& candidate) { return candidate.name == "index_of"; });
  assert(function_it != program.functions.end());
  const auto& function = *function_it;
  assert(function.open_summary && function.open_summary->complete);
  const auto& summary = *function.open_summary;
  assert(summary.bound_variable_count == 1);
  assert(summary.parameter_types.size() == 2);
  const auto& vector_type = summary.parameter_types[0];
  const auto& element_type = summary.parameter_types[1];
  assert(vector_type && vector_type->kind == TypeKind::Builtin &&
         vector_type->name == "vector" && vector_type->type_args.size() == 1);
  const auto& generic = vector_type->type_args.front();
  assert(generic && generic->kind == TypeKind::InferenceVar &&
         generic->var_id.session_id == 0 && generic->var_id.owner_id == 0 &&
         generic->var_id.ordinal == 0);
  assert(element_type && element_type->structurally_equal(*generic));
  assert(summary.result_type->structurally_equal(*Type::primitive("int")));
  assert((summary.parameter_access ==
         vector<AccessMode>{AccessMode::Read, AccessMode::Read}));
  assert(summary.observable_effects.may_panic);
  assert(summary.requirements.size() == 2);
  assert(std::none_of(summary.requirements.begin(), summary.requirements.end(),
      [](const Requirement& requirement) {
    return requirement.kind == RequirementKind::Iterate &&
        requirement.target_id == "functional source";
  }));

  assert(summary.operations.size() == 2);
  const auto find_operation = [&](const string& id) ->
      const OpenCallableSummaryFacts::OperationFact* {
    auto found = std::find_if(summary.operations.begin(), summary.operations.end(),
        [&](const auto& operation) { return operation.operation_id == id; });
    return found == summary.operations.end() ? nullptr : &*found;
  };
  const auto* count = find_operation("builtin:collection-count-v1");
  const auto* index = find_operation("builtin:index");
  assert(count && index);
  assert(count->receiver->structurally_equal(*vector_type));
  assert(count->arguments.empty());
  assert(count->result->structurally_equal(*Type::primitive("int")));
  assert(index->receiver->structurally_equal(*vector_type));
  assert(index->arguments.size() == 1 &&
         index->arguments.front()->structurally_equal(*Type::primitive("int")));
  assert(index->result->structurally_equal(*generic));

  const auto find_requirement = [&](RequirementKind kind, const string& target)
      -> const Requirement* {
    auto found = std::find_if(summary.requirements.begin(), summary.requirements.end(),
        [&](const Requirement& requirement) {
          return requirement.kind == kind && requirement.target_id == target;
        });
    return found == summary.requirements.end() ? nullptr : &*found;
  };
  const auto* indexing = find_requirement(RequirementKind::Index, "builtin:index");
  assert(indexing && indexing->subject->structurally_equal(*vector_type));
  assert(indexing->arguments.size() == 1 &&
         indexing->arguments.front()->structurally_equal(*Type::primitive("int")));
  assert(indexing->result && (*indexing->result)->structurally_equal(*generic));
  const auto* equality = find_requirement(RequirementKind::Operator, "operator:==");
  assert(equality && equality->subject->structurally_equal(*generic));
  assert(equality->arguments.size() == 1 &&
         equality->arguments.front()->structurally_equal(*generic));
  assert(equality->result && (*equality->result)->structurally_equal(
      *Type::primitive("bool")));
  assert(equality->access == AccessMode::Read &&
         equality->argument_access == vector<AccessMode>{AccessMode::Read});

  // Equality outside an indexed collection keeps the existing Int/Float
  // numeric-mixing rule; the new same-T relation is specific to index_of.
  (void)check_source(R"moss(fn equal(left, right) -> Bool:
  return left == right

fn main():
  equal(1, 1.0)
)moss");

  assert(rejects_with(R"moss(type Box:
  value: Int

fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1

fn main():
  index_of([Box(value: 1)], Box(value: 1))
)moss", "RECORD_EQUALITY_UNSUPPORTED"));

  assert(rejects_with(R"moss(fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1

fn main():
  index_of([1, 2], "wrong-type")
)moss", "TYPE_MISMATCH"));

  const auto extra_generic_operation = check_source(R"moss(export fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  unused = target + target
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1
)moss");
  const auto extra_operation_function = std::find_if(
      extra_generic_operation.functions.begin(), extra_generic_operation.functions.end(),
      [](const Function& candidate) { return candidate.name == "index_of"; });
  assert(extra_operation_function != extra_generic_operation.functions.end());
  assert(!extra_operation_function->open_summary ||
         !extra_operation_function->open_summary->complete);
  assert(!extra_operation_function->checked_body_ir_v1_bytes);

  const auto duplicate_count = check_source(R"moss(export fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  again = items |> count
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1
)moss");
  const auto duplicate_count_function = std::find_if(
      duplicate_count.functions.begin(), duplicate_count.functions.end(),
      [](const Function& candidate) { return candidate.name == "index_of"; });
  assert(duplicate_count_function != duplicate_count.functions.end());
  assert(!duplicate_count_function->open_summary ||
         !duplicate_count_function->open_summary->complete);
  assert(!duplicate_count_function->checked_body_ir_v1_bytes);

  const auto duplicate_index = check_source(R"moss(export fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  if items[0] == target:
    return 0
  return 0 - 1
)moss");
  const auto duplicate_index_function = std::find_if(
      duplicate_index.functions.begin(), duplicate_index.functions.end(),
      [](const Function& candidate) { return candidate.name == "index_of"; });
  assert(duplicate_index_function != duplicate_index.functions.end());
  assert(!duplicate_index_function->open_summary ||
         !duplicate_index_function->open_summary->complete);
  assert(!duplicate_index_function->checked_body_ir_v1_bytes);

  const auto unrelated_equality = check_source(R"moss(export fn index_of(items, target) -> Int:
  var i = 0
  n = items |> count
  if target == target:
    return 0
  while i < n:
    if items[i] == target:
      return i
    i = i + 1
  return 0 - 1
)moss");
  const auto unrelated_equality_function = std::find_if(
      unrelated_equality.functions.begin(), unrelated_equality.functions.end(),
      [](const Function& candidate) { return candidate.name == "index_of"; });
  assert(unrelated_equality_function != unrelated_equality.functions.end());
  assert(!unrelated_equality_function->open_summary ||
         !unrelated_equality_function->open_summary->complete);
  assert(!unrelated_equality_function->checked_body_ir_v1_bytes);

  const auto d4_provider = check_source(R"moss(export fn contains(items, target) -> Bool:
  var i = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      return true
    i = i + 1
  return false

export fn count_equal(items, target) -> Int:
  var i = 0
  var matches = 0
  n = items |> count
  while i < n:
    if items[i] == target:
      matches = matches + 1
    i = i + 1
  return matches
)moss");
  for (const auto& [name, expected_locals] :
       vector<std::pair<string, size_t>>{{"contains", 2}, {"count_equal", 3}}) {
    const auto found = std::find_if(d4_provider.functions.begin(),
        d4_provider.functions.end(), [&](const Function& candidate) {
      return candidate.name == name;
    });
    assert(found != d4_provider.functions.end());
    assert(found->open_summary && found->open_summary->complete);
    assert(found->open_summary->operations.size() == 2 &&
           found->open_summary->requirements.size() == 2 &&
           found->open_summary->observable_effects.may_panic);
    assert(found->checked_body_ir_v1_bytes && found->checked_body_ir_v1_hash);
    const auto decoded = checked_ir::decode_collection_loop_checked_body_v1(
        *found->checked_body_ir_v1_bytes, *d4_provider.type_context,
        found->open_summary->parameter_types, found->open_summary->result_type,
        *found->checked_body_ir_v1_hash);
    assert(decoded.size() == expected_locals + 2);
    for (size_t ordinal = 0; ordinal < expected_locals; ++ordinal)
      assert(decoded[ordinal].kind == checked_ir::StmtKind::Var &&
             decoded[ordinal].local_ordinal == ordinal);
    assert(decoded[expected_locals].kind == checked_ir::StmtKind::While);
    assert(decoded.back().kind == checked_ir::StmtKind::Return);
  }

  const auto generic_map = check_source(R"moss(module Service

export fn fresh(key, value):
  var values = Map()
  values[key] = value
  return values[key]
)moss");
  const auto map_function = std::find_if(generic_map.functions.begin(),
      generic_map.functions.end(), [](const Function& candidate) {
    return candidate.name == "Service__fresh" || candidate.name == "fresh";
  });
  assert(map_function != generic_map.functions.end());
  assert(map_function->open_summary && map_function->open_summary->complete &&
         map_function->open_summary->occurrence_ledger_complete &&
         map_function->open_summary->observable_effects.may_panic &&
         map_function->checked_body_ir_v1_bytes &&
         map_function->checked_body_ir_v1_hash);
  const auto map_body = checked_ir::decode_checked_body_v1(
      *map_function->checked_body_ir_v1_bytes, *generic_map.type_context,
      map_function->open_summary->parameter_types,
      map_function->open_summary->result_type,
      *map_function->checked_body_ir_v1_hash);
  assert(map_body.size() == 3 &&
         map_body[0].kind == checked_ir::StmtKind::Var &&
         map_body[0].expr &&
         map_body[0].expr->kind == checked_ir::ExprKind::MapLiteral &&
         map_body[1].kind == checked_ir::StmtKind::AssignIndex &&
         map_body[1].operands.size() == 2 && map_body[1].expr &&
         map_body[2].kind == checked_ir::StmtKind::Return &&
         map_body[2].expr &&
         map_body[2].expr->kind == checked_ir::ExprKind::Index);
  assert(abi_v10_local_map_effects_covered(
      map_function->open_summary->observable_effects, map_body));

  std::cout << "PASS index_of/contains/count_equal checker summaries, Count/Index "
               "TypeRefs, equality admissibility, READ accesses, may_panic, "
               "provider-only summaries, fail-closed operation cases, and "
               "generic Map local construction/mutation/read\n";
}
