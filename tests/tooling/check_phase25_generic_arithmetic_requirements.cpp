// Inspect the checker's retained records directly, including facts intentionally
// omitted from the public projection while the open summary remains incomplete.
#define main moss_cli_entry_for_generic_arithmetic_requirements_test
#include "../../src/moss.cpp"
#undef main

#include <cassert>
#include <sstream>

using namespace moss;

static Program checked_program(const string& source) {
  std::istringstream input(source);
  Program program = Parser(lex_lines(input, "<generic-arithmetic-test>")).parse();
  Checker checker(program);
  checker.run();
  return program;
}

static const Function& find_function(const Program& program,
                                     const char* name) {
  auto found = std::find_if(program.functions.begin(), program.functions.end(),
      [&](const Function& function) { return function.name == name; });
  assert(found != program.functions.end());
  return *found;
}

int main() {
  auto program = checked_program(R"moss(export fn compute(x):
  return x * 2 + 20

export fn identity(value):
  return value

fn main():
  echo compute(5)
  echo identity(7)
)moss");

  const Function& compute = find_function(program, "compute");
  assert(compute.open_summary);
  const auto& summary = *compute.open_summary;
  assert(!summary.complete);
  assert(summary.parameter_types.size() == 1);
  const TypeRef generic = summary.parameter_types.front();
  assert(generic && generic->kind == TypeKind::InferenceVar);
  assert(generic->var_id.session_id == 0 && generic->var_id.owner_id == 0);
  if (summary.requirements.size() != 2) {
    std::cerr << "result=" << compute.return_type.value_or("<none>")
              << " body=" << compute.body.size()
              << " returned=" << compute.body.front().a
              << " requirements=" << summary.requirements.size() << "\n";
  }
  assert(summary.requirements.size() == 2);

  const auto& multiply = summary.requirements[0];
  const auto& add = summary.requirements[1];
  assert(multiply.kind == RequirementKind::Operator);
  assert(multiply.target_id == "operator:*");
  assert(add.kind == RequirementKind::Operator);
  assert(add.target_id == "operator:+");
  for (const auto* requirement : {&multiply, &add}) {
    assert(requirement->subject == generic);
    assert(requirement->arguments.size() == 1);
    assert(requirement->arguments.front()->structurally_equal(
        *Type::primitive("int")));
    assert(requirement->result && *requirement->result == generic);
    assert(requirement->argument_access == vector<AccessMode>{AccessMode::Read});
    assert(requirement->origin.module_id == summary.origin.module_id);
    assert(requirement->origin.enclosing_callable == compute.semantic_callable_id);
    assert(requirement->origin.source_file == compute.source_file);
    assert(requirement->origin.line == compute.body.front().line);
    assert(requirement->origin.column > 0);
    assert(requirement->origin.call_expression_id == requirement->id);
  }
  assert(multiply.id != add.id);
  assert(!multiply.parent_requirement_id);
  assert(add.parent_requirement_id == multiply.id);

  const Function& identity = find_function(program, "identity");
  assert(identity.open_summary);
  assert(identity.open_summary->requirements.empty());

  std::cout << "PASS checker retained ordered typed operator requirements for * and +; "
               "summary remains incomplete and identity has no requirements\n";
}
