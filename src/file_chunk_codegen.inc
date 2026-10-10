// Phase 20 Agent D -- native Rust codegen for root-local FileIO statements
// and for `file.chunks(size) |> map(f) |> reduce(initial, combine)` chunk
// pipelines. See file_chunk_lowering.inc for the checker-side ChunkParallelPlan
// and docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md secs. 8-13, 20 for the
// normative sequential reference semantics every lowering here must match.
//
// Agent D owns this compiler-side lowering, not a FileIO or Branch runtime.
// Generated code targets the physical seams of its sibling agents:
//  - FileIO/Range: Agent B's `FileIO::open(&str, &str)`, `read(i64, i64) ->
//    Range`, `write(i64, data)`, `sync()`, `sync_dataonly()`, `close()`, and
//    `Range::len() -> i64`. Agent D defines none of them.
//  - Branches: Agent C's join-scope ABI -- `branch_scope_new_current()`,
//    `branch_publish(&scope, work: FnOnce() + Send + 'static)` (nonblocking;
//    the runtime may execute overflow inline), `branch_join(scope)`. Agent D
//    emits no Branch scheduler of its own.
//  - A Branch cannot borrow the parent's `&FileIO` (its work must be
//    'static), so each Branch receives an owned, compiler-internal FileIO
//    read-borrow token from `moss_fileio_branch_read_borrow(&file)`: read-only,
//    never a Moss value, never a second FileIO owner, and dead by the join.
//    This is a reserved A+B+C+D seam Agent B is expected to implement (see
//    docs/ROOT_RUNTIME_ABI.md "Agent D compiler seams").
//  - Branch results flow through compiler-owned result slots
//    (Arc<Mutex<Option<outcome>>>), never through the scheduler, which needs
//    no knowledge of the mapped type.
// An *ineligible* pipeline (impure/FileIO-reaching map, capturing map, or a
// FileIO-reaching combine) never uses the Branch ABI: it lowers to the plain
// sequential reference loop in the parent Root.

bool gen_fileio_statement(std::ostringstream& o, const Stmt& s, int spaces,
                          const Domain* d, std::set<string>& locals,
                          std::unordered_map<string,string>& types) {
  const string pad(spaces, ' ');
  if (s.kind == Stmt::Kind::Assign || s.kind == Stmt::Kind::Let ||
      s.kind == Stmt::Kind::Var) {
    vector<string> open_arguments;
    if (parse_fileio_open(s.b, open_arguments)) {
      o << pad << "let mut " << s.a << " = FileIO::open(&("
        << expr(open_arguments[0], d, locals, &types) << "), "
        << rust_string_literal(trim(open_arguments[1])) << ");\n";
      locals.insert(s.a);
      types[s.a] = "FileIO";
      return true;
    }
    string receiver, method;
    vector<string> arguments;
    if (parse_member_call(s.b, receiver, method, arguments) &&
        fileio_typed(types, receiver) && method == "read" &&
        arguments.size() == 2) {
      o << pad << "let " << s.a << " = "
        << method_receiver_place(receiver, "read", arguments, d, locals, &types)
        << ".read(" << expr(arguments[0], d, locals, &types) << ", "
        << expr(arguments[1], d, locals, &types) << ");\n";
      locals.insert(s.a);
      types[s.a] = "Range";
      return true;
    }
    return false;
  }
  if (s.kind != Stmt::Kind::Call || !fileio_typed(types, s.a)) return false;
  string receiver_expr = method_receiver_place(
      s.a, s.b, s.args, d, locals, &types);
  if (s.b == "write" && s.args.size() == 2) {
    o << pad << receiver_expr << ".write("
      << expr(s.args[0], d, locals, &types) << ", &("
      << expr(s.args[1], d, locals, &types) << "));\n";
    return true;
  }
  if (s.b == "open" && s.args.size() == 2) {
    o << pad << receiver_expr << ".open_in_place(&("
      << expr(s.args[0], d, locals, &types) << "), "
      << rust_string_literal(trim(s.args[1])) << ");\n";
    return true;
  }
  if (s.b == "sync" && s.args.size() <= 1) {
    bool dataonly = !s.args.empty() && trim(s.args[0]) == "dataonly";
    o << pad << receiver_expr << (dataonly ? ".sync_dataonly();\n" : ".sync();\n");
    return true;
  }
  if (s.b == "close" && s.args.empty()) {
    o << pad << receiver_expr << ".close();\n";
    return true;
  }
  throw std::runtime_error("internal error: unchecked FileIO operation '" + s.b + "'");
}

