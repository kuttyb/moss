// Phase 20 Agent D -- native Rust codegen for `Executor` and
// `executor.invoke(concrete_domain.Handler(args...))`.
//
// There is no real Agent-C root-admission/worker-pool runtime in this tree
// (out of scope for Agent D; see AGENTS.md task boundaries). `invoke` is
// therefore lowered through a narrow, explicitly documented adapter: the
// target handler is called inline and synchronously at the submission point,
// reusing exactly the same "_shared" handler-entry call convention and
// argument-snapshot lowering (shared_message_arg) as an ordinary synchronous
// `message`. This preserves one-way semantics, argument-snapshot-by-value,
// and submission order, but not physical concurrency. `Executor()` itself
// becomes an inert `()` marker binding; `join()` is a no-op, since inline
// invoke has already completed by the time it executes.

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
                        "' -- no Agent-C root-admission runtime is wired up "
                        "yet; invoke() below runs inline/synchronously");
    o << pad << "let mut " << s.a << " = ();\n";
    locals.insert(s.a);
    types[s.a] = "executor";
    return true;
  }
  if (s.kind != Stmt::Kind::Call || !executor_typed(types, s.a)) return false;
  if (s.b == "join") {
    backend_comment(o, spaces,
                    "EXECUTOR '" + s.a + "'.join(): inline adapter has "
                    "already completed every invoked root");
    o << pad << "let _ = &" << s.a << ";\n";
    return true;
  }
  if (s.b != "invoke")
    throw std::runtime_error("internal error: unchecked Executor operation '" + s.b + "'");
  string domain_binding, handler_name;
  vector<string> handler_args;
  if (!parse_member_call(s.args.front(), domain_binding, handler_name, handler_args))
    throw std::runtime_error("internal error: unchecked executor.invoke shape");
  auto receiver_type = types.find(domain_binding);
  if (receiver_type == types.end() || !domains_.count(canonical_type_name(receiver_type->second)))
    throw std::runtime_error("internal error: unchecked executor.invoke target");
  const Domain& target_domain = *domains_.at(canonical_type_name(receiver_type->second));
  const Handler* target_handler = find_handler(target_domain, handler_name);
  if (!target_handler)
    throw std::runtime_error("internal error: unchecked executor.invoke handler");
  backend_comment(o, spaces,
                  "EXECUTOR INVOKE '" + target_domain.name + "." + handler_name +
                      "' -- inline one-way root submission (no physical "
                      "worker thread)");
  o << pad << "{\n";
  vector<string> argument_locals;
  for (size_t index = 0; index < handler_args.size(); ++index) {
    string temp = "__moss_invoke_arg_" + std::to_string(executor_invoke_temp_++);
    o << pad << "    let " << temp << " = "
      << shared_message_arg(handler_args[index], target_handler->params.at(index).type,
                            nullptr, locals, &types, 0)
      << ";\n";
    argument_locals.push_back(temp);
  }
  o << pad << "    let _ = " << domain_binding << "." << handler_name << "_shared(";
  for (size_t index = 0; index < argument_locals.size(); ++index) {
    if (index) o << ", ";
    o << argument_locals[index];
  }
  o << ");\n" << pad << "}\n";
  return true;
}
