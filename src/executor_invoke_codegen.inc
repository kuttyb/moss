// Phase 20 Agent D -- native Rust codegen for `Executor` and
// `executor.invoke(concrete_domain.Handler(args...))`.
//
// Lowers the checked ExecutorStartPlan / RootSubmissionPlan (ast.hpp)
// against Agent C's root-admission ABI (docs/ROOT_RUNTIME_ABI.md, "Physical
// signatures & Integration Seam (Agent C Runtime)"):
//
//   MossExecutor::new().threads(n).max_threads(n).queue_capacity(n)
//       .affinity(cores).priority(level).start() -> MossExecutorHandle
//   handle.enqueue_root(MossRootDescriptor::with_target(id, target, work))
//   handle.join()
//
// Agent D defines none of these. There is no valid inline production
// fallback for `executor.invoke`: "submit an independent Root and continue
// after submission" is not "run the handler now". The submitted work is an
// owned `FnOnce() + Send + 'static` thunk: argument snapshots are evaluated
// *at the invoke statement* (sec. 4.2) into owned values via message_arg
// (the same value-boundary lowering a message payload uses), the target
// domain reference is cloned, and the thunk lends those owned snapshots to
// the handler's `_shared` entry when the Root runs.
//
// Codegen consumes the plans' checked facts only: the target binding,
// semantic identities, backend handler selector, and formal argument types
// all come from RootSubmissionPlan; no domain/handler is re-resolved here.

void emit_executor_start(std::ostringstream& o, const Stmt& s, int spaces,
                         const ExecutorStartPlan& plan, std::set<string>& locals,
                         std::unordered_map<string,string>& types) {
  const string pad(spaces, ' ');
  backend_comment(o, spaces,
                  "EXECUTOR '" + s.a + "' -- ExecutorStartPlan: configuration "
                  "arguments evaluated once, in source order, before start()");
  vector<string> values;
  for (const auto& call : plan.calls) {
    string temp = "__moss_executor_cfg_" + std::to_string(executor_invoke_temp_++);
    o << pad << "let " << temp << " = " << expr(call.argument, nullptr, locals, &types) << ";\n";
    values.push_back(temp);
  }
  o << pad << "let mut " << s.a << " = MossExecutor::new()";
  for (size_t index = 0; index < plan.calls.size(); ++index) {
    const auto& call = plan.calls[index];
    const string& value = values[index];
    if (call.method == "affinity")
      o << ".affinity(" << value << ".iter().map(|__moss_core| *__moss_core as usize).collect())";
    else if (call.method == "priority")
      o << ".priority(" << value << " as i32)";
    else
      o << "." << call.method << "(" << value << " as usize)";
  }
  o << ".start();\n";
  locals.insert(s.a);
  types[s.a] = "executor";
}

void emit_root_submit(std::ostringstream& o, const Stmt& s, int spaces,
                      const RootSubmissionPlan& plan, std::set<string>& locals,
                      std::unordered_map<string,string>& types) {
  const string pad(spaces, ' ');
  backend_comment(o, spaces,
                  "EXECUTOR INVOKE " + plan.handler_semantic_identity +
                      " -- RootSubmissionPlan; independent one-way Root, "
                      "caller continues after submission");
  o << pad << "{\n";
  string domain_temp = "__moss_invoke_domain_" + std::to_string(executor_invoke_temp_++);
  o << pad << "    let " << domain_temp << " = " << plan.target_domain_binding << ".clone();\n";
  vector<string> argument_locals;
  for (size_t index = 0; index < plan.argument_expressions.size(); ++index) {
    string temp = "__moss_invoke_arg_" + std::to_string(executor_invoke_temp_++);
    o << pad << "    let " << temp << " = "
      << message_arg(plan.argument_expressions[index], plan.argument_types[index],
                     nullptr, locals, &types, 0)
      << ";\n";
    argument_locals.push_back(temp);
  }
  o << pad << "    " << s.a << ".enqueue_root(MossRootDescriptor::with_target(next_root_id(), "
    << rust_string_literal(plan.handler_semantic_identity) << ", move || {\n";
  // Forward each owned snapshot to the handler's `_shared` entry exactly as
  // the route contract's owned `__moss_message_<Handler>` impl does (lend a
  // borrowed-payload type, pass a copy type by value); this also works for
  // per-instance specialized domain refs, which carry no route-contract impl.
  o << pad << "        let _ = " << domain_temp << "." << plan.handler_name << "_shared(";
  for (size_t index = 0; index < argument_locals.size(); ++index) {
    if (index) o << ", ";
    o << shared_message_forward_argument(Param{argument_locals[index], plan.argument_types[index], false}, true);
  }
  o << ");\n" << pad << "    }));\n" << pad << "}\n";
}

void emit_executor_join(std::ostringstream& o, const Stmt& s, int spaces) {
  backend_comment(o, spaces,
                  "EXECUTOR '" + s.a + "'.join() -- drains admitted roots, "
                  "consumes the executor");
  o << string(spaces, ' ') << s.a << ".join();\n";
}

bool gen_executor_statement(std::ostringstream& o, const Stmt& s, int spaces,
                            std::set<string>& locals,
                            std::unordered_map<string,string>& types) {
  if (s.kind == Stmt::Kind::Assign || s.kind == Stmt::Kind::Let ||
      s.kind == Stmt::Kind::Var) {
    vector<std::pair<string, vector<string>>> calls;
    if (!parse_executor_chain(s.b, calls) || calls.empty() ||
        calls.back().first != "start")
      return false;
    if (!s.executor_start_plan_id)
      throw std::runtime_error(
          "internal error: Executor start reached codegen without a checked "
          "ExecutorStartPlan");
    emit_executor_start(o, s, spaces,
                        semantic_program_->executor_start_plans.at(s.executor_start_plan_id - 1),
                        locals, types);
    return true;
  }
  if (s.kind != Stmt::Kind::Call || !executor_typed(types, s.a)) return false;
  if (s.b == "join") {
    emit_executor_join(o, s, spaces);
    return true;
  }
  if (s.b != "invoke")
    throw std::runtime_error("internal error: unchecked Executor operation '" + s.b + "'");
  if (!s.root_submission_plan_id)
    throw std::runtime_error(
        "internal error: executor.invoke reached codegen without a checked "
        "RootSubmissionPlan");
  emit_root_submit(o, s, spaces,
                   semantic_program_->root_submission_plans.at(s.root_submission_plan_id - 1),
                   locals, types);
  return true;
}