bool gen_executor_or_fileio_statement(std::ostringstream& o, const Stmt& s,
                                      int spaces, const Domain* d,
                                      std::set<string>& locals,
                                      std::unordered_map<string,string>& types) {
  return gen_executor_statement(o, s, spaces, locals, types) ||
      gen_fileio_statement(o, s, spaces, d, locals, types);
}

// Plain sequential reference loop (docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md
// sec. 20.1): used directly for an ineligible pipeline, and as the worked
// example the bounded-K lowering below must remain equivalent to.
string gen_chunk_pipeline_sequential(
    const FunctionalPipeline::ChunkParallelPlan& plan, const Domain* domain,
    const std::set<string>& locals, const std::unordered_map<string,string>* types,
    bool combine_raises) const {
  size_t id = fileio_chunk_temp_++;
  const string offset = "__moss_chunk_offset_" + std::to_string(id);
  const string size_var = "__moss_chunk_size_" + std::to_string(id);
  const string acc = "__moss_chunk_acc_" + std::to_string(id);
  const string bytes = "__moss_chunk_bytes_" + std::to_string(id);
  const string mapped = "__moss_chunk_mapped_" + std::to_string(id);

  std::ostringstream out;
  out << "{\n";
  out << "        // Moss backend: FileIO CHUNK PIPELINE -- sequential reference\n";
  out << "        // loop (ineligible for Branch lowering: " << plan.eligibility_reason << ")\n";
  out << "        let " << size_var << ": i64 = "
      << expr(plan.chunk_size_expression, domain, locals, types) << ";\n";
  out << "        let mut " << offset << ": i64 = 0;\n";
  out << "        let mut " << acc << " = "
      << expr(plan.initial_expression, domain, locals, types) << ";\n";
  string receiver_place = method_receiver_place(
      plan.fileio_receiver, "read", {offset, size_var}, domain, locals, types);
  out << "        loop {\n";
  out << "            let " << bytes << " = match " << receiver_place
      << ".read_checked(" << offset << ", " << size_var << ") {\n";
  out << "                Ok(bytes) => bytes,\n";
  out << "                Err(error) => return Err(__moss_raise_fileio(error)),\n";
  out << "            };\n";
  out << "            let __moss_chunk_len_" << id << " = " << bytes
      << ".len();\n";
  out << "            if __moss_chunk_len_" << id << " == 0 { break; }\n";
  out << "            let " << mapped << " = "
      << render_functional_callable(
             plan.map_callable, {FunctionalValue{bytes, "Range", false}}, domain,
             locals, types);
  if (!plan.map_invocation_effects.raise_set.empty()) out << "?";
  out << ";\n";
  out << "            " << acc << " = "
      << render_functional_callable(
             plan.combine_callable,
             {FunctionalValue{acc, plan.accumulator_type, false},
              FunctionalValue{mapped, plan.mapped_type, false}},
             domain, locals, types);
  if (combine_raises) out << "?";
  out << ";\n";
  out << "            " << offset << " += __moss_chunk_len_" << id << ";\n";
  out << "            if __moss_chunk_len_" << id << " < " << size_var
      << " { break; }\n";
  out << "        }\n";
  out << "        " << acc << "\n";
  out << "    }";
  return out.str();
}

