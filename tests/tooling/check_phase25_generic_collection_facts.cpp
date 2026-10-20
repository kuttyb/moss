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

int main() {
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

  std::cout << "PASS index_of checker summary, Count/Index TypeRefs, equality "
               "admissibility, READ accesses, may_panic, and negative cases\n";
}
