// Phase 20 Agent D -- Fast Debug (interpreter) support for `Executor` /
// `executor.invoke`, and the FileIO integration boundary.
//
// Executor: Fast Debug models Phase 20 roots as a deterministic sequential
// schedule (corrective item 17), preserving the semantic difference between
// `executor.invoke` and synchronous `message`:
//   start  -> create an active logical executor (configuration was already
//             checked; Fast Debug evaluates it for its effects only);
//   invoke -> evaluate/snapshot the handler arguments *now*, enqueue an
//             independent logical root, and return after submission;
//   join   -> drain the queued roots in submission order through the normal
//             synchronous handler machinery (run_handler_call), then consume
//             the executor.
// This is a valid schedule of Phase 20 roots, not a competing worker pool:
// it models no physical parallelism, fairness, or timing.
//
// FileIO: native execution uses Agent B's production runtime. Fast Debug
// intentionally does not implement a second POSIX FileIO backend in Phase 20;
// `FileIO.open(...)` (and therefore any chunk pipeline) reports that FileIO
// execution requires the native backend.

  std::map<std::string, std::vector<PreparedHandlerCall>> fast_debug_executors_;

  bool interp_phase20_statement(const Stmt& statement, Frame& frame,
                                std::ostream& output) {
    if (statement.kind == Stmt::Kind::Assign || statement.kind == Stmt::Kind::Let ||
        statement.kind == Stmt::Kind::Var) {
      const auto slot = statement.kind == Stmt::Kind::Assign
          ? est::ExprSlot::RhsValue : est::ExprSlot::Initializer;
      auto checked_value = expression_tree(statement.b,
          "phase20_statement_initializer line=" +
              std::to_string(statement.line) + " kind=" +
              std::to_string(static_cast<unsigned>(statement.kind)) +
              " target=" + statement.a,
          std::pair{slot, 0u});
      if (!checked_value)
        throw RuntimeError(statement.line,
            "MOSS_INTERNAL_INVARIANT missing checked statement initializer");
      std::vector<est::ExprId> config_arguments;
      if (interp_executor_construction(*checked_value, config_arguments)) {
        for (const auto& argument : config_arguments)
          evaluate_tree_node(*checked_value, argument, frame, statement.line,
                             output);  // source order, once
        frame.locals[statement.a] = Value::unit();
        frame.local_types[statement.a] = "executor";
        fast_debug_executors_[statement.a].clear();
        emit("executor_start", frame, statement.line, statement.a);
        return true;
      }
      if (interp_parse_fileio_open(*checked_value)) {
        throw RuntimeError(statement.line,
            "Fast Debug FileIO execution is intentionally unsupported in Phase 20; "
            "run this program with the native backend");
      }
      return false;
    }
    if (statement.kind != Stmt::Kind::Call) return false;
    auto receiver_type = frame.local_types.find(statement.a);
    if (receiver_type == frame.local_types.end() || receiver_type->second != "executor")
      return false;
    auto executor = fast_debug_executors_.find(statement.a);
    if (executor == fast_debug_executors_.end())
      throw RuntimeError(statement.line, "internal error: unchecked inactive Executor");
    if (statement.b == "invoke") {
      // Submission: snapshot arguments now, run later at join.
      auto invocation_tree = expression_tree(statement.args.front(),
          "executor_invoke_argument",
          std::pair{est::ExprSlot::Argument, 0u});
      if (!invocation_tree || !statement.root_submission_plan_id ||
          statement.root_submission_plan_id >
              program_.root_submission_plans.size())
        throw RuntimeError(statement.line,
            "MOSS_INTERNAL_INVARIANT missing checked executor submission plan");
      const auto* call_node = std::get_if<est::Call>(
          &invocation_tree->arena->at(invocation_tree->root));
      if (!call_node)
        throw RuntimeError(statement.line,
            "MOSS_INTERNAL_INVARIANT missing checked executor call node");
      executor->second.push_back(prepare_handler_call(
          *invocation_tree,
          program_.root_submission_plans.at(
              statement.root_submission_plan_id - 1),
          *call_node, frame, statement.line, output));
      emit("root_submit", frame, statement.line, statement.args.front());
      return true;
    }
    if (statement.b == "join") {
      auto roots = std::move(executor->second);
      fast_debug_executors_.erase(executor);
      for (auto& root : roots) run_handler_call(std::move(root), frame, output);
      emit("executor_join", frame, statement.line, statement.a);
      return true;
    }
    return false;
  }

  // Recognizes `Executor()(.config(arg))*.start()` and collects the
  // configuration arguments in source order.
  bool interp_executor_construction(const ExpressionTreeView& tree,
                                    std::vector<est::ExprId>& config_arguments) const {
    est::ExprId current = tree.root;
    std::vector<est::ExprId> reversed;
    while (true) {
      const auto* call = std::get_if<est::Call>(&tree.arena->at(current));
      const auto* constructor = std::get_if<est::Constructor>(
          &tree.arena->at(current));
      if (!call && !constructor) return false;
      const est::ExprId callee_id = call ? call->callee : constructor->type;
      const auto* member = std::get_if<est::Member>(
          &tree.arena->at(callee_id));
      if (call && member) {
        if (member->name != "start")
          for (auto argument = call->arguments.rbegin();
               argument != call->arguments.rend(); ++argument)
            reversed.push_back(*argument);
        current = member->base;
        continue;
      }
      const auto* callee = std::get_if<est::Identifier>(
          &tree.arena->at(callee_id));
      const auto argument_count = call ? call->arguments.size()
                                       : constructor->arguments.size();
      if (!callee || callee->name != "Executor" || argument_count != 0)
        return false;
      config_arguments.clear();
      config_arguments.reserve(reversed.size());
      for (auto argument = reversed.rbegin(); argument != reversed.rend();
           ++argument)
        config_arguments.emplace_back(*argument);
      return true;
    }
  }

  bool interp_parse_fileio_open(const ExpressionTreeView& tree) const {
    const auto* call = std::get_if<est::Call>(&tree.arena->at(tree.root));
    if (!call) return false;
    const auto* member = std::get_if<est::Member>(
        &tree.arena->at(call->callee));
    if (!member || member->name != "open" || call->arguments.size() != 2)
      return false;
    const auto* receiver = std::get_if<est::Identifier>(
        &tree.arena->at(member->base));
    return receiver && receiver->name == "FileIO";
  }
