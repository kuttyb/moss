// Phase 20 Agent D -- compiler-side legality for root-local `FileIO` and
// for `file.chunks(size) |> map(f) |> reduce(initial, combine)` chunk
// pipelines.
//
// Normative behavior: docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md secs.
// 8-13, 20; docs/ROOT_RUNTIME_ABI.md ("FileIO runtime ABI",
// "Branch publish/join API").
//
// Scope boundary (deliberate, see AGENTS.md task assignment): this
// implements enough of FileIO for root-local open/read/write/sync/close and
// for chunk-pipeline eligibility/sequential-fallback semantics to be real
// and tested. It does *not* implement domain-field FileIO lock integration
// (R14), Range/RangeBatch borrow scoping (secs. 9.3, 12.6), batch reads
// (sec. 12.5), or the general FileIO ownership/escape checker (R13) --
// those belong to Agent A/B's "general FileIO ownership/effect
// implementation", explicitly out of Agent D's scope. `FileIO` is a
// compiler-recognized pinned-capability form, never a general nominal type
// (see executor_invoke_lowering.inc for the identical rationale), so
// nothing outside the targeted call sites below recognizes the pseudo-type
// string "fileio" -- that alone rejects storing it in domain state, passing
// it as an ordinary function/handler argument, or returning it.
//
// `map`/`reduce` eligibility for the (currently unimplemented, see
// file_chunk_codegen.inc) Agent-C branch runtime is computed by reusing the
// *existing* purity/effect analysis already run for every functional
// pipeline stage (ObservableEffects::fusion_safe, functional_stage_effects):
// no FileIO-specific effect machinery is required for "impure map", "map
// domain READ/WRITE", or "map message" -- those are already general
// properties of any callable. A FileIO call from inside a map/combine
// callable is structurally unreachable under this scope (FileIO is pinned
// and cannot be passed into an ordinary helper function as a parameter), so
// `ObservableEffects::external_io` is checked for completeness/future-
// proofing but cannot currently be exercised by a valid Moss program; this
// is called out explicitly rather than silently assumed covered.

// Validates and annotates a FileIO chunk pipeline in place once the generic
// FunctionalPipeline/FunctionalNode IR has been built for it. A no-op for
// any pipeline whose source is not `<fileio-local>.chunks(size)`.
void annotate_fileio_chunk_pipeline(FunctionalPipeline& pipeline,
                                    const ParsedFunctionalPipeline& parsed,
                                    const TypeEnv& env) const {
  string fileio_receiver, size_expression;
  if (!parse_fileio_chunks_source(parsed.source, fileio_receiver, size_expression) ||
      !fileio_typed(env, fileio_receiver))
    return;
  if (pipeline.nodes.size() != 3 ||
      pipeline.nodes[1].kind != FunctionalNodeKind::Map ||
      pipeline.nodes[2].kind != FunctionalNodeKind::Reduce)
    err(pipeline.line,
        "a FileIO chunk pipeline must be exactly "
        "'file.chunks(size) |> map(f) |> reduce(initial, combine)'",
        "FILEIO_CHUNK_PIPELINE_SHAPE");
  // This is not an ordinary materializable-vector pipeline: it has its own
  // sequential-fallback lowering (file_chunk_codegen.inc), never the generic
  // fused/eager vector lowering.
  pipeline.fusion_eligible = false;
  pipeline.element_independent = false;
  pipeline.deterministic = false;
  const ObservableEffects& map_effects = pipeline.nodes[1].effects;
  const ObservableEffects& combine_effects = pipeline.nodes[2].effects;
  bool eligible = map_effects.fusion_safe() && !combine_effects.external_io;
  pipeline.optimization_notes.clear();
  if (!eligible) {
    string reason = !map_effects.fusion_safe()
        ? "map stage is not eligible for parallel branch lowering: " +
              observable_barrier(map_effects)
        : "combine stage makes a FileIO call, which the baseline Phase 20 "
          "lowering forbids";
    pipeline.decision = "sequential: ineligible for parallel lowering (" + reason + ")";
    pipeline.optimization_notes.push_back(reason);
  } else {
    pipeline.decision =
        "sequential: eligible for parallel branch lowering, but no active "
        "Executor/runtime branch-publish support is integrated yet "
        "(Agent C integration pending)";
    pipeline.optimization_notes.push_back(
        "chunk reads/maps are eligible for compiler-generated branches; "
        "execution remains sequential pending Agent C's runtime");
  }
}
