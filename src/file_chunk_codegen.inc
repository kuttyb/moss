// Phase 20 Agent D -- native Rust codegen for root-local FileIO statements
// and for `file.chunks(size) |> map(f) |> reduce(initial, combine)` chunk
// pipelines. See file_chunk_lowering.inc for the checker-side ChunkParallelPlan
// and docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md secs. 8-13, 20 for the
// normative sequential reference semantics every lowering here must match.
//
// Agent D owns this compiler-side lowering, not a FileIO or Branch runtime:
//  - FileIO statements emit calls against the normative Rust-facing names
//    reserved in docs/ROOT_RUNTIME_ABI.md ("FileIO", "Range"). Agent B's
//    crate defines those types; Agent D only assumes their existence. A
//    standalone compile of a FileIO-using program is therefore expected to
//    require Agent B's runtime at link time -- that is an acceptable,
//    documented integration-time dependency (not hidden by a substitute).
//  - An *eligible* chunk pipeline (ChunkParallelPlan::eligible) lowers to the
//    real bounded-K read/map Branch algorithm against the Branch abstraction
//    (`branch_publish`/`branch_join`, src/branch_runtime.hpp) -- publish up to
//    K indexed branches ahead, commit strictly in ascending index order, fold
//    left-to-right in the parent only, discard every later speculative
//    outcome once an earlier terminal (short/zero-length) outcome commits.
//    `branch_publish`'s only implementation in this tree runs each branch
//    inline (E4/sec. 7.3's normative fallback), so this always executes
//    sequentially today, but the *generated code* does not know or care --
//    swapping in Agent C's real worker-thread branch_publish/branch_join
//    changes nothing here.
//  - An *ineligible* pipeline (impure map, map message/domain-state touch, or
//    a FileIO call inside map/combine) never uses the Branch abstraction at
//    all: wrapping unsafe work in a closure that merely *looks*
//    branch-publishable would be a latent correctness hazard once Agent C's
//    branch_publish can really hand work to another thread. It lowers to the
//    plain sequential reference loop instead, matching the ineligible but
//    still-valid-Moss requirement.

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
      types[s.a] = "fileio";
      return true;
    }
    string receiver, method;
    vector<string> arguments;
    if (parse_member_call(s.b, receiver, method, arguments) &&
        fileio_typed(types, receiver) && method == "read" &&
        arguments.size() == 2) {
      o << pad << "let " << s.a << " = " << expr(receiver, d, locals, &types)
        << ".read(" << expr(arguments[0], d, locals, &types) << ", "
        << expr(arguments[1], d, locals, &types) << ");\n";
      locals.insert(s.a);
      types[s.a] = "range";
      return true;
    }
    return false;
  }
  if (s.kind != Stmt::Kind::Call || !fileio_typed(types, s.a)) return false;
  string receiver_expr = expr(s.a, d, locals, &types);
  if (s.b == "write" && s.args.size() == 2) {
    o << pad << receiver_expr << ".write("
      << expr(s.args[0], d, locals, &types) << ", &("
      << expr(s.args[1], d, locals, &types) << "));\n";
    return true;
  }
  if (s.b == "sync" && s.args.size() <= 1) {
    bool dataonly = !s.args.empty() && trim(s.args[0]) == "dataonly";
    o << pad << receiver_expr << ".sync(" << (dataonly ? "true" : "false") << ");\n";
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
    const std::set<string>& locals, const std::unordered_map<string,string>* types) const {
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
  out << "        loop {\n";
  out << "            let " << bytes << " = "
      << expr(plan.fileio_receiver, domain, locals, types) << ".read("
      << offset << ", " << size_var << ");\n";
  out << "            let __moss_chunk_len_" << id << " = " << bytes
      << ".length();\n";
  out << "            if __moss_chunk_len_" << id << " == 0 { break; }\n";
  out << "            let " << mapped << " = "
      << render_functional_callable(
             plan.map_callable, {FunctionalValue{bytes, "range", false}}, domain,
             locals, types)
      << ";\n";
  out << "            " << acc << " = "
      << render_functional_callable(
             plan.combine_callable,
             {FunctionalValue{acc, plan.accumulator_type, false},
              FunctionalValue{mapped, plan.mapped_type, false}},
             domain, locals, types)
      << ";\n";
  out << "            " << offset << " += __moss_chunk_len_" << id << ";\n";
  out << "            if __moss_chunk_len_" << id << " < " << size_var
      << " { break; }\n";
  out << "        }\n";
  out << "        " << acc << "\n";
  out << "    }";
  return out.str();
}

