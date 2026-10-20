#define main moss_cli_entry_for_empty_vector_records_test
#include "../../src/moss.cpp"
#undef main

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

using namespace moss;

static const string kModuleId =
    "module-v1:empty-vector-provider@0.1.0::Provider";

static Program checked_provider(const string& functions) {
  std::istringstream input("module Provider\n\n" + functions);
  Program program = Parser(lex_lines(input, "Provider.moss")).parse();
  program.package_identities = true;
  program.module_identities["Provider"] = kModuleId;
  program.module_identities[""] = kModuleId;
  for (auto& function : program.functions) {
    function.declaration_name = function.name;
    function.defining_module = "Provider";
    function.source_file = "Provider.moss";
  }
  Checker checker(program);
  checker.run();
  for (auto& function : program.functions)
    if (!starts_with(function.name, "Provider__"))
      function.name = "Provider__" + function.name;
  return program;
}

static std::pair<std::size_t, std::size_t> emitted_counts(
    Program& program) {
  abi_v10::Interface value;
  value.module_id = kModuleId;
  append_abi_v10_empty_vector_records(value, program, "Provider");
  std::size_t summaries = 0;
  for (const auto& record : value.semantic_contract)
    if (!record.payload.empty() && record.payload.front() == 2 &&
        abi_v10::hex_hash(record.id) &&
        abi_v10::read_summary(record.payload).callable_id.find(":fn:fresh") !=
            string::npos)
      ++summaries;
  std::size_t bodies = 0;
  for (const auto& record : value.private_semantic_dependencies)
    if (record.id.rfind("checked-body-v1:", 0) == 0 &&
        abi_v10::read_checked_body_record(record.payload).callable_id.find(
            ":fn:fresh") != string::npos)
      ++bodies;
  return {summaries, bodies};
}

static void check_in_memory_eligibility() {
  setenv("MOSS_STAGED_ABI", "v10", 1);
  setenv("MOSS_PHASE25_EMPTY_VECTOR_EMIT", "1", 1);
  auto good = checked_provider("export fn fresh():\n  return []\n");
  assert(good.functions.size() == 1 && good.functions.front().open_summary &&
         good.functions.front().open_summary->complete);
  auto counts = emitted_counts(good);
  assert(counts.first == 1 && counts.second == 1);

  for (const auto& source : {
           string("export fn fresh():\n  return [1]\n"),
           string("export fn fresh(unused):\n  return []\n"),
           string("export fn fresh(values):\n  return values\n"),
           string("export fn fresh():\n  return 1\n"),
           string("export fn fresh():\n  values = []\n  return values\n")}) {
    auto unsupported = checked_provider(source);
    const auto unsupported_counts = emitted_counts(unsupported);
    assert(unsupported_counts.first == 0 && unsupported_counts.second == 0);
  }

  auto missing = checked_provider("export fn fresh():\n  return []\n");
  missing.functions.front().open_summary.reset();
  counts = emitted_counts(missing);
  assert(counts.first == 0 && counts.second == 0);

  auto inconsistent = checked_provider("export fn fresh():\n  return []\n");
  inconsistent.functions.front().open_summary->result_type =
      Type::primitive("int");
  counts = emitted_counts(inconsistent);
  assert(counts.first == 0 && counts.second == 0);

  auto unresolved = checked_provider("export fn fresh():\n  return []\n");
  unresolved.functions.front().open_summary->observable_effects.unresolved =
      true;
  counts = emitted_counts(unresolved);
  assert(counts.first == 0 && counts.second == 0);

  auto switch_disabled = checked_provider("export fn fresh():\n  return []\n");
  setenv("MOSS_PHASE25_EMPTY_VECTOR_EMIT", "0", 1);
  counts = emitted_counts(switch_disabled);
  assert(counts.first == 0 && counts.second == 0);
  setenv("MOSS_PHASE25_EMPTY_VECTOR_EMIT", "1", 1);
  setenv("MOSS_STAGED_ABI", "v9", 1);
  auto v9 = checked_provider("export fn fresh():\n  return []\n");
  counts = emitted_counts(v9);
  assert(counts.first == 0 && counts.second == 0);
}

static std::string read_file(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), {}};
}

static std::size_t count_lines_containing(const std::string& payload,
                                          const std::string& needle) {
  std::istringstream lines(payload);
  std::string line;
  std::size_t count = 0;
  while (std::getline(lines, line))
    if (line.find(needle) != std::string::npos) ++count;
  return count;
}

