// Phase 20 Agent D -- compiler-side legality for root-local `FileIO` and
// the structured ChunkParallelPlan for
// `file.chunks(size) |> map(f) |> reduce(initial, combine)` chunk pipelines.
//
// Normative behavior: docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md secs.
// 8-13, 20; docs/ROOT_RUNTIME_ABI.md ("FileIO runtime ABI",
// "Branch publish/join API").
//
// Scope boundary (deliberate, see AGENTS.md corrective-pass task): this
// implements FileIO/Range *compiler typing* (open/read/write/sync/close/
// chunks) and the checked chunk-pipeline plan. It does not implement
// domain-field FileIO lock integration (R14), Range/RangeBatch borrow
// scoping (secs. 9.3, 12.6), batch reads (sec. 12.5), or the general FileIO
// ownership/escape checker (R13) -- those belong to Agent A/B. `FileIO` and
// `Range` are compiler-recognized pinned-capability forms outside
// phase20_allowed_as_ordinary_parameter's narrow allowance (see
// moss.cpp's shared Phase 20 capability adapter, used identically by
// executor_invoke_lowering.inc): they may be borrowed by an ordinary
// synchronous helper function, but cannot be stored, returned, or cross a
// message/reply/executor.invoke/domain-state/collection boundary
// (phase20_is_nontransferable_capability).
//
// `map`/`combine` eligibility for the bounded-K Branch lowering
// (file_chunk_codegen.inc) reuses the existing per-stage purity/effect
// analysis (ObservableEffects::fusion_safe) plus the explicit
// ObservableEffects::fileio fact, set for every FileIO operation directly or
// transitively through helpers (a helper may legally borrow a FileIO
// parameter or open its own root-local FileIO). A FileIO-reaching map or
// combine is still valid Moss; it simply lowers sequentially.
//
// A source-free provider function's .mossi interface carries no FileIO fact,
// so it is marked ObservableEffects::fileio_unknown and never assumed
// FileIO-free: a map or combine reaching one lowers sequentially.
//
// This FileIO recognition is provisional and not authoritative. It only
// chooses between Branch and sequential lowering and must never short-circuit
// Agent A's checker; at integration A's effect facts feed fileio directly.

// Builds the structured ChunkParallelPlan for a checked pipeline whose
// source is `<fileio-local>.chunks(size)`. Returns false (plan left
// default-constructed) for any other pipeline.
bool build_fileio_chunk_parallel_plan(
    FunctionalPipeline::ChunkParallelPlan& plan,
    const ParsedFunctionalPipeline& parsed, const TypeEnv& env) const {
  string fileio_receiver, size_expression;
  if (!parse_fileio_chunks_source(parsed.source, fileio_receiver, size_expression) ||
      !fileio_typed(env, fileio_receiver))
    return false;
  plan.is_chunk_pipeline = true;
  plan.fileio_receiver = fileio_receiver;
  plan.chunk_size_expression = size_expression;
  if (parsed.stages.size() != 2 ||
      parsed.stages[0].kind != FunctionalNodeKind::Map ||
      parsed.stages[0].arguments.size() != 1 ||
      parsed.stages[1].kind != FunctionalNodeKind::Reduce ||
      parsed.stages[1].arguments.size() != 2)
    return true;  // shape error raised with a real line by the caller.
  plan.map_callable = parsed.stages[0].arguments.front();
  plan.initial_expression = parsed.stages[1].arguments[0];
  plan.combine_callable = parsed.stages[1].arguments[1];
  plan.accumulator_type = canonical_type_name(
      inferred_expr_type(plan.initial_expression, env).value_or(""));
  plan.mapped_type = canonical_type_name(
      functional_callable_result(plan.map_callable, {"range"}, env).value_or(""));
  return true;
}

// Validates and annotates a FileIO chunk pipeline in place once the generic
// FunctionalPipeline/FunctionalNode IR has been built for it. A no-op for
// any pipeline whose source is not `<fileio-local>.chunks(size)`.
void annotate_fileio_chunk_pipeline(FunctionalPipeline& pipeline,
                                    const ParsedFunctionalPipeline& parsed,
                                    const TypeEnv& env) const {
  if (!build_fileio_chunk_parallel_plan(pipeline.chunk_plan, parsed, env)) return;
  if (parsed.stages.size() != 2 ||
      parsed.stages[0].kind != FunctionalNodeKind::Map ||
      parsed.stages[1].kind != FunctionalNodeKind::Reduce ||
      pipeline.nodes.size() != 3)
    err(pipeline.line,
        "a FileIO chunk pipeline must be exactly "
        "'file.chunks(size) |> map(f) |> reduce(initial, combine)'",
        "FILEIO_CHUNK_PIPELINE_SHAPE");
  // This is not an ordinary materializable-vector pipeline: it has its own
  // lowering (file_chunk_codegen.inc), never the generic fused/eager vector
  // lowering.
  pipeline.fusion_eligible = false;
  pipeline.element_independent = false;
  pipeline.deterministic = false;
  const ObservableEffects& map_effects = pipeline.nodes[1].effects;
  const ObservableEffects& combine_effects = pipeline.nodes[2].effects;
  // Eligibility is purely static, never a function of whether an Executor
  // happens to be active in `main` (corrective-pass item 23):
  //  - map must be pure w.r.t. Moss/domain state (fusion_safe) and must not
  //    reach FileIO (the explicit ObservableEffects::fileio fact, which
  //    fusion_safe also excludes);
  //  - map must capture nothing: a Branch body is `FnOnce() + Send +
  //    'static` work that may run on another worker, so it may only carry
  //    owned values (the chunk index, size, an owned FileIO read-borrow
  //    token, and its result slot), never a borrow of a parent local;
  //  - combine runs only in the parent Root and may have ordinary parent/
  //    domain effects, but must not reach FileIO (sec. 13.2).
  const auto& map_node = pipeline.nodes[1];
  string reason;
  if (map_effects.fileio)
    reason = "map stage reaches a FileIO operation";
  else if (map_effects.fileio_unknown)
    reason = "map callable FileIO effect unavailable for compiled provider";
  else if (!map_effects.fusion_safe())
    reason = "map stage is not pure: " + observable_barrier(map_effects);
  else if (!map_node.captures.empty() ||
           (!starts_with(map_node.callable_identity, "placeholder:") &&
            map_node.callable_identity.find('.') != string::npos))  // bound method
    reason = "map stage captures parent-root locals; Branch work must be "
             "Send + 'static";
  else if (combine_effects.fileio)
    reason = "combine stage reaches a FileIO operation";
  else if (combine_effects.fileio_unknown)
    reason = "combine callable FileIO effect unavailable for compiled provider";
  bool eligible = reason.empty();
  pipeline.chunk_plan.eligible = eligible;
  pipeline.chunk_plan.window_k = 4;
  pipeline.optimization_notes.clear();
  if (!eligible) {
    pipeline.chunk_plan.eligibility_reason = reason;
    pipeline.decision = "sequential (ineligible for Branch lowering): " + reason;
    pipeline.optimization_notes.push_back(reason);
  } else {
    pipeline.chunk_plan.eligibility_reason =
        "pure map, no FileIO call in map or combine, combine runs parent-only";
    pipeline.decision =
        "eligible: bounded K=" + std::to_string(pipeline.chunk_plan.window_k) +
        " read/map Branch lowering (branch_scope_new_current/branch_publish/branch_join), "
        "ordered parent commit and fold";
    pipeline.optimization_notes.push_back(pipeline.chunk_plan.eligibility_reason);
  }
}
