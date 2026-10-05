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
// (file_chunk_codegen.inc) is computed by reusing the *existing*
// purity/effect analysis already run for every functional pipeline stage
// (ObservableEffects::fusion_safe): no FileIO-specific effect machinery is
// required for "impure map", "map domain READ/WRITE", or "map message" --
// those are already general properties of any callable (confirmed by the
// phase20_chunk_domain differential test, where a map stage closes over
// domain state inside a handler). A FileIO call from inside a map/combine
// callable is structurally unreachable under this scope's FileIO (FileIO
// cannot be passed into an ordinary helper as anything other than a plain
// borrowed parameter, and a chunk pipeline's map/combine callables are
// resolved as ordinary functions of the chunk element/accumulator types
// only), so that eligibility branch is implemented for completeness but not
// exercised by a valid Moss test program -- called out explicitly rather
// than silently assumed covered.

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
  // Eligibility is purely static (map/combine purity), never a function of
  // whether an Executor happens to be active in `main` (AGENTS.md
  // corrective-pass item 23): the exact same bounded-K Branch lowering is
  // emitted whether or not a runtime ends up actually distributing work.
  bool eligible = map_effects.fusion_safe() && !combine_effects.external_io;
  pipeline.chunk_plan.eligible = eligible;
  pipeline.optimization_notes.clear();
  if (!eligible) {
    string reason = !map_effects.fusion_safe()
        ? "map stage is not eligible for the Branch lowering: " +
              observable_barrier(map_effects)
        : "combine stage makes a FileIO call, which the baseline Phase 20 "
          "lowering forbids";
    pipeline.chunk_plan.eligibility_reason = reason;
    pipeline.decision = "sequential (ineligible for Branch lowering): " + reason;
    pipeline.optimization_notes.push_back(reason);
  } else {
    pipeline.chunk_plan.eligibility_reason =
        "pure map, no FileIO call in map or combine, combine runs parent-only";
    pipeline.decision =
        "eligible for bounded-K Branch lowering (branch_publish/branch_join); "
        "executes inline today because no worker-thread Branch runtime is "
        "wired into this build (Agent C integration pending)";
    pipeline.optimization_notes.push_back(pipeline.chunk_plan.eligibility_reason);
  }
}