static void check_artifact(const std::string& bytes, bool experimental) {
  const auto sealed = abi_v10::decode(bytes);
  abi_v10::validate_complete_interface(sealed);
  const auto& iface = sealed.value;
  const string fresh_id = "callable-v1:" + iface.module_id + ":fn:fresh";
  const string dependent_id =
      "callable-v1:" + iface.module_id + ":fn:dependent";
  std::vector<abi_v10::Summary> summaries;
  for (const auto* records : {&iface.semantic_contract,
                               &iface.private_semantic_dependencies}) {
    for (const auto& record : *records) {
      if (record.payload.empty() || record.payload.front() != 2 ||
          !abi_v10::hex_hash(record.id)) continue;
      abi_v10::validate_summary(record);
      auto summary = abi_v10::read_summary(record.payload);
      if (summary.callable_id == fresh_id)
        summaries.push_back(std::move(summary));
    }
  }
  std::vector<abi_v10::CheckedBodyRecord> bodies;
  for (const auto& record : iface.private_semantic_dependencies) {
    if (record.id.rfind("checked-body-v1:", 0) != 0) continue;
    abi_v10::validate_checked_body_record(record);
    auto body = abi_v10::read_checked_body_record(record.payload);
    if (body.callable_id == fresh_id) bodies.push_back(std::move(body));
  }
  std::size_t generic_statements = 0;
  for (const auto& record : iface.semantic_contract)
    if (record.payload.find("export fn fresh") != string::npos)
      generic_statements = count_lines_containing(
          record.payload, "  generic_stmt ");

  // Direct generic parameter returns are a separate always-on ABI-v10 body
  // form. The empty-vector feature switch must not disable this record pair.
  std::vector<abi_v10::Summary> parameter_summaries;
  for (const auto* records : {&iface.semantic_contract,
                               &iface.private_semantic_dependencies})
    for (const auto& record : *records)
      if (!record.payload.empty() && record.payload.front() == 2 &&
          abi_v10::hex_hash(record.id) &&
          abi_v10::read_summary(record.payload).callable_id == dependent_id)
        parameter_summaries.push_back(abi_v10::read_summary(record.payload));
  std::vector<abi_v10::CheckedBodyRecord> parameter_bodies;
  for (const auto& record : iface.private_semantic_dependencies)
    if (record.id == "checked-body-v1:" + dependent_id)
      parameter_bodies.push_back(
          abi_v10::read_checked_body_record(record.payload));
  assert(parameter_summaries.size() == 1 && parameter_bodies.size() == 1);
  const auto& parameter_summary = parameter_summaries.front();
  const auto& parameter_body = parameter_bodies.front();
  assert(parameter_summary.variable_count == 1 &&
         parameter_summary.parameters.size() == 1 &&
         parameter_summary.parameters.front().kind ==
             abi_v10::WireTypeKind::BoundVar &&
         parameter_summary.parameters.front().ordinal == 0 &&
         parameter_summary.parameter_access ==
             std::vector<abi_v10::Access>{abi_v10::Access::Read} &&
         parameter_summary.result.kind == abi_v10::WireTypeKind::BoundVar &&
         parameter_summary.result.ordinal == 0 &&
         parameter_summary.requirements.empty() &&
         parameter_summary.dependencies.empty() &&
         parameter_body.semantic_hash ==
             parameter_summary.body_semantic_hash &&
         parameter_body.dependencies.empty());
  TypeContext parameter_context;
  const auto generic_type = Type::var(TypeVarId{0, 0, 0});
  const auto parameter_statements = checked_ir::decode_h3_checked_body_v1(
      parameter_body.checked_ir_bytes, parameter_context, {generic_type},
      generic_type, parameter_body.semantic_hash);
  assert(parameter_statements.size() == 1 &&
         parameter_statements.front().kind == checked_ir::StmtKind::Return &&
         parameter_statements.front().expr &&
         parameter_statements.front().expr->kind ==
             checked_ir::ExprKind::Parameter &&
         parameter_statements.front().expr->ordinal == 0);

  if (!experimental) {
    assert(summaries.empty() && bodies.empty());
    assert(generic_statements == 1);
    std::cout << "PASS switch-disabled counts: summary=0 body=0 generic_stmt=1\n";
    return;
  }

  assert(summaries.size() == 1 && bodies.size() == 1);
  const auto& summary = summaries.front();
  const auto& body = bodies.front();
  assert(summary.variable_count == 1 && summary.parameters.empty() &&
         summary.parameter_access.empty() && summary.requirements.empty());
  assert(summary.result.kind == abi_v10::WireTypeKind::Builtin &&
         summary.result.identity == "vector" &&
         summary.result.arguments.size() == 1 &&
         summary.result.arguments.front().kind ==
             abi_v10::WireTypeKind::BoundVar &&
         summary.result.arguments.front().ordinal == 0);
  assert(summary.body_semantic_hash == body.semantic_hash &&
         body.callable_id == fresh_id && body.dependencies.empty());
  assert(generic_statements == 1);

  TypeContext context;
  auto element = Type::var(TypeVarId{0, 0, 0});
  auto vector_type = Type::builtin("vector", {element});
  checked_ir::Expr literal;
  literal.kind = checked_ir::ExprKind::VectorLiteral;
  literal.type = vector_type;
  checked_ir::Stmt returned;
  returned.kind = checked_ir::StmtKind::Return;
  returned.type = vector_type;
  returned.expr = literal;
  const std::vector<checked_ir::Stmt> canonical{returned};
  const auto canonical_bytes = checked_ir::encode_checked_body_v1(canonical);
  const auto canonical_hash =
      checked_ir::compute_checked_body_hash_v1(canonical);
  assert(body.checked_ir_bytes == canonical_bytes);
  assert(body.semantic_hash == canonical_hash);
  assert(checked_ir::encode_checked_body_v1(canonical) == body.checked_ir_bytes);

  auto decoded_empty = checked_ir::decode_empty_vector_return_checked_body_v1(
      body.checked_ir_bytes, context, vector_type, body.semantic_hash);
  assert(decoded_empty.size() == 1 &&
         decoded_empty.front().expr->kind ==
             checked_ir::ExprKind::VectorLiteral &&
         decoded_empty.front().expr->operands.empty() &&
         decoded_empty.front().expr->type->structurally_equal(*vector_type));
  auto rejects_empty_vector_body = [&](const std::string& bytes,
                                       const TypeRef& result,
                                       const std::string& hash) {
    try {
      (void)checked_ir::decode_empty_vector_return_checked_body_v1(
          bytes, context, result, hash);
    } catch (const std::invalid_argument&) {
      return true;
    }
    return false;
  };
  auto nonempty_literal = literal;
  nonempty_literal.operands.push_back(checked_ir::Expr{
      checked_ir::ExprKind::IntegerLiteral, Type::primitive("int"), 0, "1"});
  auto nonempty_return = returned;
  nonempty_return.expr = nonempty_literal;
  const std::vector<checked_ir::Stmt> nonempty_body{nonempty_return};
  const auto nonempty_bytes = checked_ir::encode_checked_body_v1(nonempty_body);
  const auto nonempty_hash =
      checked_ir::compute_checked_body_hash_v1(nonempty_body);
  assert(rejects_empty_vector_body(nonempty_bytes, vector_type, nonempty_hash));
  assert(rejects_empty_vector_body(canonical_bytes, vector_type,
                                   "body-semantic-v1:sha256:" +
                                       std::string(64, '0')));
  assert(rejects_empty_vector_body(canonical_bytes, Type::primitive("int"),
                                   canonical_hash));
  auto scalar_return = returned;
  scalar_return.expr = checked_ir::Expr{
      checked_ir::ExprKind::IntegerLiteral, Type::primitive("int"), 0, "1"};
  scalar_return.type = Type::primitive("int");
  const std::vector<checked_ir::Stmt> unsupported_shape{scalar_return};
  const auto unsupported_bytes =
      checked_ir::encode_checked_body_v1(unsupported_shape);
  assert(rejects_empty_vector_body(
      unsupported_bytes, vector_type,
      checked_ir::compute_checked_body_hash_v1(unsupported_shape)));

  std::size_t callable_provenance_count = 0;
  for (const auto& record : iface.provenance) {
    abi_v10::validate_provenance(record);
    auto provenance = abi_v10::read_provenance(record.payload);
    if (provenance.provenance_id == summary.provenance_id) {
      ++callable_provenance_count;
      assert(provenance.declaration_id == fresh_id);
      assert(provenance.source_file_display == "provider.moss");
      assert(provenance.start_line == 3 && provenance.end_line == 3);
    }
  }
  assert(callable_provenance_count == 1);

  for (const auto& name : {"nonempty", "scalar", "multiple"}) {
    const string id = "callable-v1:" + iface.module_id + ":fn:" + name;
    assert(std::none_of(bodies.begin(), bodies.end(), [&](const auto& item) {
      return item.callable_id == id;
    }));
    for (const auto& record : iface.private_semantic_dependencies)
      if (record.id.rfind("checked-body-v1:", 0) == 0)
        assert(abi_v10::read_checked_body_record(record.payload).callable_id !=
               id);
  }
  std::cout << "PASS experimental empty-vector summary/body counts 1/1; hash="
            << body.semantic_hash
            << "; provenance=1; canonical bytes and tree verified; "
               "legacy generic_stmt=1; unsupported empty-vector bodies=0; "
               "direct parameter-return summary/body=1/1; interface valid\n";
}

int main(int argc, char** argv) {
  assert(argc == 3);
  check_in_memory_eligibility();
  check_artifact(read_file(argv[1]), string(argv[2]) == "experimental");
}