// Bounded-K read/map Branch lowering for an eligible pipeline (corrective
// items 1-7). Each window opens one Branch join scope, publishes K Branches
// (chunk indices next..next+K-1) *before* joining, joins the scope once, then
// commits the K explicitly indexed result slots strictly in index order: a
// zero-length outcome
// terminates without map/fold, a short nonzero outcome folds exactly once and
// terminates, a full outcome folds and continues. Any later slots in the
// window -- speculative reads/maps past the first terminal -- are discarded.
// The initializer is evaluated before the first publication; combine runs
// only here in the parent, left to right. K comes from the checked
// ChunkParallelPlan, never from the environment.
string gen_chunk_pipeline_bounded_k(
    const FunctionalPipeline::ChunkParallelPlan& plan, const Domain* domain,
    const std::set<string>& locals, const std::unordered_map<string,string>* types,
    bool combine_raises) const {
  size_t id = fileio_chunk_temp_++;
  const string n = std::to_string(id);
  const string size_var = "__moss_chunk_size_" + n;
  const string acc = "__moss_chunk_acc_" + n;
  const string next = "__moss_chunk_next_" + n;
  const string scope = "__moss_chunk_scope_" + n;
  const string slots = "__moss_chunk_slots_" + n;
  const string lane = "__moss_chunk_lane_" + n;
  const string index = "__moss_chunk_index_" + n;
  const string offset = "__moss_chunk_offset_" + n;
  const string slot = "__moss_chunk_slot_" + n;
  const string out_slot = "__moss_chunk_out_" + n;
  const string borrow = "__moss_chunk_borrow_" + n;
  const string bytes = "__moss_chunk_bytes_" + n;
  const string len = "__moss_chunk_len_" + n;
  const string outcome = "__moss_chunk_outcome_" + n;
  const string chunk_outcome = "__MossChunkOutcome_" + n;
  const string label = "'moss_chunk_commit_" + n;
  const string k = std::to_string(plan.window_k);
  const string outcome_type = chunk_outcome + "<" + rust_type(plan.mapped_type) + ">";
  const bool map_raises = !plan.map_invocation_effects.raise_set.empty();

  std::ostringstream out;
  out << "{\n";
  out << "        // Moss backend: FileIO CHUNK PIPELINE -- eligible; bounded K=" << k
      << " read/map Branches per join scope,\n";
  out << "        // ordered parent commit, parent-only left-to-right fold\n";
  out << "        let " << size_var << ": i64 = "
      << expr(plan.chunk_size_expression, domain, locals, types) << ";\n";
  out << "        let mut " << acc << " = "
      << expr(plan.initial_expression, domain, locals, types) << ";\n";
  out << "        let mut " << next << ": i64 = 0;\n";
  out << "        enum " << chunk_outcome << "<T> { Value { index: i64, value: T, len: i64, short: bool }, Raised { index: i64, error: __MossRaised }, Eof { index: i64 } }\n";
  out << "        " << label << ": loop {\n";
  out << "            // The scope belongs to the currently executing Root; the runtime,\n";
  out << "            // not compiler output, supplies that Root's identity.\n";
  out << "            let " << scope << " = branch_scope_new_current();\n";
  out << "            let mut " << slots << ": std::vec::Vec<std::sync::Arc<std::sync::Mutex<Option<"
      << outcome_type << ">>>> = std::vec::Vec::with_capacity(" << k << ");\n";
  out << "            for " << lane << " in 0.." << k << "usize {\n";
  out << "                let " << index << " = " << next
      << ".checked_add(" << lane << " as i64).unwrap_or(i64::MAX);\n";
  out << "                let " << slot << " = std::sync::Arc::new(std::sync::Mutex::new(None));\n";
  out << "                let " << out_slot << " = std::sync::Arc::clone(&" << slot << ");\n";
  out << "                let " << borrow << " = moss_fileio_branch_read_borrow(&("
      << method_receiver_place(plan.fileio_receiver, "read", {size_var}, domain, locals, types) << "));\n";
  out << "                branch_publish(&" << scope << ", move || {\n";
  out << "                    let " << outcome << ": " << outcome_type
      << " = match " << index << ".checked_mul(" << size_var << ") {\n"
      << "                        None => " << chunk_outcome << "::Eof { index: "
      << index << " },\n"
      << "                        Some(" << offset << ") => match " << borrow
      << ".read_checked(" << offset << ", " << size_var << ") {\n";
  out << "                        Err(error) => " << chunk_outcome
      << "::Raised { index: " << index
      << ", error: __moss_raise_fileio(error) },\n";
  out << "                        Ok(" << bytes << ") => { let " << len << " = "
      << bytes << ".len(); if " << len << " == 0 { " << chunk_outcome
      << "::Eof { index: " << index << " } } else { ";
  const string mapped = render_functional_callable(
      plan.map_callable, {FunctionalValue{bytes, "Range", false}}, domain,
      locals, types);
  if (map_raises) {
    out << "match " << mapped << " { Ok(value) => " << chunk_outcome
        << "::Value { index: " << index << ", value, len: " << len
        << ", short: " << len << " < " << size_var << " }, "
        << "Err(error) => " << chunk_outcome << "::Raised { index: "
        << index << ", error } }";
  } else {
    out << chunk_outcome << "::Value { index: " << index << ", value: "
        << mapped << ", len: " << len << ", short: " << len << " < "
        << size_var << " }";
  }
  out << " } },\n";
  out << "                    }\n";
  out << "                    };\n";
  out << "                    *" << out_slot
      << ".lock().unwrap_or_else(|_| std::process::abort()) = Some(" << outcome << ");\n";
  out << "                });\n";
  out << "                " << slots << ".push(" << slot << ");\n";
  out << "            }\n";
  out << "            branch_join(" << scope << ");\n";
  out << "            for (" << lane << ", " << slot << ") in " << slots
      << ".into_iter().enumerate() {\n";
  out << "                let " << outcome << " = " << slot
      << ".lock().unwrap_or_else(|_| std::process::abort()).take()"
      << ".unwrap_or_else(|| std::process::abort());\n";
  out << "                let __moss_chunk_outcome_index = match &" << outcome
      << " { " << chunk_outcome << "::Value { index, .. } | " << chunk_outcome
      << "::Raised { index, .. } | " << chunk_outcome
      << "::Eof { index } => *index };\n";
  out << "                if __moss_chunk_outcome_index != " << next
      << ".checked_add(" << lane
      << " as i64).unwrap_or(i64::MAX) { std::process::abort(); }\n";
  out << "                match " << outcome << " {\n";
  out << "                    " << chunk_outcome
      << "::Raised { error, .. } => return Err(error),\n";
  out << "                    " << chunk_outcome << "::Eof { .. } => break "
      << label << ",\n";
  out << "                    " << chunk_outcome
      << "::Value { value, short, .. } => {\n";
  out << "                        " << acc << " = "
      << render_functional_callable(
             plan.combine_callable,
             {FunctionalValue{acc, plan.accumulator_type, false},
              FunctionalValue{"value", plan.mapped_type, false}},
             domain, locals, types);
  if (combine_raises) out << "?";
  out << ";\n";
  out << "                        if short { break " << label << "; }\n";
  out << "                    }\n                }\n";
  out << "            }\n";
  out << "            " << next << " = " << next << ".checked_add(" << k
      << ").unwrap_or(i64::MAX);\n";
  out << "        }\n";
  out << "        " << acc << "\n";
  out << "    }";
  return out.str();
}

