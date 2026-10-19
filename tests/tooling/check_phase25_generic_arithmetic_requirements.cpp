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
  assert(summary.complete);
  assert(summary.parameter_types.size() == 1);
  const TypeRef generic = summary.parameter_types.front();
  assert(generic && generic->kind == TypeKind::InferenceVar);
  assert(generic->var_id.session_id == 0 && generic->var_id.owner_id == 0);
  assert(summary.result_type == generic);
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

  vector<abi_v10::SourceProvenance> requirement_provenance;
  auto converted = abi_v10_checked_requirements(
      summary.requirements, summary.bound_variable_count,
      requirement_provenance);
  assert(converted && converted->size() == 2);
  abi_v10::Summary wire_summary;
  wire_summary.callable_id = compute.semantic_callable_id;
  wire_summary.variable_count = summary.bound_variable_count;
  for (size_t index = 0; index < summary.parameter_types.size(); ++index) {
    auto type = abi_v10_checked_type(summary.parameter_types[index],
                                     summary.bound_variable_count);
    assert(type);
    wire_summary.parameters.push_back(*type);
    wire_summary.parameter_access.push_back(
        abi_v10_checked_access(summary.parameter_access[index]));
  }
  auto result = abi_v10_checked_type(summary.result_type,
                                     summary.bound_variable_count);
  assert(result);
  wire_summary.result = *result;
  wire_summary.requirements = *converted;
  wire_summary.body_semantic_hash = "body-semantic-v1:sha256:" +
      string(64, 'a');
  auto callable_provenance = abi_v10_checked_provenance(
      summary.origin.module_id, compute.semantic_callable_id,
      compute.source_file, compute.line);
  wire_summary.provenance_id = callable_provenance.provenance_id;

  assert(wire_summary.requirements[0].operation == "operator:*");
  assert(wire_summary.requirements[1].operation == "operator:+");
  for (size_t index = 0; index < wire_summary.requirements.size(); ++index) {
    const auto& requirement = wire_summary.requirements[index];
    assert(requirement.receiver.kind == abi_v10::WireTypeKind::BoundVar &&
           requirement.receiver.ordinal == 0);
    assert(requirement.arguments.size() == 1 &&
           requirement.arguments[0].kind ==
               abi_v10::WireTypeKind::Primitive &&
           requirement.arguments[0].identity == "int");
    assert(requirement.result &&
           requirement.result->kind == abi_v10::WireTypeKind::BoundVar &&
           requirement.result->ordinal == 0);
    assert(requirement.argument_access ==
           vector<abi_v10::Access>{abi_v10::Access::Read});
    assert(requirement.parent == (index == 0 ? 0u : 1u));
    assert(requirement_provenance[index].provenance_id ==
           requirement.provenance_id);
    assert(requirement_provenance[index].declaration_id ==
           summary.requirements[index].origin.call_expression_id);
    assert(requirement_provenance[index].module_id ==
           summary.requirements[index].origin.module_id);
    assert(requirement_provenance[index].start_line ==
           static_cast<uint32_t>(summary.requirements[index].origin.line));
    abi_v10::validate_provenance(
        abi_v10::provenance_record(requirement_provenance[index]));
  }
  const string encoded_summary = abi_v10::summary_payload(wire_summary);
  const auto decoded_summary = abi_v10::read_summary(encoded_summary);
  assert(abi_v10::summary_payload(decoded_summary) == encoded_summary);
  assert(decoded_summary.requirements.size() == 2);
  assert(decoded_summary.requirements[0].operation == "operator:*");
  assert(decoded_summary.requirements[1].operation == "operator:+");

  const Function& identity = find_function(program, "identity");
  assert(identity.open_summary);
  assert(identity.open_summary->requirements.empty());

  auto missing_facts = checked_program(R"moss(export fn compute(x):
  return x + 1.5
)moss");
  const Function& unsupported = find_function(missing_facts, "compute");
  assert(unsupported.open_summary);
  assert(!unsupported.open_summary->complete);
  assert(!unsupported.open_summary->result_type);
  assert(unsupported.open_summary->requirements.empty());

  std::cout << "PASS checker retained and ABI-v10 roundtripped two typed operator "
               "requirements for * and +; result preserves T; missing facts stay incomplete\n";
}
