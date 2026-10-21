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

static bool synthetic_imported_call_rejected(const string& argument,
    const string& payload,
    const vector<std::pair<string, string>>& provenance_records) {
  try {
    std::istringstream input("module Service\n"
        "export fn compute(x):\n  return x\n"
        "fn main():\n  value = compute(" + argument + ")\n");
    Program program = Parser(lex_lines(input, "Service.mossi")).parse();
    program.package_identities = true;
    program.module_identities["Service"] = kImportedModuleId;
    auto& function = program.functions.front();
    function.semantic_callable_id = kImportedCallableId;
    function.defining_module = "Service";
    function.imported_checked_summary_payload = payload;
    function.imported_checked_provenance_records = provenance_records;
    Checker checker(program);
    checker.run();
    return false;
  } catch (const CompileError& error) {
    assert(error.code == "TYPE_MISMATCH" ||
           error.code == "TYPE_INFERENCE_FAILED");
    assert(string(error.what()).find("compatible '") != string::npos ||
           string(error.what()).find("invalid typed '") != string::npos ||
           string(error.what()).find("required '") != string::npos);
    return true;
  }
}

static Program checked_arithmetic_source(const string& expression) {
  std::istringstream input("export fn compute(x):\n  return " + expression +
                           "\n");
  Program program = Parser(lex_lines(input, "<checked-arithmetic-body>")).parse();
  Checker checker(program);
  checker.run();
  return program;
}

static bool arithmetic_body_rejected(const string& expression) {
  try {
    Program program = checked_arithmetic_source(expression);
    return !checked_arithmetic_body(find_function(program, "compute"));
  } catch (const CompileError&) {
    return true;
  }
}

