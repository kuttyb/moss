// Phase 20 Agent D -- native Rust codegen for `Executor` and
// `executor.invoke(concrete_domain.Handler(args...))`.
//
// There is no valid inline-execution production fallback for
// `executor.invoke` (unlike a compiler Branch -- see branch_runtime.hpp):
// "submit an independent Root and continue after submission" is not the
// same operation as "execute the handler synchronously before continuing".
// Agent D therefore lowers the checked RootSubmissionPlan (ast.hpp) through
// exactly one narrow seam targeting three not-yet-defined Rust names that
// Agent C's root-admission runtime is expected to supply:
//
//   moss_root_start()                        -- emit_executor_start
//   moss_root_submit(&executor, move || ...) -- emit_root_submit
//   moss_root_join(executor)                 -- emit_executor_join
//
// Agent D does not define these names anywhere in src/ and does not guess
// their final signature beyond this minimal shape (a handle value, and a
// plain `FnOnce` thunk for the one-way submitted work, with argument
// snapshotting already complete by the time the thunk is built -- "Arguments
// are evaluated when invoke executes", sec. 4.2). A standalone native build
// of a program using executor.invoke is therefore expected to need Agent
// C's crate at link time; that is an acceptable, documented integration-time
// dependency (AGENTS.md corrective-pass item 6), not something Agent D
// hides by substituting synchronous execution.
//
// For Agent D's own isolated tests, tests/tooling/fixtures/ supplies a
// minimal TEST-ONLY deterministic root queue defining these same three
// names (see check_phase20_executor_fileio.py); it is never emitted by the
// compiler and is not documented as Phase 20 runtime behavior.

bool gen_executor_statement(std::ostringstream& o, const Stmt& s, int spaces,
                            std::set<string>& locals,
                            std::unordered_map<string,string>& types) {
  const string pad(spaces, ' ');
  if (s.kind == Stmt::Kind::Assign || s.kind == Stmt::Kind::Let ||
      s.kind == Stmt::Kind::Var) {
    vector<std::pair<string, vector<string>>> calls;
    if (!parse_executor_chain(s.b, calls) || calls.empty() ||
        calls.back().first != "start")
      return false;
    backend_comment(o, spaces,
                    "EXECUTOR '" + s.a +
                        "' -- emit_executor_start seam; Agent C's "
                        "moss_root_start() is not defined in this build");
    o << pad << "let mut " << s.a << " = moss_root_start();\n";
    locals.insert(s.a);
    types[s.a] = "executor";
    return true;
  }
  if (s.kind != Stmt::Kind::Call || !executor_typed(types, s.a)) return false;
  if (s.b == "join") {
    backend_comment(o, spaces,
                    "EXECUTOR '" + s.a +
                        "'.join() -- emit_executor_join seam; Agent C's "
                        "moss_root_join() drains admitted roots");
    o << pad << "moss_root_join(" << s.a << ");\n";
    return true;
  }
  if (s.b != "invoke")
    throw std::runtime_error("internal error: unchecked Executor operation '" + s.b + "'");
  if (!s.root_submission_plan_id)
    throw std::runtime_error(
        "internal error: executor.invoke reached codegen without a checked "
        "RootSubmissionPlan");
  const RootSubmissionPlan& plan =
      semantic_program_->root_submission_plans.at(s.root_submission_plan_id - 1);
  auto receiver_type = types.find(plan.target_domain_binding);
  if (receiver_type == types.end() ||
      !domains_.count(canonical_type_name(receiver_type->second)))
    throw std::runtime_error("internal error: unchecked executor.invoke target");
  const Domain& target_domain = *domains_.at(canonical_type_name(receiver_type->second));
  const Handler* target_handler = find_handler(target_domain, plan.handler_name);
  if (!target_handler)
    throw std::runtime_error("internal error: unchecked executor.invoke handler");
  backend_comment(o, spaces,
                  "EXECUTOR INVOKE '" + target_domain.name + "." + plan.handler_name +
                      "' -- emit_root_submit seam; RootSubmissionPlan carries "
                      "the checked target/handler/argument facts, Agent C's "
                      "moss_root_submit() carries no Moss-specific knowledge");
  o << pad << "{\n";
  // A domain reference is an Arc-like shared handle elsewhere in generated
  // code (e.g. domain_capability clones in gen_block's Let/Var case); clone
  // it into the submitted thunk rather than moving the caller's binding, so
  // `main` can still invoke the same domain again afterward.
  string domain_temp = "__moss_invoke_domain_" + std::to_string(executor_invoke_temp_++);
  o << pad << "    let " << domain_temp << " = " << plan.target_domain_binding
    << ".clone();\n";
  vector<string> argument_locals;
  for (size_t index = 0; index < plan.argument_expressions.size(); ++index) {
    string temp = "__moss_invoke_arg_" + std::to_string(executor_invoke_temp_++);
    o << pad << "    let " << temp << " = "
      << shared_message_arg(plan.argument_expressions[index],
                            target_handler->params.at(index).type, nullptr,
                            locals, &types, 0)
      << ";\n";
    argument_locals.push_back(temp);
  }
  o << pad << "    moss_root_submit(&" << s.a << ", move || {\n";
  o << pad << "        let _ = " << domain_temp << "."
    << plan.handler_name << "_shared(";
  for (size_t index = 0; index < argument_locals.size(); ++index) {
    if (index) o << ", ";
    o << argument_locals[index];
  }
  o << ");\n" << pad << "    });\n" << pad << "}\n";
  return true;
}
