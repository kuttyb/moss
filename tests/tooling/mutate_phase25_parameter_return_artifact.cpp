#include "abi_v10_records.hpp"
#include "checked_body_ir.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::string read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), {}};
}

void write_file(const std::string& path, const std::string& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output.good());
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  assert(output.good());
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 4 || argc == 5 || argc == 6);
  const std::string interface_path = argv[1];
  const std::string archive_path = argv[2];
  const std::string mode = argv[3];
  const std::string callable_name = argc >= 5 ? argv[4] : "fresh";
  const std::string provider_name = argc == 6 ? argv[5] : "Service";
  auto sealed = moss::abi_v10::decode(read_file(interface_path));
  const std::string old_digest = sealed.digest;
  const std::string callable = "callable-v1:" + sealed.value.module_id +
      ":fn:" + callable_name;

  auto summary_at = std::find_if(sealed.value.semantic_contract.begin(),
      sealed.value.semantic_contract.end(), [&](const auto& record) {
    return moss::abi_v10::hex_hash(record.id) && !record.payload.empty() &&
        moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::read_summary(record.payload).callable_id == callable;
  });
  assert(summary_at != sealed.value.semantic_contract.end());
  auto summary = moss::abi_v10::read_summary(summary_at->payload);
  auto body_at = std::find_if(
      sealed.value.private_semantic_dependencies.begin(),
      sealed.value.private_semantic_dependencies.end(), [&](const auto& record) {
    return record.id == "checked-body-v1:" + callable;
  });
  assert(body_at != sealed.value.private_semantic_dependencies.end());

  auto replace_summary = [&] {
    *summary_at = moss::abi_v10::summary_record(summary);
  };
  auto replace_body = [&](moss::abi_v10::CheckedBodyRecord body) {
    *body_at = moss::abi_v10::checked_body_record(body);
  };
  const std::string helper_callable =
      "callable-v1:" + sealed.value.module_id + ":fn:helper";
  auto helper_summary_at = std::find_if(
      sealed.value.private_semantic_dependencies.begin(),
      sealed.value.private_semantic_dependencies.end(), [&](const auto& record) {
    return moss::abi_v10::hex_hash(record.id) && !record.payload.empty() &&
        moss::abi_v10::typed_summary_payload(record.payload) &&
        moss::abi_v10::read_summary(record.payload).callable_id ==
            helper_callable;
  });
  auto helper_body_at = std::find_if(
      sealed.value.private_semantic_dependencies.begin(),
      sealed.value.private_semantic_dependencies.end(), [&](const auto& record) {
    return record.id == "checked-body-v1:" + helper_callable;
  });
  auto rewrite_decoded_caller = [&](auto&& rewrite) {
    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    moss::TypeContext context;
    const auto variable = moss::Type::var(moss::TypeVarId{0, 0, 0});
    auto statements = moss::checked_ir::decode_h3_checked_body_v1(
        body.checked_ir_bytes, context, {variable}, variable,
        body.semantic_hash);
    assert(statements.size() == 1 && statements.front().expr &&
           statements.front().expr->kind == moss::checked_ir::ExprKind::Call);
    rewrite(statements.front().expr.value());
    body.checked_ir_bytes =
        moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    replace_summary();
    replace_body(std::move(body));
  };
  auto rewrite_collection_body = [&](auto&& rewrite) {
    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    moss::TypeContext context;
    const auto variable = moss::Type::var(moss::TypeVarId{0, 0, 0});
    const auto vector = moss::Type::builtin("vector", {variable});
    auto statements = moss::checked_ir::decode_collection_loop_checked_body_v1(
        body.checked_ir_bytes, context, {vector, variable},
        moss::Type::primitive("int"), body.semantic_hash);
    rewrite(statements);
    body.checked_ir_bytes =
        moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    replace_summary();
    replace_body(std::move(body));
  };

  if (mode == "missing-summary") {
    sealed.value.semantic_contract.erase(summary_at);
  } else if (mode == "missing-body") {
    sealed.value.private_semantic_dependencies.erase(body_at);
  } else if (mode == "corrupted-summary") {
    const bool was_int = summary.result.kind ==
            moss::abi_v10::WireTypeKind::Primitive &&
        summary.result.identity == "int";
    summary.result.kind = moss::abi_v10::WireTypeKind::Primitive;
    summary.result.identity = was_int ? "bool" : "int";
    summary.result.arguments.clear();
    summary.result.ordinal = 0;
    replace_summary();
  } else if (mode == "weaken-effect") {
    summary.observable_effect_bits = 0;
    summary.observable_effect_tags.clear();
    replace_summary();
    auto weaken_legacy_effects = [&](auto& records) {
      std::size_t changed = 0;
      for (auto& record : records) {
        if (record.payload.find("fn " + callable_name) == std::string::npos ||
            record.payload.find("effects=") == std::string::npos)
          continue;
        const auto mutation = record.payload.find("local_mutation=0");
        const auto panic = record.payload.find("may_panic=1");
        assert(mutation != std::string::npos && panic != std::string::npos);
        record.payload.replace(mutation, std::string("local_mutation=0").size(),
                               "local_mutation=1");
        record.payload.replace(panic, std::string("may_panic=1").size(),
                               "may_panic=0");
        ++changed;
      }
      return changed;
    };
    const auto changed = weaken_legacy_effects(sealed.value.binary_contract) +
        weaken_legacy_effects(sealed.value.semantic_contract);
    assert(changed >= 2);
  } else if (mode == "type-binding") {
    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    auto wrong_type = moss::Type::var(moss::TypeVarId{0, 0, 1});
    moss::checked_ir::Expr parameter;
    parameter.kind = moss::checked_ir::ExprKind::Parameter;
    parameter.type = wrong_type;
    parameter.ordinal = 0;
    moss::checked_ir::Stmt returned;
    returned.kind = moss::checked_ir::StmtKind::Return;
    returned.type = wrong_type;
    returned.expr = std::move(parameter);
    const std::vector<moss::checked_ir::Stmt> statements{std::move(returned)};
    body.checked_ir_bytes = moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    replace_summary();
    replace_body(std::move(body));
  } else if (mode == "identity") {
    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    body.callable_id += ":forged";
    replace_body(std::move(body));
  } else if (mode == "mismatch-hash") {
    summary.body_semantic_hash =
        "body-semantic-v1:sha256:" + std::string(64, '8');
    replace_summary();
  } else if (mode == "missing-provenance") {
    auto& records = sealed.value.provenance;
    records.erase(std::remove_if(records.begin(), records.end(),
        [&](const auto& record) { return record.id == summary.provenance_id; }),
        records.end());
  } else if (mode == "forged-provenance") {
    auto& records = sealed.value.provenance;
    auto provenance_at = std::find_if(records.begin(), records.end(),
        [&](const auto& record) { return record.id == summary.provenance_id; });
    assert(provenance_at != records.end());
    auto provenance = moss::abi_v10::read_provenance(provenance_at->payload);
    provenance.declaration_id += ":forged";
    *provenance_at = moss::abi_v10::provenance_record(provenance);
  } else if (mode == "bad-field-identity" ||
             mode == "bad-field-ordinal" ||
             mode == "bad-field-type") {
    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    auto nominal = std::find_if(sealed.value.binary_contract.begin(),
        sealed.value.binary_contract.end(), [&](const auto& record) {
      return record.id == summary.parameters.front().identity;
    });
    assert(nominal != sealed.value.binary_contract.end());
    const auto template_record = moss::abi_v10::read_template(nominal->payload);
    moss::TypeContext context;
    moss::NominalDeclaration declaration;
    declaration.kind = moss::DeclarationKind::Record;
    declaration.program_name = "Service__" + template_record.declaration_name;
    declaration.module_id = sealed.value.module_id;
    declaration.declaration_name = template_record.declaration_name;
    for (const auto& field : template_record.fields)
      declaration.fields.push_back({field.name, "", field.type.kind ==
          moss::abi_v10::WireTypeKind::BoundVar});
    auto owner = context.declare(std::move(declaration));
    assert(owner->id == template_record.template_id &&
           owner->fields.size() == 1);
    const auto variable = moss::Type::var(moss::TypeVarId{0, 0, 0});
    const auto receiver = moss::Type::nominal_bound(
        owner, owner->program_name, {{owner->fields.front().name, variable}});
    auto statements = moss::checked_ir::decode_h3_checked_body_v1(
        body.checked_ir_bytes, context, {receiver}, variable,
        body.semantic_hash);
    assert(statements.size() == 1 && statements.front().expr &&
           statements.front().expr->kind == moss::checked_ir::ExprKind::Field);
    auto& field = *statements.front().expr;
    if (mode == "bad-field-identity")
      field.semantic_target_id += ":forged";
    else if (mode == "bad-field-ordinal")
      field.member_ordinal = 4;
    else
      field.type = moss::Type::primitive("bool");
    body.checked_ir_bytes = moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    replace_summary();
    replace_body(std::move(body));
  } else if (mode == "missing-dependency") {
    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    body.dependencies.clear();
    summary.dependencies.clear();
    replace_summary();
    replace_body(std::move(body));
  } else if (mode == "drop-requirement-dependency") {
    assert(summary.requirements.size() >= 2);
    auto& requirement = summary.requirements.back();
    assert(!requirement.dependencies.empty());
    requirement.dependencies.pop_back();
    requirement.parent = requirement.dependencies.empty()
        ? 0 : requirement.dependencies.front();
    replace_summary();
  } else if (mode == "shadow-local-ordinal") {
    assert(summary.parameters.size() == 3 &&
           summary.parameters[0].kind == moss::abi_v10::WireTypeKind::BoundVar &&
           summary.parameters[1].kind == moss::abi_v10::WireTypeKind::Primitive &&
           summary.parameters[1].identity == "bool" &&
           summary.parameters[2].kind == moss::abi_v10::WireTypeKind::Primitive &&
           summary.parameters[2].identity == "bool");
    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    moss::TypeContext context;
    const auto variable = moss::Type::var(moss::TypeVarId{0, 0, 0});
    const auto boolean = moss::Type::primitive("bool");
    auto statements = moss::checked_ir::decode_checked_body_v1(
        body.checked_ir_bytes, context, {variable, boolean, boolean}, variable,
        body.semantic_hash);
    bool changed = false;
    std::function<void(std::vector<moss::checked_ir::Stmt>&)> substitute =
        [&](std::vector<moss::checked_ir::Stmt>& block) {
      for (auto& statement : block) {
        if (!changed && statement.kind == moss::checked_ir::StmtKind::Return &&
            statement.expr && statement.expr->kind ==
                moss::checked_ir::ExprKind::LocalRead) {
          statement.expr->ordinal = 999;
          changed = true;
        }
        substitute(statement.then_branch);
        substitute(statement.else_branch);
        substitute(statement.body);
      }
    };
    substitute(statements);
    assert(changed);
    body.checked_ir_bytes = moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    replace_summary();
    replace_body(std::move(body));
  } else if (mode == "for-binder-type" || mode == "for-local-ordinal") {
    assert(summary.parameters.size() == 1 &&
           summary.requirements.size() == 1 &&
           summary.requirements.front().operation == "static iterator");
    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    moss::TypeContext context;
    const auto source_type = moss::Type::var(moss::TypeVarId{0, 0, 0});
    const auto int_type = moss::Type::primitive("int");
    auto statements = moss::checked_ir::decode_checked_body_v1(
        body.checked_ir_bytes, context, {source_type}, int_type,
        body.semantic_hash);
    bool changed = false;
    std::function<void(std::vector<moss::checked_ir::Stmt>&)> mutate =
        [&](std::vector<moss::checked_ir::Stmt>& block) {
      for (auto& statement : block) {
        if (!changed && statement.kind == moss::checked_ir::StmtKind::For) {
          if (mode == "for-binder-type")
            statement.type = moss::Type::primitive("bool");
          else
            statement.local_ordinal = 999;
          changed = true;
        }
        mutate(statement.then_branch);
        mutate(statement.else_branch);
        mutate(statement.body);
      }
    };
    mutate(statements);
    assert(changed);
    body.checked_ir_bytes =
        moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    replace_summary();
    replace_body(std::move(body));
  } else if (mode == "for-requirement-result") {
    assert(summary.requirements.size() == 1 &&
           summary.requirements.front().operation == "static iterator");
    summary.variable_count = 1;
    summary.requirements.front().result = moss::abi_v10::WireType{
        moss::abi_v10::WireTypeKind::Primitive, "bool", {}, 0};
    replace_summary();
  } else if (mode == "for-iterator-receiver") {
    assert(summary.requirements.size() == 1 &&
           summary.requirements.front().operation == "static iterator");
    summary.requirements.front().receiver = moss::abi_v10::WireType{
        moss::abi_v10::WireTypeKind::Primitive, "int", {}, 0};
    replace_summary();
  } else if (mode == "missing-callee-summary") {
    assert(helper_summary_at != sealed.value.private_semantic_dependencies.end());
    sealed.value.private_semantic_dependencies.erase(helper_summary_at);
  } else if (mode == "missing-callee-body") {
    assert(helper_body_at != sealed.value.private_semantic_dependencies.end());
    sealed.value.private_semantic_dependencies.erase(helper_body_at);
  } else if (mode == "wrong-callable") {
    rewrite_decoded_caller([&](auto& expression) {
      expression.semantic_target_id += ":forged";
    });
  } else if (mode == "dependency-hash") {
    assert(summary.dependencies.size() == 1 &&
           summary.dependencies.front().first == helper_callable);
    summary.dependencies.front().second =
        "body-semantic-v1:sha256:" + std::string(64, '7');
    replace_summary();
    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    assert(body.dependencies.size() == 1 &&
           body.dependencies.front().target_id == helper_callable);
    body.dependencies.front().expected_hash = summary.dependencies.front().second;
    replace_body(std::move(body));
  } else if (mode == "wrong-type") {
    summary.result.kind = moss::abi_v10::WireTypeKind::Primitive;
    summary.result.identity = "int";
    summary.result.ordinal = 0;
    summary.result.arguments.clear();
    replace_summary();
  } else if (mode == "wrong-argument-type") {
    assert(!summary.parameters.empty());
    summary.parameters.front().kind = moss::abi_v10::WireTypeKind::Primitive;
    summary.parameters.front().identity = "int";
    summary.parameters.front().ordinal = 0;
    summary.parameters.front().arguments.clear();
    replace_summary();
  } else if (mode == "unsupported-arity") {
    rewrite_decoded_caller([&](auto& expression) {
      assert(expression.operands.size() == 1);
      expression.operands.push_back(expression.operands.front());
    });
  } else if (mode == "unsupported-shape") {
    rewrite_decoded_caller([&](auto& expression) {
      expression.kind = moss::checked_ir::ExprKind::LocalRead;
      expression.semantic_target_id.clear();
      expression.operands.clear();
      expression.ordinal = 0;
    });
  } else if (mode == "vector-mislink-element") {
    assert(summary.requirements.size() == 1 &&
           summary.requirements.front().operation ==
               "builtin:vector-literal" &&
           summary.requirements.front().arguments.size() == 2);
    auto& second = summary.requirements.front().arguments.back();
    const auto& receiver = summary.requirements.front().receiver;
    assert(second.kind == moss::abi_v10::WireTypeKind::BoundVar &&
           receiver.kind == moss::abi_v10::WireTypeKind::BoundVar &&
           second.ordinal != receiver.ordinal);
    second = receiver;
    replace_summary();
  } else if (mode == "vector-drop-relation") {
    assert(summary.requirements.size() == 1 &&
           summary.requirements.front().operation ==
               "builtin:vector-literal");
    summary.requirements.clear();
    replace_summary();
  } else if (mode == "vector-impossible-sametype") {
    assert(summary.parameters.size() == 2 && summary.requirements.size() == 1 &&
           summary.requirements.front().operation ==
               "builtin:vector-literal");
    auto set_primitive = [](auto& type, const std::string& identity) {
      type.kind = moss::abi_v10::WireTypeKind::Primitive;
      type.identity = identity;
      type.ordinal = 0;
      type.arguments.clear();
    };
    set_primitive(summary.parameters[0], "int");
    set_primitive(summary.parameters[1], "string");
    summary.variable_count = 0;
    summary.result.kind = moss::abi_v10::WireTypeKind::Builtin;
    summary.result.identity = "vector";
    summary.result.ordinal = 0;
    summary.result.arguments = {summary.parameters[0]};
    auto& relation = summary.requirements.front();
    set_primitive(relation.receiver, "int");
    assert(relation.arguments.size() == 2);
    set_primitive(relation.arguments[0], "int");
    set_primitive(relation.arguments[1], "string");
    assert(relation.result);
    relation.result->kind = moss::abi_v10::WireTypeKind::Builtin;
    relation.result->identity = "vector";
    relation.result->ordinal = 0;
    relation.result->arguments = {relation.receiver};

    auto body = moss::abi_v10::read_checked_body_record(body_at->payload);
    moss::TypeContext context;
    const auto integer = moss::Type::primitive("int");
    const auto text = moss::Type::primitive("string");
    const auto vector = moss::Type::builtin("vector", {integer});
    moss::checked_ir::Expr first;
    first.kind = moss::checked_ir::ExprKind::Parameter;
    first.type = integer;
    first.ordinal = 0;
    moss::checked_ir::Expr second;
    second.kind = moss::checked_ir::ExprKind::Parameter;
    second.type = text;
    second.ordinal = 1;
    moss::checked_ir::Expr literal;
    literal.kind = moss::checked_ir::ExprKind::VectorLiteral;
    literal.type = vector;
    literal.operands = {first, second};
    moss::checked_ir::Stmt returned;
    returned.kind = moss::checked_ir::StmtKind::Return;
    returned.type = vector;
    returned.expr = std::move(literal);
    const std::vector<moss::checked_ir::Stmt> statements{std::move(returned)};
    body.checked_ir_bytes = moss::checked_ir::encode_checked_body_v1(statements);
    body.semantic_hash =
        moss::checked_ir::compute_checked_body_hash_v1(statements);
    summary.body_semantic_hash = body.semantic_hash;
    replace_summary();
    replace_body(std::move(body));
  } else if (mode == "collection-extra-else") {
    rewrite_collection_body([&](auto& statements) {
      assert(statements.size() >= 4 &&
             statements[2].kind == moss::checked_ir::StmtKind::While &&
             !statements[2].body.empty() &&
             statements[2].body[0].kind == moss::checked_ir::StmtKind::If &&
             statements[2].body[0].then_branch.size() == 1);
      statements[2].body[0].else_branch.push_back(
          statements[2].body[0].then_branch.front());
    });
  } else if (mode == "collection-index-start") {
    rewrite_collection_body([&](auto& statements) {
      assert(!statements.empty() && statements[0].kind ==
                 moss::checked_ir::StmtKind::Var && statements[0].expr &&
             statements[0].expr->kind ==
                 moss::checked_ir::ExprKind::IntegerLiteral);
      statements[0].expr->literal_value = "1";
    });
  } else if (mode == "collection-count-start") {
    rewrite_collection_body([&](auto& statements) {
      assert(statements.size() == 5 && statements[1].kind ==
                 moss::checked_ir::StmtKind::Var && statements[1].expr &&
             statements[1].expr->kind ==
                 moss::checked_ir::ExprKind::IntegerLiteral);
      statements[1].expr->literal_value = "1";
    });
  } else if (mode == "duplicate-record") {
    auto duplicate = *summary_at;
    duplicate.id = "sha256:" + std::string(64, 'f');
    sealed.value.semantic_contract.push_back(std::move(duplicate));
    std::sort(sealed.value.semantic_contract.begin(),
              sealed.value.semantic_contract.end(), [](const auto& left,
                                                        const auto& right) {
      return left.id < right.id;
    });
  } else {
    assert(false && "unknown parameter-return artifact mutation mode");
  }

  auto sort_records = [](auto& records) {
    std::sort(records.begin(), records.end(), [](const auto& left,
                                                  const auto& right) {
      return left.id < right.id;
    });
  };
  sort_records(sealed.value.binary_contract);
  sort_records(sealed.value.semantic_contract);
  sort_records(sealed.value.private_semantic_dependencies);
  sort_records(sealed.value.provenance);
  const auto rewritten = moss::abi_v10::encode(sealed.value);
  const auto new_digest = moss::abi_v10::parse(rewritten).digest;
  auto archive = read_file(archive_path);
  const auto old_marker = moss::abi_v10::provider_marker(
      provider_name, old_digest);
  const auto new_marker = moss::abi_v10::provider_marker(
      provider_name, new_digest);
  std::size_t replacements = 0;
  for (auto position = archive.find(old_marker);
       position != std::string::npos;
       position = archive.find(old_marker, position + new_marker.size())) {
    archive.replace(position, old_marker.size(), new_marker);
    ++replacements;
  }
  assert(replacements > 0);
  moss::abi_v10::validate_selected_rlib(archive, provider_name, new_digest);
  write_file(interface_path, rewritten);
  write_file(archive_path, archive);
}
