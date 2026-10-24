#define main moss_cli_entry_for_h3_common_lowering_test
#include "../../src/moss.cpp"
#undef main

#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1]);
  assert(input.good());
  moss::Program program = moss::parse_source(input, argv[1]);
  (void)moss::check_program(program);

  const auto find_function = [&](const std::string& name)
      -> const moss::Function& {
    const auto found = std::find_if(program.functions.begin(),
        program.functions.end(), [&](const moss::Function& function) {
      return function.name == name;
    });
    assert(found != program.functions.end());
    return *found;
  };
  const auto& private_identity = find_function("private_identity");
  const auto& identity_chain = find_function("identity_chain");
  const auto& private_box = find_function("private_box");
  const auto& nested_private_box = find_function("nested_private_box");
  const auto& fresh = find_function("fresh");
  const auto& relay = find_function("relay");
  const auto verify_common_body = [&](const moss::Function& function) {
    if (!function.checked_body_ir_v1_occurrence_lowered) {
      std::cerr << "H3 helper used migration fallback: " << function.name
                << " scheme=" << (function.open_summary &&
                      function.open_summary->scheme_complete)
                << " ledger=" << (function.open_summary &&
                      function.open_summary->occurrence_ledger_complete)
                << " callable=" << function.semantic_callable_id
                << " declaration=" << function.declaration_name << '\n';
      if (function.open_summary) {
        for (const auto& dependency :
             function.open_summary->semantic_dependencies)
          std::cerr << " dependency=" << dependency << '\n';
        for (const auto& operation : function.open_summary->operations)
          std::cerr << " operation=" << operation.operation_id
                    << " occurrence=" << operation.expression_occurrence_id
                    << " result=" << (operation.result
                        ? operation.result->to_canonical_string() : "<none>")
                    << '\n';
      }
      for (const auto& edge : program.semantic_call_edges)
        if (edge.source == "fn:" + function.name)
          std::cerr << " edge=" << edge.source << " -> " << edge.target
                    << " line=" << edge.line << " kind="
                    << edge.invocation_kind << '\n';
    }
    assert(function.checked_body_ir_v1_occurrence_lowered);
    assert(function.checked_body_ir_v1_bytes);
    assert(function.checked_body_ir_v1_hash);
    assert(function.open_summary);
    assert(program.type_context);
    const auto decoded = moss::checked_ir::decode_checked_body_v1(
        *function.checked_body_ir_v1_bytes, *program.type_context,
        function.open_summary->parameter_types,
        function.open_summary->result_type,
        *function.checked_body_ir_v1_hash);
    assert(moss::checked_ir::encode_checked_body_v1(decoded) ==
           *function.checked_body_ir_v1_bytes);
    assert(moss::checked_ir::compute_checked_body_hash_v1(decoded) ==
           *function.checked_body_ir_v1_hash);
    return decoded;
  };

  const auto private_body = verify_common_body(private_identity);
  assert(private_body.size() == 1 &&
         private_body.front().kind == moss::checked_ir::StmtKind::Return &&
         private_body.front().expr &&
         private_body.front().expr->kind ==
             moss::checked_ir::ExprKind::Parameter &&
         private_body.front().expr->ordinal == 0);
  const auto private_box_body = verify_common_body(private_box);
  const auto nested_private_box_body = verify_common_body(nested_private_box);
  const auto fresh_body = verify_common_body(fresh);
  const auto relay_body = verify_common_body(relay);
  assert(!private_box_body.empty() && !nested_private_box_body.empty() &&
         !fresh_body.empty() && !relay_body.empty());
  const auto chain_body = verify_common_body(identity_chain);
  assert(chain_body.size() == 1 &&
         chain_body.front().kind == moss::checked_ir::StmtKind::Return &&
         chain_body.front().expr &&
         chain_body.front().expr->kind == moss::checked_ir::ExprKind::Call &&
         chain_body.front().expr->semantic_target_id ==
             private_identity.semantic_callable_id);
}