std::optional<string> gen_fileio_chunk_pipeline(
    const ParsedFunctionalPipeline& pipeline, const Domain* domain,
    const std::set<string>& locals, const std::unordered_map<string,string>* types,
    size_t functional_pipeline_id) const {
  string fileio_receiver, size_expression;
  if (!parse_fileio_chunks_source(pipeline.source, fileio_receiver, size_expression))
    return std::nullopt;
  if (!types || !fileio_typed(*types, fileio_receiver)) return std::nullopt;
  const FunctionalPipeline* planned = planned_functional_pipeline(functional_pipeline_id);
  if (!planned || !planned->chunk_plan.is_chunk_pipeline)
    throw std::runtime_error(
        "internal error: unchecked FileIO chunk pipeline (no ChunkParallelPlan)");
  if (!phase21_result_context_)
    throw std::runtime_error(
        "internal error: checked FileIO chunk pipeline lacks a Phase 21 outcome context");
  const auto& plan = planned->chunk_plan;
  const bool combine_raises =
      !planned->nodes[2].effects.raise_set.empty();
  if (!plan.eligible)
    return gen_chunk_pipeline_sequential(
        plan, domain, locals, types, combine_raises);
  // A helper specialization can execute in either ordinary code or a Root.
  // Executor activity alone is insufficient: only a current Root owns Branch
  // scopes. Select at runtime so the same helper preserves source semantics
  // when called from main and uses bounded Branches when called from a Root.
  return "if current_root_id().is_some() { " +
      gen_chunk_pipeline_bounded_k(plan, domain, locals, types,
                                   combine_raises) +
      " } else { " +
      gen_chunk_pipeline_sequential(
          plan, domain, locals, types, combine_raises) + " }";
}
