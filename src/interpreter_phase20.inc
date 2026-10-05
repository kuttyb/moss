// Phase 20 Agent D -- Fast Debug (interpreter) legality/plan-level support
// for `Executor`/`executor.invoke` and root-local `FileIO`.
//
// Fast Debug is a *separate* execution engine from native codegen; it does
// not share Agent D's Rust seams (branch_publish/branch_join, FileIO::open,
// etc.) and must not invent its own competing runtime to paper over Agent
// B/C's absence. Per AGENTS.md corrective-pass items 2 and 24:
//   - `executor.invoke(...)` has no valid inline-execution production
//     fallback (see executor_invoke_codegen.inc's comment) -- turning it
//     into Fast Debug's synchronous `message(...)` would just relocate the
//     same semantic error into a different backend. It throws a clear,
//     honest "pending Agent C" RuntimeError instead.
//   - `FileIO.open(...)` (and therefore every chunk pipeline, which needs
//     real bytes to do anything at all) throws a clear "pending Agent B"
//     RuntimeError. Chunk-pipeline *algorithm* correctness (bounded-K
//     ordering, speculative discard, termination) is independently testable
//     without either agent through native codegen's test-only Rust shim
//     (tests/tooling/fixtures/), which is in scope for Agent D; a *real*
//     Fast Debug FileIO path is integration-stage work for whenever Agent B
//     lands, not something Agent D should re-implement here to look done.
//   - `Executor()`/`.threads()`/etc. and `.join()` need no runtime at all
//     (E6: "without [an executor], roots/branches use inline mode" -- a
//     construct/join with no invoke in between is a legal no-op), so those
//     remain real, not stubbed.

  bool interp_phase20_statement(const Stmt& statement, Frame& frame,
                                std::ostream& output) {
    (void)output;
    if (statement.kind == Stmt::Kind::Assign || statement.kind == Stmt::Kind::Let ||
        statement.kind == Stmt::Kind::Var) {
      if (interp_is_executor_construction(statement.b)) {
        frame.locals[statement.a] = Value::unit();
        frame.local_types[statement.a] = "executor";
        return true;
      }
      std::vector<std::string> open_arguments;
      if (interp_parse_fileio_open(statement.b, open_arguments)) {
        throw RuntimeError(statement.line,
            "FileIO requires Agent B's Phase 20 runtime integration; not yet "
            "executable in Fast Debug (see src/interpreter_phase20.inc)");
      }
      return false;
    }
    if (statement.kind != Stmt::Kind::Call) return false;
    auto receiver_type = frame.local_types.find(statement.a);
    if (receiver_type == frame.local_types.end() || receiver_type->second != "executor")
      return false;
    if (statement.b == "join") return true;
    if (statement.b == "invoke") {
      throw RuntimeError(statement.line,
          "executor.invoke requires Agent C's Phase 20 root-admission "
          "runtime; not yet executable in Fast Debug (see "
          "src/interpreter_phase20.inc)");
    }
    return false;
  }

  static bool interp_is_executor_construction(const std::string& text) {
    std::string value = trim_copy(text);
    while (true) {
      auto member = top_level_member(value);
      if (!member) {
        std::string callee;
        std::vector<std::string> args;
        return parse_call(value, callee, args) && callee == "Executor" && args.empty();
      }
      value = member->first;
    }
  }

  static bool interp_parse_fileio_open(const std::string& text,
                                       std::vector<std::string>& args) {
    auto member = top_level_member(text);
    if (!member || member->first != "FileIO") return false;
    std::string method_name;
    return parse_call(member->second, method_name, args) && method_name == "open" &&
        args.size() == 2;
  }
