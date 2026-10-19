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

static const string kImportedModuleId = "module-v1:provider@1.0.0::Service";
static const string kImportedCallableId =
    "callable-v1:module-v1:provider@1.0.0::Service:fn:compute";

static abi_v10::SourceProvenance synthetic_requirement_provenance(
    const string& id, int line) {
  abi_v10::SourceProvenance provenance;
  provenance.provenance_id = id;
  provenance.module_id = kImportedModuleId;
  provenance.logical_source_id = "logical-source:Service.moss";
  provenance.source_file_display = "service.moss";
  provenance.start_line = static_cast<uint32_t>(line);
  provenance.start_column = 3;
  provenance.end_line = static_cast<uint32_t>(line);
  provenance.end_column = 17;
  provenance.declaration_id = "requirement:" + std::to_string(line);
  return provenance;
}

static abi_v10::Summary synthetic_arithmetic_summary() {
  abi_v10::WireType generic;
  generic.kind = abi_v10::WireTypeKind::BoundVar;
  generic.ordinal = 0;
  abi_v10::WireType integer;
  integer.kind = abi_v10::WireTypeKind::Primitive;
  integer.identity = "int";

  abi_v10::Summary summary;
  summary.callable_id = kImportedCallableId;
  summary.variable_count = 1;
  summary.parameters = {generic};
  summary.parameter_access = {abi_v10::Access::Read};
  summary.result = generic;
  summary.provenance_id = "summary-provenance";
  summary.body_semantic_hash = "body-semantic-v1:sha256:" + string(64, 'a');
  for (size_t index = 0; index < 2; ++index) {
    abi_v10::Requirement requirement;
    requirement.receiver = generic;
    requirement.operation = index == 0 ? "operator:*" : "operator:+";
    requirement.access = abi_v10::Access::Read;
    requirement.parent = index == 0 ? 0 : 1;
    requirement.provenance_id = "requirement-provenance-" +
        std::to_string(index);
    requirement.arguments = {integer};
    requirement.result = generic;
    requirement.argument_access = {abi_v10::Access::Read};
    summary.requirements.push_back(std::move(requirement));
  }
  return summary;
}

static OpenCallableSummaryFacts import_synthetic_summary(
    const string& payload,
    const vector<std::pair<string, string>>& provenance_records) {
  std::istringstream input("module Service\nexport fn compute(x):\n  return x\n");
  Program program = Parser(lex_lines(input, "Service.mossi")).parse();
  program.package_identities = true;
  program.module_identities["Service"] = kImportedModuleId;
  auto& function = program.functions.front();
  function.semantic_callable_id = kImportedCallableId;
  function.defining_module = "Service";
  function.imported_checked_summary_payload = payload;
  function.imported_checked_provenance_records = provenance_records;
  // This deliberately conflicting legacy hint must not replace sealed facts.
  function.constraints.emplace_back(ConstraintKind::Operator, "x",
                                    "operator:%", "x");
  Checker checker(program);
  checker.run();
  assert(function.open_summary && function.open_summary->complete);
  return *function.open_summary;
}