// Bounded-K read/map Branch lowering for an eligible pipeline. Publishes up
// to K indexed branches ahead of the commit point, commits strictly in
// ascending index order, folds left-to-right in the parent only, and
// discards every later speculative outcome once an earlier terminal
// (short/zero-length) outcome has been committed -- the exact algorithm in
// AGENTS.md corrective-pass item 16, independent of whether branch_publish
// is backed by inline execution (this tree) or a real worker pool (Agent C).
string gen_chunk_pipeline_bounded_k(
    const FunctionalPipeline::ChunkParallelPlan& plan, const Domain* domain,
    const std::set<string>& locals, const std::unordered_map<string,string>* types) const {
  size_t id = fileio_chunk_temp_++;
  const string size_var = "__moss_chunk_size_" + std::to_string(id);
  const string k_var = "__moss_chunk_k_" + std::to_string(id);
  const string acc = "__moss_chunk_acc_" + std::to_string(id);
  const string next_index = "__moss_chunk_next_" + std::to_string(id);
  const string window = "__moss_chunk_window_" + std::to_string(id);
  const string index = "__moss_chunk_index_" + std::to_string(id);
  const string bytes = "__moss_chunk_bytes_" + std::to_string(id);
  const string len = "__moss_chunk_len_" + std::to_string(id);
  const string mapped = "__moss_chunk_mapped_" + std::to_string(id);
  const string outcome = "__moss_chunk_outcome_" + std::to_string(id);
  const string slot = "__moss_chunk_slot_" + std::to_string(id);

  std::ostringstream out;
  out << "{\n";
  out << "        // Moss backend: FileIO CHUNK PIPELINE -- eligible for Branch\n";
  out << "        // lowering; bounded-K read/map, ordered commit, parent-only fold\n";
  out << "        let " << size_var << ": i64 = "
      << expr(plan.chunk_size_expression, domain, locals, types) << ";\n";
  out << "        let " << k_var << ": usize = std::env::var(\"MOSS_CHUNK_WINDOW_K\").ok()\n"
      << "            .and_then(|__v| __v.parse().ok()).filter(|__k: &usize| *__k > 0).unwrap_or(4);\n";
  out << "        let mut " << acc << " = "
      << expr(plan.initial_expression, domain, locals, types) << ";\n";
  out << "        let mut " << next_index << ": i64 = 0;\n";
  out << "        let mut " << window << " = std::collections::VecDeque::new();\n";
  out << "        'moss_chunk_commit: loop {\n";
  out << "            while " << window << ".len() < " << k_var << " {\n";
  out << "                let " << index << " = " << next_index << ";\n";
  out << "                " << next_index << " += 1;\n";
  out << "                let " << outcome << " = branch_publish(|| {\n";
  out << "                    let " << bytes << " = "
      << expr(plan.fileio_receiver, domain, locals, types) << ".read("
      << index << " * " << size_var << ", " << size_var << ");\n";
  out << "                    let " << len << " = " << bytes << ".length();\n";
  out << "                    if " << len << " == 0 { (None, 0i64, true) } else {\n";
  out << "                        let " << mapped << " = "
      << render_functional_callable(
             plan.map_callable, {FunctionalValue{bytes, "range", false}}, domain,
             locals, types)
      << ";\n";
  out << "                        (Some(" << mapped << "), " << len << ", "
      << len << " < " << size_var << ")\n";
  out << "                    }\n";
  out << "                });\n";
  out << "                " << window << ".push_back(branch_join(" << outcome << "));\n";
  out << "            }\n";
  out << "            let " << slot << " = " << window
      << ".pop_front().expect(\"internal error: empty chunk Branch window\");\n";
  out << "            if let Some(__moss_chunk_value) = " << slot << ".0 {\n";
  out << "                " << acc << " = "
      << render_functional_callable(
             plan.combine_callable,
             {FunctionalValue{acc, plan.accumulator_type, false},
              FunctionalValue{"__moss_chunk_value", plan.mapped_type, false}},
             domain, locals, types)
      << ";\n";
  out << "            }\n";
  out << "            if " << slot << ".2 { break 'moss_chunk_commit; }\n";
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
  const auto& plan = planned->chunk_plan;
  return plan.eligible ? gen_chunk_pipeline_bounded_k(plan, domain, locals, types)
                       : gen_chunk_pipeline_sequential(plan, domain, locals, types);
}
