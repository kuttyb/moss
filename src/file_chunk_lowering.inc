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
// (phase20_capability_type, fileio_semantics.inc).
//
// `map`/`combine` eligibility for the bounded-K Branch lowering
// (file_chunk_codegen.inc) reuses the existing per-stage purity/effect
// analysis (ObservableEffects::chunk_speculation_safe) plus the explicit
// ObservableEffects::fileio fact, set for every FileIO operation directly or
// transitively through helpers (a helper may legally borrow a FileIO
// parameter or open its own root-local FileIO). A FileIO-reaching map or
// combine is still valid Moss; it simply lowers sequentially.
//
// ABI-v8 source-free providers carry explicit FileIO, panic, typed-raise, and
// unresolved facts. Missing metadata fails closed while loading the .mossi;
// an explicitly unknown FileIO fact still forces sequential lowering.
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
      functional_callable_result(plan.map_callable, {"Range"}, env).value_or(""));
  return true;
}

// P21-10A is a call-site proof, not a rewrite of a callable's exported effect
// summary. Recheck only this chunk-map invocation under `len(parameter) >= 1`.
// A named mapper gets the fact only when its Range parameter is never rebound
// or replaced. Placeholder `_` is intrinsically the unchanged invocation
// argument. Delegation remains conservative until a concrete interprocedural
// specialization carries an equivalent fact.
ObservableEffects chunk_mapper_invocation_effects(
    const FunctionalNode& map_node, const TypeEnv& caller_env,
    const std::set<string>& domain_fields,
    const ObjectType* implicit_object,
    bool& used_nonempty_proof) const {
  used_nonempty_proof = false;
  if (starts_with(map_node.callable_identity, "placeholder:")) {
    TypeEnv env = caller_env;
    env["_"] = "Range";
    ProvenIndexAccesses proven{{"_", "0"}};
    ObservableEffects effects = observable_expression_effects(
        map_node.callable_expression, env, domain_fields, implicit_object, {},
        &proven);
    used_nonempty_proof = map_node.effects.may_panic && !effects.may_panic;
    return effects;
  }

  auto function = functions_.find(map_node.callable_identity);
  if (function == functions_.end() || function->second->params.size() != 1)
    return map_node.effects;
  const Function& mapper = *function->second;
  const string parameter = mapper.params.front().name;
  for (const Stmt& statement : mapper.body) {
    if (statement.kind == Stmt::Kind::Assign ||
        statement.kind == Stmt::Kind::Let ||
        statement.kind == Stmt::Kind::Var) {
      if (extract_root_identifier(statement.a) == parameter)
        return map_node.effects;
    }
    for (const auto& expression : statement_expressions(statement)) {
      string callee;
      vector<string> arguments;
      if (parse_simple_call(expression, callee, arguments) &&
          callee == "replace" && !arguments.empty() &&
          extract_root_identifier(arguments.front()) == parameter)
        return map_node.effects;
    }
  }

  TypeEnv env;
  env[parameter] = "Range";
  std::set<string> parameters{parameter};
  ProvenIndexAccesses proven{{parameter, "0"}};
  ObservableEffects effects = observable_body_effects(
      mapper.body, env, parameters, {}, nullptr, &proven);
  if (mapper.result_expression)
    effects.merge(observable_expression_effects(
        *mapper.result_expression, env, {}, nullptr, parameters, &proven));
  used_nonempty_proof = map_node.effects.may_panic && !effects.may_panic;
  return effects;
}

// Validates and annotates a FileIO chunk pipeline in place once the generic
// FunctionalPipeline/FunctionalNode IR has been built for it. A no-op for
// any pipeline whose source is not `<fileio-local>.chunks(size)`.
void annotate_fileio_chunk_pipeline(FunctionalPipeline& pipeline,
                                    const ParsedFunctionalPipeline& parsed,
                                    const TypeEnv& env,
                                    const std::set<string>& domain_fields,
                                    const ObjectType* implicit_object) const {
  if (!build_fileio_chunk_parallel_plan(pipeline.chunk_plan, parsed, env)) return;
  if (parsed.stages.size() != 2 ||
      parsed.stages[0].kind != FunctionalNodeKind::Map ||
      parsed.stages[1].kind != FunctionalNodeKind::Reduce ||
      pipeline.nodes.size() != 3)
    err(pipeline.line,
        "a FileIO chunk pipeline must be exactly "
        "'file.chunks(size) |> map(f) |> reduce(initial, combine)'",
        "FILEIO_CHUNK_PIPELINE_SHAPE");
  if (starts_with(pipeline.context, "handler:")) {
    const string handler = pipeline.context.substr(8);
    const size_t separator = handler.find('.');
    if (separator != string::npos && separator > 0 &&
        separator + 1 < handler.size()) {
      const string owner = handler.substr(0, separator);
      const string operation = handler.substr(separator + 1);
      pipeline.chunk_plan.failure_converter =
          "__moss_raise_fileio_" + owner + "_" + operation;
      pipeline.chunk_plan.body_outcome_type =
          "__MossBodyOutcome_" + owner + "_" + operation;
    }
  }
  // This is not an ordinary materializable-vector pipeline: it has its own
  // lowering (file_chunk_codegen.inc), never the generic fused/eager vector
  // lowering.
  pipeline.fusion_eligible = false;
  pipeline.element_independent = false;
  pipeline.deterministic = false;
  bool used_nonempty_proof = false;
  ObservableEffects map_effects = chunk_mapper_invocation_effects(
      pipeline.nodes[1], env, domain_fields, implicit_object,
      used_nonempty_proof);
  pipeline.chunk_plan.map_invocation_effects = map_effects;
  pipeline.chunk_plan.used_nonempty_chunk_index_proof = used_nonempty_proof;
  const ObservableEffects& combine_effects = pipeline.nodes[2].effects;
  // Eligibility is purely static, never a function of whether an Executor
  // happens to be active in `main` (corrective-pass item 23):
  //  - map must be pure w.r.t. Moss/domain state (chunk_speculation_safe)
  //    and must not panic; typed raises remain legal indexed outcomes and do
  //    not weaken ordinary fusion_safe();
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
  else if (!map_effects.chunk_speculation_safe())
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
    if (used_nonempty_proof)
      pipeline.chunk_plan.eligibility_reason +=
          ", contextual nonempty chunk[0] proof";
    pipeline.decision =
        "eligible: bounded K=" + std::to_string(pipeline.chunk_plan.window_k) +
        " read/map Branch lowering (branch_scope_new_current/branch_publish/branch_join), "
        "ordered parent commit and fold";
    pipeline.optimization_notes.push_back(pipeline.chunk_plan.eligibility_reason);
  }
}