int main() {
  assert(Checker::unit_test_control_join_binding_facts());
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

  auto checked_body = checked_arithmetic_body(compute);
  assert(checked_body);
  assert(checked_body->statements.size() == 1);
  const auto& return_stmt = checked_body->statements.front();
  assert(return_stmt.kind == checked_ir::StmtKind::Return);
  assert(return_stmt.type->structurally_equal(*generic));
  assert(return_stmt.expr);
  const auto& add_expr = *return_stmt.expr;
  assert(add_expr.kind == checked_ir::ExprKind::Binary);
  assert(add_expr.binary_op == checked_ir::BinaryOp::Add);
  assert(add_expr.type->structurally_equal(*generic));
  assert(add_expr.operands.size() == 2);
  const auto& mul_expr = add_expr.operands[0];
  const auto& add_literal = add_expr.operands[1];
  assert(mul_expr.kind == checked_ir::ExprKind::Binary);
  assert(mul_expr.binary_op == checked_ir::BinaryOp::Mul);
  assert(mul_expr.type->structurally_equal(*generic));
  assert(mul_expr.operands.size() == 2);
  const auto& parameter = mul_expr.operands[0];
  const auto& multiply_literal = mul_expr.operands[1];
  assert(parameter.kind == checked_ir::ExprKind::Parameter &&
         parameter.ordinal == 0 && parameter.type->structurally_equal(*generic));
  for (const auto* literal : {&multiply_literal, &add_literal})
    assert(literal->kind == checked_ir::ExprKind::IntegerLiteral &&
           literal->type->structurally_equal(*Type::primitive("int")));
  assert(multiply_literal.literal_value == "2");
  assert(add_literal.literal_value == "20");
  const string checked_ir_bytes =
      checked_ir::encode_checked_body_v1(checked_body->statements);
  auto checked_ir_roundtrip = checked_ir::decode_h3_checked_body_v1(
      checked_ir_bytes, *program.type_context, summary.parameter_types,
      summary.result_type, checked_body->semantic_hash);
  assert(checked_ir_roundtrip.size() == 1);
  assert(checked_ir::encode_checked_body_v1(checked_ir_roundtrip) ==
         checked_ir_bytes);
  assert(checked_ir::compute_checked_body_hash_v1(checked_ir_roundtrip) ==
         checked_body->semantic_hash);

  auto missing_requirement = *compute.open_summary;
  missing_requirement.requirements.pop_back();
  Function bad_facts = compute;
  bad_facts.open_summary = std::move(missing_requirement);
  assert(!checked_arithmetic_body(bad_facts));
  auto reordered_requirements = *compute.open_summary;
  std::swap(reordered_requirements.requirements[0],
            reordered_requirements.requirements[1]);
  bad_facts.open_summary = std::move(reordered_requirements);
  assert(!checked_arithmetic_body(bad_facts));
  auto inconsistent_requirement = *compute.open_summary;
  inconsistent_requirement.requirements[1].parent_requirement_id =
      string("missing-parent");
  bad_facts.open_summary = std::move(inconsistent_requirement);
  assert(!checked_arithmetic_body(bad_facts));
  auto inconsistent_type = *compute.open_summary;
  inconsistent_type.requirements[0].result = Type::primitive("int");
  bad_facts.open_summary = std::move(inconsistent_type);
  assert(!checked_arithmetic_body(bad_facts));
  assert(arithmetic_body_rejected("x + 20"));
  assert(arithmetic_body_rejected("x * 2 - 20"));
  assert(arithmetic_body_rejected("x * 2 + 20 + 1"));
  assert(arithmetic_body_rejected("20 + x * 2"));

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

  auto joined_binding = checked_program(R"moss(export fn select(value, flag: Bool):
  if flag:
    let local = value
  else:
    let local = value
  return local

export fn choose(value, flag: Bool):
  if flag:
    return value
  else:
    return value

export fn branch_compute(value, flag: Bool):
  if flag:
    let item = value * 2
  else:
    let item = value * 3
  return item + 1
)moss");
  const Function& select = find_function(joined_binding, "select");
  // The checker retains one exact join binding at the control-flow merge.
  // Summary completeness must include that binding and all branch occurrences.
  assert(select.open_summary && select.open_summary->complete);
  assert(select.open_summary->occurrence_ledger_complete);

  const Function& branch_compute = find_function(joined_binding,
                                                  "branch_compute");
  assert(branch_compute.open_summary && branch_compute.open_summary->complete);
  assert(branch_compute.open_summary->occurrence_ledger_complete);
  assert(branch_compute.open_summary->operations.size() == 3);
  assert(branch_compute.open_summary->requirements.size() == 3);
  vector<abi_v10::SourceProvenance> branch_provenance;
  const auto branch_requirements = abi_v10_checked_requirements(
      branch_compute.open_summary->requirements,
      branch_compute.open_summary->bound_variable_count, branch_provenance);
  assert(branch_requirements && branch_requirements->size() == 3);
  abi_v10::Summary branch_wire;
  branch_wire.callable_id = branch_compute.semantic_callable_id;
  branch_wire.variable_count = branch_compute.open_summary->bound_variable_count;
  for (size_t index = 0;
       index < branch_compute.open_summary->parameter_types.size(); ++index) {
    auto type = abi_v10_checked_type(
        branch_compute.open_summary->parameter_types[index],
        branch_wire.variable_count);
    assert(type);
    branch_wire.parameters.push_back(*type);
    branch_wire.parameter_access.push_back(abi_v10_checked_access(
        branch_compute.open_summary->parameter_access[index]));
  }
  auto branch_result = abi_v10_checked_type(
      branch_compute.open_summary->result_type, branch_wire.variable_count);
  assert(branch_result);
  branch_wire.result = *branch_result;
  branch_wire.requirements = *branch_requirements;
  branch_wire.body_semantic_hash = "body-semantic-v1:sha256:" +
      string(64, 'b');
  const auto branch_callable_provenance = abi_v10_checked_provenance(
      branch_compute.open_summary->origin.module_id,
      branch_compute.semantic_callable_id, branch_compute.source_file,
      branch_compute.line);
  branch_wire.provenance_id = branch_callable_provenance.provenance_id;
  assert(branch_wire.requirements[0].operation == "operator:*");
  assert(branch_wire.requirements[1].operation == "operator:*");
  assert(branch_wire.requirements[2].operation == "operator:+");
  assert(branch_wire.requirements[0].dependencies.empty());
  assert(branch_wire.requirements[1].dependencies.empty());
  assert(branch_wire.requirements[2].parent == 0);
  assert(branch_wire.requirements[2].dependencies == vector<uint32_t>({1, 2}));
  const auto branch_payload = abi_v10::summary_payload(branch_wire);
  const auto branch_roundtrip = abi_v10::read_summary(branch_payload);
  assert(branch_roundtrip.requirements[2].parent == 0);
  assert(branch_roundtrip.requirements[2].dependencies ==
         vector<uint32_t>({1, 2}));
  const auto legacy_v2_payload = abi_v10::summary_payload(wire_summary, 2);
  const auto legacy_v2_summary = abi_v10::read_summary(legacy_v2_payload);
  assert(legacy_v2_summary.requirements.size() == 2);
  assert(legacy_v2_summary.requirements[1].dependencies ==
         vector<uint32_t>{1});
  bool multi_edge_v2_rejected = false;
  try {
    (void)abi_v10::summary_payload(branch_wire, 2);
  } catch (const std::exception&) {
    multi_edge_v2_rejected = true;
  }
  assert(multi_edge_v2_rejected);
  auto duplicate_dependencies = branch_wire;
  duplicate_dependencies.requirements[2].dependencies = {1, 1};
  bool duplicate_rejected = false;
  try {
    (void)abi_v10::summary_payload(duplicate_dependencies);
  } catch (const std::exception&) {
    duplicate_rejected = true;
  }
  assert(duplicate_rejected);

  const Function& choose = find_function(joined_binding, "choose");
  assert(choose.open_summary && choose.open_summary->complete);
  assert(choose.checked_body_ir_v1_bytes && choose.checked_body_ir_v1_hash);
  const auto choose_body = checked_ir::decode_checked_body_v1(
      *choose.checked_body_ir_v1_bytes, *joined_binding.type_context,
      choose.open_summary->parameter_types, choose.open_summary->result_type,
      *choose.checked_body_ir_v1_hash);
  assert(choose_body.size() == 1 &&
         choose_body.front().kind == checked_ir::StmtKind::If &&
         choose_body.front().then_branch.size() == 1 &&
         choose_body.front().else_branch.size() == 1 &&
         choose_body.front().then_branch.front().kind ==
             checked_ir::StmtKind::Return &&
         choose_body.front().else_branch.front().kind ==
             checked_ir::StmtKind::Return);

  std::istringstream staged_input(R"moss(export fn staged(value):
  let doubled = value * 2
  return doubled + 20
)moss");
  Program staged_program = Parser(
      lex_lines(staged_input, "<checked-straight-line-body>")).parse();
  staged_program.type_context = std::make_shared<TypeContext>();
  Checker staged_checker(staged_program);
  staged_checker.run();
  const Function& staged = find_function(staged_program, "staged");
  assert(staged.open_summary && staged.open_summary->complete);
  assert(staged.checked_body_ir_v1_bytes && staged.checked_body_ir_v1_hash);
  const auto staged_body = checked_ir::decode_checked_body_v1(
      *staged.checked_body_ir_v1_bytes, *staged_program.type_context,
      staged.open_summary->parameter_types, staged.open_summary->result_type,
      *staged.checked_body_ir_v1_hash);
  assert(staged_body.size() == 2);
  assert(staged_body[0].kind == checked_ir::StmtKind::Let &&
         staged_body[0].local_ordinal == 0 && staged_body[0].expr &&
         staged_body[0].expr->kind == checked_ir::ExprKind::Binary);
  assert(staged_body[1].kind == checked_ir::StmtKind::Return &&
         staged_body[1].expr &&
         staged_body[1].expr->kind == checked_ir::ExprKind::Binary &&
         staged_body[1].expr->operands.front().kind ==
             checked_ir::ExprKind::LocalRead);


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

  for (const char* operation : {"operator:*", "operator:+"}) {
    assert(admit_typed_arithmetic_operator(operation, Type::primitive("int"),
        Type::primitive("int"), Type::primitive("int")) ==
        BuiltinOperatorAdmission::Admitted);
    assert(admit_typed_arithmetic_operator(operation, Type::primitive("float"),
        Type::primitive("int"), Type::primitive("float")) ==
        BuiltinOperatorAdmission::Admitted);
    assert(admit_typed_arithmetic_operator(operation, Type::primitive("string"),
        Type::primitive("int"), Type::primitive("string")) ==
        BuiltinOperatorAdmission::Rejected);
    assert(admit_typed_arithmetic_operator(operation, Type::primitive("bool"),
        Type::primitive("int"), Type::primitive("bool")) ==
        BuiltinOperatorAdmission::Rejected);
    assert(admit_typed_arithmetic_operator(operation, Type::primitive("int"),
        Type::primitive("string"), Type::primitive("int")) ==
        BuiltinOperatorAdmission::Rejected);
    assert(admit_typed_arithmetic_operator(operation, Type::primitive("int"),
        Type::primitive("int"), Type::primitive("float")) ==
        BuiltinOperatorAdmission::Rejected);
  }
  const TypeRef open = Type::var({0, 0, 0});
  assert(admit_typed_arithmetic_operator("operator:*", open,
      Type::primitive("int"), open) ==
      BuiltinOperatorAdmission::Deferred);
  assert(!synthetic_imported_call_rejected("5", imported_payload,
                                           imported_provenance));
  assert(!synthetic_imported_call_rejected("2.5", imported_payload,
                                           imported_provenance));
  assert(synthetic_imported_call_rejected("\"bad\"", imported_payload,
                                           imported_provenance));
  assert(synthetic_imported_call_rejected("true", imported_payload,
                                           imported_provenance));

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
  auto wrong_result_wire = imported_wire;
  abi_v10::WireType wrong_result;
  wrong_result.kind = abi_v10::WireTypeKind::Primitive;
  wrong_result.identity = "int";
  wrong_result_wire.requirements[1].result = wrong_result;
  assert(synthetic_summary_rejected(
      abi_v10::summary_payload(wrong_result_wire), imported_provenance));
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

  std::cout << "PASS checked arithmetic body hash: " << checked_body->semantic_hash
               << "; canonical tree and codec roundtrip passed; missing, reordered, "
               "inconsistent, and unsupported body forms rejected; checker retained "
               "and ABI-v10 roundtripped two typed operator "
               "requirements for * and +; import preserves T, parent links, and provenance; H3 empty requirements and malformed-record rejection pass\n";
}