static bool synthetic_summary_rejected(
    const string& payload,
    const vector<std::pair<string, string>>& provenance_records) {
  try {
    (void)import_synthetic_summary(payload, provenance_records);
  } catch (const CompileError&) {
    return true;
  } catch (const abi_v10::Error&) {
    return true;
  }
  return false;
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

  auto unsupported_requirements = summary.requirements;
  unsupported_requirements[0].target_id = "operator:/";
  vector<abi_v10::SourceProvenance> rejected_provenance;
  assert(!abi_v10_checked_requirements(
      unsupported_requirements, summary.bound_variable_count,
      rejected_provenance));
  assert(rejected_provenance.empty());

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

  auto imported_wire = synthetic_arithmetic_summary();
  vector<std::pair<string, string>> imported_provenance;
  for (size_t index = 0; index < 2; ++index) {
    auto provenance = synthetic_requirement_provenance(
        imported_wire.requirements[index].provenance_id,
        static_cast<int>(index + 4));
    auto record = abi_v10::provenance_record(provenance);
    imported_provenance.emplace_back(record.id, record.payload);
  }
  const string imported_payload = abi_v10::summary_payload(imported_wire);
  auto imported = import_synthetic_summary(imported_payload,
                                            imported_provenance);
  assert(imported.requirements.size() == 2);
  assert(imported.parameter_types.size() == 1 &&
         imported.parameter_types[0]->kind == TypeKind::InferenceVar &&
         imported.parameter_types[0]->var_id.ordinal == 0);
  assert(imported.result_type->structurally_equal(
      *imported.parameter_types[0]));
  const auto& imported_multiply = imported.requirements[0];
  const auto& imported_add = imported.requirements[1];
  assert(imported_multiply.target_id == "operator:*");
  assert(imported_add.target_id == "operator:+");
  for (const auto* requirement : {&imported_multiply, &imported_add}) {
    assert(requirement->subject->structurally_equal(
        *imported.parameter_types[0]));
    assert(requirement->arguments.size() == 1 &&
           requirement->arguments[0]->structurally_equal(
               *Type::primitive("int")));
    assert(requirement->result && (*requirement->result)->structurally_equal(
           *imported.parameter_types[0]));
    assert(requirement->access == AccessMode::Read);
    assert(requirement->argument_access ==
           vector<AccessMode>{AccessMode::Read});
    assert(requirement->origin.module_id == kImportedModuleId);
    assert(requirement->origin.enclosing_callable == kImportedCallableId);
    assert(requirement->origin.source_file == "service.moss");
    assert(requirement->origin.line >= 4 && requirement->origin.column == 3);
    assert(requirement->origin.call_expression_id == requirement->id);
  }
  assert(!imported_multiply.parent_requirement_id);
  assert(imported_add.parent_requirement_id == imported_multiply.id);
  assert(imported_multiply.id != imported_add.id);

  auto incomplete_wire = imported_wire;
  incomplete_wire.requirements.pop_back();
  assert(synthetic_summary_rejected(
      abi_v10::summary_payload(incomplete_wire), {imported_provenance.front()}));
  auto unsupported_wire = imported_wire;
  unsupported_wire.requirements[1].operation = "operator:/";
  assert(synthetic_summary_rejected(
      abi_v10::summary_payload(unsupported_wire), imported_provenance));
  auto unlinked_wire = imported_wire;
  unlinked_wire.requirements[1].parent = 0;
  assert(synthetic_summary_rejected(
      abi_v10::summary_payload(unlinked_wire), imported_provenance));
  auto duplicate_identity_wire = imported_wire;
  duplicate_identity_wire.requirements[1].provenance_id =
      duplicate_identity_wire.requirements[0].provenance_id;
  assert(synthetic_summary_rejected(
      abi_v10::summary_payload(duplicate_identity_wire), imported_provenance));
  auto inconsistent_wire = imported_wire;
  inconsistent_wire.variable_count = 2;
  inconsistent_wire.requirements[1].result->ordinal = 1;
  assert(synthetic_summary_rejected(
      abi_v10::summary_payload(inconsistent_wire), imported_provenance));
  assert(synthetic_summary_rejected(imported_payload,
                                    {imported_provenance.front()}));
  assert(synthetic_summary_rejected(imported_payload + "x",
                                    imported_provenance));

  auto h3_wire = imported_wire;
  h3_wire.requirements.clear();
  auto imported_h3 = import_synthetic_summary(
      abi_v10::summary_payload(h3_wire), {});
  assert(imported_h3.requirements.empty());
  assert(synthetic_summary_rejected(abi_v10::summary_payload(h3_wire),
                                    imported_provenance));

  std::cout << "PASS checker retained and ABI-v10 roundtripped two typed operator "
               "requirements for * and +; import preserves T, parent links, and provenance; H3 empty requirements and malformed-record rejection pass\n";
}
