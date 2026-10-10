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
  // Moss Int is a signed i64: convert with checked conversions and fail
  // closed, never with a wrapping `as` cast that could turn -1 into a huge
  // worker count or core id. Agent C still validates counts > 0 and
  // max_threads >= threads at start().
  auto fail = [&](const string& what) {
    return ".unwrap_or_else(|_| { eprintln!(" +
        rust_string_literal("moss executor: invalid " + what + " for Executor '" + s.a +
                            "' at line " + std::to_string(s.line)) +
        "); std::process::abort() })";
  };
  vector<string> converted;
  for (size_t index = 0; index < plan.calls.size(); ++index) {
    const auto& call = plan.calls[index];
    string temp = values[index] + "_checked";
    if (call.method == "affinity")
      o << pad << "let " << temp << ": std::vec::Vec<usize> = " << values[index]
        << ".iter().map(|__moss_core| <usize as std::convert::TryFrom<i64>>::try_from(*__moss_core)" << fail("affinity core id")
        << ").collect();\n";
    else if (call.method == "priority")
      o << pad << "let " << temp << ": i32 = <i32 as std::convert::TryFrom<i64>>::try_from(" << values[index] << ")"
        << fail("priority") << ";\n";
    else
      o << pad << "let " << temp << ": usize = <usize as std::convert::TryFrom<i64>>::try_from(" << values[index] << ")"
        << fail(call.method) << ";\n";
    converted.push_back(temp);
  }
  o << pad << "let mut " << s.a << " = MossExecutor::new()";
  for (size_t index = 0; index < plan.calls.size(); ++index)
    o << "." << plan.calls[index].method << "(" << converted[index] << ")";
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
  auto domain_type = types.find(plan.target_domain_binding);
  if (domain_type == types.end() ||
      !domains_.count(canonical_type_name(domain_type->second)))
    throw std::runtime_error(
        "internal error: Executor Root target lost its checked domain type");
  const Domain& target_domain =
      *domains_.at(canonical_type_name(domain_type->second));
  const Handler* target_handler = find_handler(target_domain, plan.handler_name);
  if (!target_handler)
    throw std::runtime_error(
        "internal error: Executor Root target lost its checked handler");
  const bool root_handles_failure = !target_handler->failure_arms.empty();
  o << pad << "        let _ = " << domain_temp << "." << plan.handler_name
    << (root_handles_failure ? "_root(" : "_shared(");
  for (size_t index = 0; index < argument_locals.size(); ++index) {
    if (index) o << ", ";
    if (root_handles_failure)
      o << argument_locals[index];
    else
      o << shared_message_forward_argument(
          Param{argument_locals[index], plan.argument_types[index], false}, true);
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

// ---------------------------------------------------------------------------
// Top-level `message` from `main`: synchronous Root ingress (sec. 5).
//
// A message evaluated by a statement the checker marked message_root_ingress
// is an independent Root, not a nested call. It lowers through Agent C's
// synchronous ingress, which uses inline admission before start() and after
// join(), and executor admission while ACTIVE:
//
//   { let __d = target.clone(); let __a0 = <owned snapshot>; ...
//     runtime_invoke(move || __d.Handler_shared(<lend __a0>)[.unwrap reply]) }
//
// The target and every argument are evaluated exactly once at the message
// statement, snapshotted by value with the ordinary message-boundary
// lowering, and moved into a `Send + 'static` thunk; the caller waits for
// that Root and receives its reply. Nested messages (inside handlers,
// functions, methods) keep the direct synchronous lowering.
//
string root_ingress_message(const string& receiver, const string& handler,
                            const vector<string>& arguments,
                            const Domain& target_domain, const Handler& target_handler,
                            const Domain* d, const std::set<string>& locals,
                            const std::unordered_map<string,string>* types,
                            const vector<size_t>* message_argument_plans) const {
  std::ostringstream rendered;
  string domain_temp = "__moss_root_domain_" + std::to_string(executor_invoke_temp_++);
  rendered << "{ let " << domain_temp << " = " << expr(receiver, d, locals, types) << ".clone(); ";
  vector<string> argument_locals;
  for (size_t index = 0; index < arguments.size(); ++index) {
    string temp = "__moss_root_arg_" + std::to_string(executor_invoke_temp_++);
    rendered << "let " << temp << " = "
             << message_arg(arguments[index], target_handler.params.at(index).type, d,
                            locals, types,
                            message_argument_plans ? message_argument_plans->at(index) : 0)
             << "; ";
    argument_locals.push_back(temp);
  }
  rendered << "runtime_invoke(move || ";
  const bool root_handles_failure = !target_handler.failure_arms.empty();
  const bool raises = !target_handler.observable_effects.raise_set.empty() &&
      !root_handles_failure;
  if (raises && !phase21_result_context_)
    throw std::runtime_error(
        "internal error: checked raising Root message lacks a Phase 21 outcome context");
  if (root_handles_failure && !exported_domain_bridge_body_) {
    rendered << domain_temp << "." << handler << "_root(";
    for (size_t index = 0; index < argument_locals.size(); ++index)
      rendered << (index ? ", " : "") << argument_locals[index];
    rendered << ")";
  } else if (target_domain.exported && !exported_domain_bridge_body_) {
    rendered << domain_temp << ".__moss_message_" << handler << "(";
    for (size_t index = 0; index < argument_locals.size(); ++index)
      rendered << (index ? ", " : "") << argument_locals[index];
    rendered << ")";
  } else {
    rendered << domain_temp << "." << handler << "_shared(";
    for (size_t index = 0; index < argument_locals.size(); ++index)
      rendered << (index ? ", " : "")
               << shared_message_forward_argument(
                      Param{argument_locals[index], target_handler.params.at(index).type, false},
                      true);
    rendered << ")";
    if (target_handler.reply_type) {
      if (raises)
        rendered << ".map(|value| value.unwrap_or_else(|| std::process::abort()))";
      else
        rendered << ".unwrap_or_else(|| std::process::abort())";
    }
  }
  rendered << ")";
  if (raises) rendered << "?";
  rendered << " }";
  return rendered.str();
}
