// Logical domains in Fast Debug use checked concrete identities and ordinary
// interpreter frames. No physical synchronization plan or scheduler is involved.
  Value independent(const Value& source) {
    if (root_ingress_argument_preparation_ &&
        independent_recursion_depth_ == 0) {
      if (explicit_copy_evaluation_)
        ++root_ingress_explicit_independent_calls_;
      else
        ++root_ingress_implicit_independent_calls_;
    }
    struct IndependentRecursionScope {
      std::size_t& depth;
      explicit IndependentRecursionScope(std::size_t& current) : depth(current) {
        ++depth;
      }
      ~IndependentRecursionScope() { --depth; }
    } recursion_scope(independent_recursion_depth_);
    Value result = source;
    if (source.object) {
      result.object = std::make_shared<StructValue>();
      result.object->type = source.object->type;
      result.object->case_name = source.object->case_name;
      for (const auto& field : source.object->fields)
        result.object->fields[field.first] = independent(field.second);
    }
    if (source.vector) {
      result.vector = std::make_shared<std::vector<Value>>();
      for (const auto& value : *source.vector) result.vector->push_back(independent(value));
    }
    if (source.queue) {
      result.queue = std::make_shared<std::vector<Value>>();
      for (const auto& value : *source.queue) result.queue->push_back(independent(value));
    }
    if (source.map) {
      result.map = std::make_shared<std::vector<std::pair<Value, Value>>>();
      for (const auto& entry : *source.map)
        result.map->push_back({independent(entry.first), independent(entry.second)});
      if (source.map_index)
        result.map_index = std::make_shared<Value::HashIndex>(*source.map_index);
      if (source.map_hashes)
        result.map_hashes = std::make_shared<std::vector<std::int64_t>>(*source.map_hashes);
    }
    if (source.set) {
      result.set = std::make_shared<std::vector<Value>>();
      for (const auto& key : *source.set)
        result.set->push_back(independent(key));
      if (source.set_index)
        result.set_index = std::make_shared<Value::HashIndex>(*source.set_index);
    }
    return result;
  }

  static std::string summary(const Value& value) {
    if (value.kind == Value::Kind::Struct) return value.object ? value.object->type : "<struct>";
    if (value.kind == Value::Kind::Vector)
      return "Vector(size=" + std::to_string(value.vector ? value.vector->size() : 0) + ")";
    if (value.kind == Value::Kind::Queue)
      return "Queue(size=" + std::to_string(value.queue ? value.queue->size() : 0) + ")";
    if (value.kind == Value::Kind::Map)
      return "Map(size=" + std::to_string(value.map ? value.map->size() : 0) + ")";
    if (value.kind == Value::Kind::Set)
      return "Set(size=" + std::to_string(value.set ? value.set->size() : 0) + ")";
    return value.display().substr(0, 256);
  }

  void state_event(const std::string& kind, const Frame& frame, int line,
                   const Place& place, std::string before = {}, std::string after = {}) {
    if (!options_.trace) return;
    emit(kind, frame, line);
    auto& event = trace_.back();
    event.instance = place.domain->concrete->identity;
    event.specialization = place.domain->concrete->specialization;
    event.path = place.path; event.before = std::move(before); event.after = std::move(after);
  }

  Place locate(const std::string& text, Frame& frame, int line, std::ostream& output) {
    std::string expression = trim_copy(text);
    std::vector<est::ExprId> checked_indices;
    if (active_expression_tree_ &&
        trim_copy(std::string(expression_node_text(
            *active_expression_tree_, active_expression_node_))) == expression) {
      est::ExprId base = active_expression_node_;
      while (const auto* index = std::get_if<est::Index>(
                 &active_expression_tree_->arena->at(base))) {
        checked_indices.push_back(index->index);
        base = index->base;
      }
      std::reverse(checked_indices.begin(), checked_indices.end());
    }
    std::size_t checked_index = 0;
    // Only checked lvalue-shaped expressions participate in location lookup.
    size_t end = 0;
    while (end < expression.size() && (std::isalnum(static_cast<unsigned char>(expression[end])) || expression[end] == '_')) ++end;
    if (!end || !binding_identifier(expression.substr(0, end))) return {};
    std::string root = expression.substr(0, end);
    Place place;
    auto alias = frame.aliases.find(root);
    auto local = frame.locals.find(root);
    if (alias != frame.aliases.end()) place = alias->second;
    else if (local != frame.locals.end()) place.value = &local->second;
    else if (frame.handler_scope && frame.domain && frame.domain->state.count(root))
      place = {&frame.domain->state.at(root), frame.domain, root};
    else return {};
    while (end < expression.size()) {
      while (end < expression.size() &&
             std::isspace(static_cast<unsigned char>(expression[end]))) ++end;
      if (end == expression.size()) break;
      if (expression[end] == '.') {
        size_t start = ++end;
        while (end < expression.size() && (std::isalnum(static_cast<unsigned char>(expression[end])) || expression[end] == '_')) ++end;
        if (start == end || (end < expression.size() && expression[end] == '(')) return {};
        auto name = expression.substr(start, end - start);
        if (!place.value->object || !place.value->object->fields.count(name)) return {};
        place.value = &place.value->object->fields.at(name);
        if (place.domain) place.path += "." + name;
      } else if (expression[end] == '[') {
        size_t start = ++end; int depth = 1;
        while (end < expression.size() && depth) {
          if (expression[end] == '[') ++depth;
          if (expression[end] == ']') --depth;
          if (depth) ++end;
        }
        if (depth) return {};
        Value index;
        if (checked_index < checked_indices.size())
          index = evaluate_tree_node(*active_expression_tree_,
              checked_indices[checked_index++], frame, line, output);
        else
          index = evaluate_expression_source(
              expression.substr(start, end - start), frame, line, output);
        if (place.value->map) {
          bool found = false;
          for (auto& entry : *place.value->map) {
            if (protocol_key_equal(entry.first, index, frame, line, output)) {
              place.value = &entry.second;
              found = true;
              break;
            }
          }
          if (!found) throw RuntimeError(line, "missing map key");
        } else {
          auto values = place.value->vector ? place.value->vector : place.value->queue;
          if (!values || index.kind != Value::Kind::Int || index.integer < 0 ||
              static_cast<size_t>(index.integer) >= values->size())
            throw RuntimeError(line, "vector index out of bounds");
          place.value = &values->at(static_cast<size_t>(index.integer));
          if (place.domain) place.path += "[" + std::to_string(index.integer) + "]";
        }
        ++end;
      } else return {};
    }
    return place;
  }

  void bind_parameters(const std::vector<Param>& parameters, const std::vector<Effect>& effects,
      const std::vector<std::string>& arguments, Frame& caller, Frame& frame, int line,
      std::ostream& output, const std::vector<std::string>* concrete_types = nullptr,
      const ExpressionTreeView* tree = nullptr,
      const std::vector<est::ExprId>* argument_nodes = nullptr,
      const std::vector<Value>* argument_values = nullptr,
      const std::vector<SpecId>* argument_callable_spec_ids = nullptr) {
    frame.domain = caller.domain; frame.handler = caller.handler;
    if (argument_callable_spec_ids &&
        argument_callable_spec_ids->size() != arguments.size())
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT checked callable argument SpecId arity mismatch");
    // Domain context is trace provenance only in helpers; state names remain
    // lexically resolved through explicit actual/formal bindings.
    for (size_t i = 0; i < arguments.size(); ++i) {
      std::string type = concrete_types && i < concrete_types->size()
          ? (*concrete_types)[i] : parameters[i].type;
      if ((type.empty() || internal_semantic_placeholder(type)) && binding_identifier(trim_copy(arguments[i]))) {
        auto caller_type = caller.local_types.find(trim_copy(arguments[i]));
        if (caller_type != caller.local_types.end()) type = caller_type->second;
      }
      if (!type.empty()) frame.local_types[parameters[i].name] = type;
      Effect effect = i < effects.size() ? effects[i] : Effect::Read;
      auto bind_callable_id = [&](Value& value) {
        const SpecId id = argument_callable_spec_ids &&
                i < argument_callable_spec_ids->size()
            ? argument_callable_spec_ids->at(i) : InvalidSpecId;
        if (value.kind != Value::Kind::Callable) {
          if (id != InvalidSpecId)
            throw RuntimeError(line,
                "MOSS_INTERNAL_INVARIANT non-callable argument has a callable SpecId");
          return;
        }
        if (!value.callable_target || id == InvalidSpecId ||
            id >= program_.specialization_table.size())
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT callable argument lacks checked SpecId authority");
        const auto& identity = program_.specialization_table[id];
        if (identity.id != id ||
            identity.semantic_callable_identity !=
                value.callable_target->semantic_callable_id)
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT callable argument SpecId is contradictory");
        std::size_t specialization_count = 0;
        for (const auto& candidate : value.callable_target->specializations)
          specialization_count += candidate.spec_id == id;
        if (value.callable_target->static_dispatch
                ? specialization_count != 1
                : (specialization_count != 0 ||
                   value.callable_target->spec_id != id))
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT callable argument SpecId does not identify a checked specialization");
        value.callable_spec_id = id;
      };
      if (argument_values && i < argument_values->size()) {
        frame.locals[parameters[i].name] = independent(argument_values->at(i));
        bind_callable_id(frame.locals[parameters[i].name]);
        continue;
      }
      const bool has_node = tree && argument_nodes &&
          i < argument_nodes->size();
      auto place = has_node
          ? locate_tree_node(*tree, argument_nodes->at(i), caller, line, output)
          : locate(arguments[i], caller, line, output);
      if (effect != Effect::Consume && place.value) frame.aliases[parameters[i].name] = place;
      else {
        frame.locals[parameters[i].name] = has_node
            ? evaluate_tree_node(*tree, argument_nodes->at(i), caller, line,
                                 output)
            : evaluate_expression_source(arguments[i], caller, line, output);
        bind_callable_id(frame.locals[parameters[i].name]);
        if (effect == Effect::Consume && place.value) {
          if (place.domain) state_event("state_consume", caller, line, place, summary(*place.value), "<moved>");
          *place.value = Value::unit();
        }
      }
    }
  }

  void prepare_domains() {
    instances_.clear();
    for (const auto& concrete : program_.concrete_domain_graph.instances) {
      if (concrete.source_domain_index >= program_.domains.size() || !concrete.specialization_index ||
          *concrete.specialization_index >= program_.domain_specializations.size())
        throw RuntimeError(concrete.line, "internal error: invalid checked concrete domain identity");
      auto instance = std::make_unique<DomainValue>();
      instance->concrete = &concrete;
      instance->definition = &program_.domains.at(concrete.source_domain_index);
      instances_.emplace(concrete.identity, std::move(instance));
    }
    for (const auto& edge : program_.concrete_domain_graph.edges)
      instances_.at(edge.source_instance)->routes.emplace(edge.route, instances_.at(edge.target_instance).get());
  }

  Value default_semantic_value(const std::string& type, Frame& frame, int line, std::ostream& output) {
    if (type == "Int" || type == "int") return Value::int_value(0);
    if (type == "Float" || type == "float") return Value::float_value(0);
    if (type == "Bool" || type == "bool") return Value::boolean_value(false);
    if (type == "Char" || type == "char") return Value::char_value(0);
    if (type == "String" || type == "string") return Value::string_value("");
    if (type.rfind("vector", 0) == 0 || type.rfind("Vector", 0) == 0) {
      auto open = type.find('[');
      auto close = type.rfind(']');
      return Value::vector_value({}, open != std::string::npos && close > open
                                      ? type.substr(open + 1, close - open - 1)
                                      : std::string{});
    }
    if (type.rfind("queue", 0) == 0 || type.rfind("Queue", 0) == 0) return Value::queue_value();
    if (type.rfind("map", 0) == 0 || type.rfind("Map", 0) == 0) return Value::map_value();
    if (auto object = object_type(type)) return construct(*object, {}, frame, line, output);
    throw RuntimeError(line, "Fast Debug cannot initialize state type '" + type + "'");
  }

  bool compose(const Stmt& statement, Frame& frame, std::ostream& output) {
    if (frame.function != "main") return false;
    for (auto& entry : instances_) {
      auto& instance = *entry.second;
      if (instance.concrete->binding != statement.a || instance.concrete->line != statement.line) continue;
      if (instance.initialized) throw RuntimeError(statement.line, "internal error: duplicate domain initialization");
      const auto constructor_slot = statement.kind == Stmt::Kind::Assign
          ? est::ExprSlot::RhsValue : est::ExprSlot::Initializer;
      auto constructor_tree = expression_tree(statement.b,
          "compose_domain_constructor",
          std::pair{constructor_slot, 0u});
      if (!constructor_tree)
        throw RuntimeError(statement.line,
                           "internal error: missing domain constructor");
      const auto& specialization = program_.domain_specializations.at(*instance.concrete->specialization_index);
      std::unordered_map<std::string, est::ExprId> initializers;
      const auto& constructor_node =
          constructor_tree->arena->at(constructor_tree->root);
      const std::vector<est::ExprId>* argument_nodes = nullptr;
      if (const auto* call = std::get_if<est::Call>(&constructor_node))
        argument_nodes = &call->arguments;
      else if (const auto* constructor =
                   std::get_if<est::Constructor>(&constructor_node))
        argument_nodes = &constructor->arguments;
      if (!argument_nodes)
        throw RuntimeError(statement.line,
                           "MOSS_INTERNAL_INVARIANT missing checked domain constructor arguments");
      for (const auto argument : *argument_nodes) {
        const auto* named = std::get_if<est::NamedArgument>(
            &constructor_tree->arena->at(argument));
        if (!named)
          throw RuntimeError(statement.line,
                             "internal error: unnamed state initializer");
        initializers.emplace(named->name, named->value);
      }
      for (const auto& field : instance.definition->state) {
        auto explicit_value = initializers.find(field.name);
        auto type = specialization.state_types.find(field.name);
        ExpressionHandlesScope field_handles_scope(
            active_expression_handles_, &field.expression_handles);
        instance.state[field.name] = explicit_value != initializers.end() ? evaluate_tree_node(*constructor_tree, explicit_value->second, frame, statement.line, output)
            : !field.init.empty() ? evaluate_expression_source(
                  field.init, frame, field.line, output,
                  std::pair{est::ExprSlot::Initializer, 0u})
            : default_semantic_value(type == specialization.state_types.end() ? field.type : type->second, frame, statement.line, output);
      }
      instance.initialized = true;
      Value handle; handle.kind = Value::Kind::DomainHandle; handle.domain = &instance;
      frame.locals[statement.a] = handle;
      Frame event = frame; event.domain = &instance;
      emit("domain_instance", event, statement.line);
      return true;
    }
    return false;
  }

  // A checked handler call whose argument snapshots have already been
  // evaluated. Synchronous `message` evaluates and runs immediately; Phase 20
  // Fast Debug `executor.invoke` evaluates at submission and runs at join
  // (see interpreter_phase20.inc).
  struct PreparedHandlerCall {
    DomainValue* instance = nullptr;
    const Handler* target = nullptr;
    std::vector<Value> arguments;
    int line = 0;
  };

  std::string resolve_root_ingress_argument_authority(
      const ExpressionTreeView& tree, const est::Message& message_node,
      est::ExprId message_expression, std::uint32_t argument_index,
      DomainValue& instance, const Handler& target, Frame& caller,
      int line, RootIngressCopyAuthorityOccurrence* copy_authority) const {
    const Stmt* statement = active_statement_;
    if (!statement || !statement->message_root_ingress ||
        statement->checked_statement_occurrence_id == UINT32_MAX ||
        !program_.typed_semantics || !tree.arena ||
        tree.arena_index >= program_.expression_arenas.size() ||
        !program_.expression_arenas[tree.arena_index] ||
        program_.expression_arenas[tree.arena_index].get() != tree.arena ||
        message_expression >= tree.arena->size() ||
        argument_index >= message_node.arguments.size())
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT Root-ingress argument query identity unavailable");

    if (!program_.main || caller.spec_id != program_.main->spec_id)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT Root-ingress argument context is not main");
    est::ExprId exact_message_expression = message_expression;
#ifdef MOSS_TEST_BUILD
    if (std::getenv(
            "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_NESTED_EXPR_ID") &&
        tree.root != exact_message_expression)
      ++exact_message_expression;
#endif
    if (exact_message_expression >= tree.arena->size())
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT Root-ingress message ExprId is unavailable");
    const auto* checked_message = std::get_if<est::Message>(
        &tree.arena->at(exact_message_expression));
    if (!checked_message || checked_message->handler != message_node.handler ||
        checked_message->target != message_node.target ||
        argument_index >= checked_message->arguments.size())
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT Root-ingress argument does not match exact EST message");
    const auto argument_expression = est::unparenthesized(
        *tree.arena, checked_message->arguments[argument_index]);

    est::ExprId owner_expression = tree.root;
#ifdef MOSS_TEST_BUILD
    if (std::getenv("MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_OWNER"))
      ++owner_expression;
#endif
    const est::Handle* message_handle = nullptr;
    std::size_t matching_handles = 0;
    for (const auto& handle : statement->expression_handles) {
      if (handle.arena_index != tree.arena_index ||
          handle.source_fingerprint == 0 || handle.expr >= tree.arena->size() ||
          handle.expr != owner_expression)
        continue;
      message_handle = &handle;
      ++matching_handles;
    }
    if (matching_handles != 1 || !message_handle)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT Root-ingress statement/message handle missing or ambiguous");

    const std::string handler_identity = "handler:" +
        instance.definition->name + "." + target.name + "@" +
        std::to_string(target.line);
    const TypedRootIngressDomainCaptureRequirementV1* capture_requirement =
        nullptr;
    std::size_t capture_requirement_count = 0;
    for (const auto& candidate : program_.typed_semantics
             ->required_root_ingress_domain_capture_occurrences)
      if (candidate.statement_occurrence_id ==
              statement->checked_statement_occurrence_id &&
          candidate.est_arena_index == tree.arena_index &&
          candidate.message_expression_occurrence_id == exact_message_expression &&
          candidate.source_fingerprint == message_handle->source_fingerprint &&
          candidate.origin_id == message_handle->origin_id &&
          candidate.specialization_id == caller.spec_id) {
        capture_requirement = &candidate;
        ++capture_requirement_count;
      }
    if (capture_requirement_count != 1 || !capture_requirement)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT exact Root-ingress domain capture requirement missing or ambiguous");
    const TypedRootIngressDomainCaptureFactV1* capture = nullptr;
    try {
      capture = &program_.typed_semantics
          ->find_root_ingress_domain_capture_fact(*capture_requirement);
    } catch (const std::exception& error) {
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT " + std::string(error.what()));
    }
    if (!capture || capture->selected_handler_identity != handler_identity ||
        capture->source_fingerprint != message_handle->source_fingerprint ||
        capture->origin_id != message_handle->origin_id)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT Root-ingress checked handler identity mismatch");

    const TypedRootIngressArgumentTypeRequirementV1* argument_type_requirement =
        nullptr;
    std::size_t argument_type_requirements = 0;
    std::size_t argument_type_facts = 0;
    for (const auto& candidate : program_.typed_semantics
             ->required_root_ingress_argument_type_occurrences)
      if (candidate.statement_occurrence_id ==
              statement->checked_statement_occurrence_id &&
          candidate.est_arena_index == tree.arena_index &&
          candidate.owner_expression_occurrence_id == owner_expression &&
          candidate.message_expression_occurrence_id ==
              exact_message_expression &&
          candidate.argument_index == argument_index &&
          candidate.argument_expression_occurrence_id == argument_expression &&
          candidate.source_fingerprint == message_handle->source_fingerprint &&
          candidate.selected_handler_identity == handler_identity &&
          candidate.specialization_id == caller.spec_id) {
        argument_type_requirement = &candidate;
        ++argument_type_requirements;
      }
    for (const auto& candidate : program_.typed_semantics
             ->root_ingress_argument_type_facts)
      if (candidate.statement_occurrence_id ==
              statement->checked_statement_occurrence_id &&
          candidate.est_arena_index == tree.arena_index &&
          candidate.owner_expression_occurrence_id == owner_expression &&
          candidate.message_expression_occurrence_id ==
              exact_message_expression &&
          candidate.argument_index == argument_index &&
          candidate.argument_expression_occurrence_id == argument_expression &&
          candidate.source_fingerprint == message_handle->source_fingerprint &&
          candidate.selected_handler_identity == handler_identity &&
          candidate.specialization_id == caller.spec_id)
        ++argument_type_facts;
#ifdef MOSS_TEST_BUILD
    if (std::getenv(
            "MOSS_TEST_INTERPRETER_DROP_ROOT_INGRESS_ARGUMENT_TYPE_REQUIREMENT"))
      --argument_type_requirements;
    if (std::getenv(
            "MOSS_TEST_INTERPRETER_DUPLICATE_ROOT_INGRESS_ARGUMENT_TYPE_REQUIREMENT"))
      ++argument_type_requirements;
    if (std::getenv("MOSS_TEST_INTERPRETER_DROP_ROOT_INGRESS_ARGUMENT_TYPE_FACT"))
      argument_type_facts = 0;
    if (std::getenv(
            "MOSS_TEST_INTERPRETER_DUPLICATE_ROOT_INGRESS_ARGUMENT_TYPE_FACT"))
      ++argument_type_facts;
#endif
    if (argument_type_requirements != 1 || argument_type_facts != 1 ||
        !argument_type_requirement)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT Root-ingress argument type requirement/fact missing or ambiguous");
    const TypedRootIngressArgumentTypeFactV1* checked_argument_type = nullptr;
    try {
#ifdef MOSS_TEST_BUILD
      if (std::getenv(
              "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_ARGUMENT_TYPE_FACT")) {
        auto corrupted = *argument_type_requirement;
        const std::string field = std::getenv(
            "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_ARGUMENT_TYPE_FACT");
        if (field == "statement") ++corrupted.statement_occurrence_id;
        else if (field == "arena") ++corrupted.est_arena_index;
        else if (field == "owner") ++corrupted.owner_expression_occurrence_id;
        else if (field == "message") ++corrupted.message_expression_occurrence_id;
        else if (field == "argument") ++corrupted.argument_expression_occurrence_id;
        else if (field == "source") ++corrupted.source_fingerprint;
        else if (field == "handler") corrupted.selected_handler_identity += ":wrong";
        else if (field == "type") corrupted.argument_type.reset();
        else if (field == "context" || field == "est-context")
          ++corrupted.specialization_id;
        (void)program_.typed_semantics
            ->find_root_ingress_argument_type_fact(corrupted);
        throw std::runtime_error(
            "perturbed Root-ingress argument TypeRef unexpectedly passed");
      }
#endif
      checked_argument_type = &program_.typed_semantics
          ->find_root_ingress_argument_type_fact(*argument_type_requirement);
      if (checked_argument_type->selected_handler_identity != handler_identity ||
          checked_argument_type->specialization_id != caller.spec_id ||
          !checked_argument_type->argument_type ||
          !checked_argument_type->argument_type->is_closed())
        throw std::invalid_argument(
            "Root-ingress argument type contradicts selected handler/context");
      if (const auto* expression_facts = tree.arena->facts(
              argument_expression, caller.spec_id);
          expression_facts && expression_facts->type &&
          !expression_facts->type->structurally_equal(
              *checked_argument_type->argument_type))
        throw std::invalid_argument(
            "Root-ingress argument type contradicts checked EST TypeRef");
    } catch (const std::exception& error) {
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT " + std::string(error.what()));
    }
    const TypeRef& checked_type = checked_argument_type->argument_type;

    const auto* argument_call = std::get_if<est::Call>(
        &tree.arena->at(argument_expression));
    if (argument_call) {
      const auto* call_facts = tree.arena->facts(argument_expression,
                                                 caller.spec_id);
      if (call_facts && call_facts->resolved_target == "builtin:copy-v1") {
        if (argument_call->arguments.size() != 1)
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT checked Root-ingress copy arity mismatch");
        try {
          const auto& requirement = program_.typed_semantics->find_copy_requirement(
              tree.arena_index, argument_expression, caller.spec_id);
          if (requirement.statement_occurrence_id !=
                  statement->checked_statement_occurrence_id ||
              requirement.est_arena_index != tree.arena_index ||
              requirement.copy_occurrence_id != argument_expression ||
              requirement.source_occurrence_id != est::unparenthesized(
                  *tree.arena, argument_call->arguments.at(0)))
            throw std::invalid_argument(
                "Root-ingress explicit copy does not match exact statement/message argument/source");
          const auto* fact = program_.typed_semantics->find_copy_fact(requirement);
          if (!fact || !fact->is_authoritative() ||
              fact->specialization_id != caller.spec_id ||
              !fact->result_type->structurally_equal(*checked_type))
            throw std::invalid_argument(
                "Root-ingress explicit copy fact contradicts checked argument type/context");
          if (copy_authority) {
            *copy_authority = RootIngressCopyAuthorityOccurrence{
                statement->checked_statement_occurrence_id,
                tree.arena_index, tree.root, exact_message_expression,
                argument_index, argument_expression};
#ifdef MOSS_TEST_BUILD
            if (std::getenv(
                    "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_COPY_OWNER"))
              ++copy_authority->owner_expression;
            if (std::getenv(
                    "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_COPY_EXPR_ID"))
              ++copy_authority->copy_expression;
#endif
          }
          return "copy";
        } catch (const std::exception& error) {
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT " + std::string(error.what()));
        }
      }
    }

    const auto* argument_local = std::get_if<est::Identifier>(
        &tree.arena->at(argument_expression));
    if (argument_local) {
      const TypedRootIngressArgumentDispositionRequirementV1* disposition =
          nullptr;
      std::size_t disposition_requirements = 0;
      std::size_t disposition_facts = 0;
      est::ExprId owner_expression = tree.root;
#ifdef MOSS_TEST_BUILD
      if (std::getenv(
              "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_DISPOSITION_OWNER"))
        ++owner_expression;
#endif
      for (const auto& candidate : program_.typed_semantics
               ->required_root_ingress_argument_disposition_occurrences)
        if (candidate.statement_occurrence_id ==
                statement->checked_statement_occurrence_id &&
            candidate.est_arena_index == tree.arena_index &&
            candidate.owner_expression_occurrence_id == owner_expression &&
            candidate.message_expression_occurrence_id ==
                exact_message_expression &&
            candidate.argument_index == argument_index &&
            candidate.argument_expression_occurrence_id ==
                argument_expression &&
            candidate.source_fingerprint == message_handle->source_fingerprint &&
            candidate.specialization_id == caller.spec_id) {
          disposition = &candidate;
          ++disposition_requirements;
        }
      for (const auto& candidate : program_.typed_semantics
               ->root_ingress_argument_disposition_facts)
        if (candidate.statement_occurrence_id ==
                statement->checked_statement_occurrence_id &&
            candidate.est_arena_index == tree.arena_index &&
            candidate.owner_expression_occurrence_id == owner_expression &&
            candidate.message_expression_occurrence_id ==
                exact_message_expression &&
            candidate.argument_index == argument_index &&
            candidate.argument_expression_occurrence_id ==
                argument_expression &&
            candidate.source_fingerprint == message_handle->source_fingerprint &&
            candidate.specialization_id == caller.spec_id)
          ++disposition_facts;
#ifdef MOSS_TEST_BUILD
      if (std::getenv(
              "MOSS_TEST_INTERPRETER_DROP_ROOT_INGRESS_ARGUMENT_DISPOSITION_REQUIREMENT"))
        --disposition_requirements;
      if (std::getenv(
              "MOSS_TEST_INTERPRETER_DUPLICATE_ROOT_INGRESS_ARGUMENT_DISPOSITION_REQUIREMENT"))
        ++disposition_requirements;
      if (std::getenv(
              "MOSS_TEST_INTERPRETER_DROP_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT"))
        disposition_facts = 0;
      if (std::getenv(
              "MOSS_TEST_INTERPRETER_DUPLICATE_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT"))
        ++disposition_facts;
#endif
      if (disposition_requirements != 0 || disposition_facts != 0) {
        if (disposition_requirements != 1 || disposition_facts != 1 ||
            !disposition)
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT Root-ingress argument disposition requirement/fact missing or ambiguous");
        try {
#ifdef MOSS_TEST_BUILD
          if (std::getenv(
                  "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT")) {
            auto corrupted = *disposition;
            const std::string field = std::getenv(
                "MOSS_TEST_INTERPRETER_PERTURB_ROOT_INGRESS_ARGUMENT_DISPOSITION_FACT");
            if (field == "owner") ++corrupted.owner_expression_occurrence_id;
            else if (field == "message") ++corrupted.message_expression_occurrence_id;
            else if (field == "argument") ++corrupted.argument_expression_occurrence_id;
            else if (field == "binding") corrupted.source_binding_identity += ":wrong";
            else if (field == "definition") corrupted.source_definition_identity += ":wrong";
            else if (field == "type") corrupted.payload_type.reset();
            else if (field == "context") ++corrupted.specialization_id;
            (void)program_.typed_semantics
                ->find_root_ingress_argument_disposition_fact(corrupted);
            throw std::runtime_error(
                "perturbed Root-ingress argument disposition unexpectedly passed");
          }
#endif
          const auto& fact = program_.typed_semantics
              ->find_root_ingress_argument_disposition_fact(*disposition);
          const std::string expected_type = target.params.at(argument_index).type;
          if (!fact.payload_type->structurally_equal(*checked_type) ||
              fact.payload_type->to_canonical_string() != expected_type ||
              fact.specialization_id != caller.spec_id ||
              fact.action != TypedRootIngressArgumentDispositionFactV1::Action::TransferOwnedBinding ||
              fact.required_access != Effect::Consume || !fact.source_is_owned ||
              !fact.checker_proved_available)
            throw std::invalid_argument(
                "Root-ingress argument disposition contradicts exact handler/type/Consume action");
          return "transfer";
        } catch (const std::exception& error) {
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT " + std::string(error.what()));
        }
      }
    }

    const TypedRootIngressPayloadRequirementV1* payload_requirement = nullptr;
    std::size_t payload_requirement_count = 0;
    for (const auto& candidate : program_.typed_semantics
             ->required_root_ingress_payload_occurrences)
      if (candidate.statement_occurrence_id ==
              statement->checked_statement_occurrence_id &&
          candidate.est_arena_index == tree.arena_index &&
          candidate.message_expression_occurrence_id == exact_message_expression &&
          candidate.argument_index == argument_index &&
          candidate.argument_expression_occurrence_id == argument_expression &&
          candidate.source_fingerprint == message_handle->source_fingerprint &&
          candidate.specialization_id == caller.spec_id) {
        payload_requirement = &candidate;
        ++payload_requirement_count;
      }
    if (payload_requirement_count > 1)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT duplicate Root-ingress fresh-payload requirements");
    if (payload_requirement) {
      try {
        const auto& fact = program_.typed_semantics
            ->find_root_ingress_payload_fact(*payload_requirement);
        if (fact.selected_handler_identity != handler_identity ||
            !fact.payload_type ||
            !fact.payload_type->structurally_equal(*checked_type) ||
            fact.action != TypedRootIngressPayloadActionV1::TransferOwnedFreshRvalue ||
            !fact.checker_proved_fresh_rvalue || fact.permits_moss_value_copy)
          throw std::invalid_argument(
              "Root-ingress fresh-payload fact contradicts exact handler/type/action");
        return "fresh";
      } catch (const std::exception& error) {
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT " + std::string(error.what()));
      }
    }

    const bool primitive = checked_type->kind == TypeKind::Primitive &&
        (checked_type->name == "Int" || checked_type->name == "int" ||
         checked_type->name == "Float" || checked_type->name == "float" ||
         checked_type->name == "Bool" || checked_type->name == "bool" ||
         checked_type->name == "Char" || checked_type->name == "char");
    if (primitive)
      return "primitive";
    throw RuntimeError(line,
        "MOSS_INTERNAL_INVARIANT nonprimitive Root-ingress argument lacks checked copy or fresh-transfer authority");
  }

  PreparedHandlerCall prepare_root_ingress_handler_call(
      const ExpressionTreeView& tree, const est::Message& message_node,
      est::ExprId message_expression, DomainValue& instance,
      const Handler& target, Frame& caller, int line,
      std::ostream& output) {
    PreparedHandlerCall call;
    call.instance = &instance;
    call.target = &target;
    call.line = line;
    struct RootIngressCopyCounterScope {
      std::size_t& implicit_calls;
      std::size_t& explicit_calls;
      std::size_t& copy_evaluations;
      std::size_t prior_implicit_calls;
      std::size_t prior_explicit_calls;
      std::size_t prior_copy_evaluations;
      RootIngressCopyCounterScope(std::size_t& implicit_target,
                                  std::size_t& explicit_target,
                                  std::size_t& copy_target)
          : implicit_calls(implicit_target), explicit_calls(explicit_target),
            copy_evaluations(copy_target),
            prior_implicit_calls(implicit_target),
            prior_explicit_calls(explicit_target),
            prior_copy_evaluations(copy_target) {}
      ~RootIngressCopyCounterScope() {
        implicit_calls = prior_implicit_calls;
        explicit_calls = prior_explicit_calls;
        copy_evaluations = prior_copy_evaluations;
      }
    } copy_counter_scope(root_ingress_implicit_independent_calls_,
                         root_ingress_explicit_independent_calls_,
                         root_ingress_explicit_copy_evaluations_);
    for (std::uint32_t index = 0; index < message_node.arguments.size(); ++index) {
      RootIngressCopyAuthorityOccurrence copy_authority;
      const auto authority = resolve_root_ingress_argument_authority(
          tree, message_node, message_expression, index, instance, target,
          caller, line, &copy_authority);
      const auto prior_boundary = root_ingress_argument_preparation_;
      const auto prior_independent_count =
          root_ingress_implicit_independent_calls_;
      const auto prior_explicit_independent_count =
          root_ingress_explicit_independent_calls_;
      const auto prior_copy_count = root_ingress_explicit_copy_evaluations_;
      struct RootIngressPreparationScope {
        bool& slot;
        bool prior;
        explicit RootIngressPreparationScope(bool& target)
            : slot(target), prior(target) { slot = true; }
        ~RootIngressPreparationScope() { slot = prior; }
      } root_scope(root_ingress_argument_preparation_);
      const auto previous_copy_authority =
          active_root_ingress_copy_authority_;
      if (authority == "copy")
        active_root_ingress_copy_authority_ = copy_authority;
      else
        active_root_ingress_copy_authority_.reset();
      struct RootIngressCopyAuthorityScope {
        std::optional<RootIngressCopyAuthorityOccurrence>& slot;
        std::optional<RootIngressCopyAuthorityOccurrence> previous;
        ~RootIngressCopyAuthorityScope() { slot = std::move(previous); }
      } copy_authority_scope{active_root_ingress_copy_authority_,
                             previous_copy_authority};
      const auto argument_expression = est::unparenthesized(
          *tree.arena, message_node.arguments[index]);
      Value value;
      if (authority == "transfer") {
        const auto* identifier = std::get_if<est::Identifier>(
            &tree.arena->at(argument_expression));
        if (!identifier || caller.aliases.count(identifier->name) ||
            caller.consumed.count(identifier->name))
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT Root-ingress Consume authority does not name an available owned local");
        auto source = caller.locals.find(identifier->name);
        if (source == caller.locals.end())
          throw RuntimeError(line,
              "MOSS_INTERNAL_INVARIANT Root-ingress Consume source local is unavailable");
        value = std::move(source->second);
        caller.locals.erase(source);
        caller.consumed.insert(identifier->name);
      } else {
        value = evaluate_tree_node(tree, argument_expression,
                                   caller, line, output);
      }
      const auto implicit_count = root_ingress_implicit_independent_calls_ -
          prior_independent_count;
      const auto copy_count = root_ingress_explicit_copy_evaluations_ -
          prior_copy_count;
      const auto explicit_independent_count =
          root_ingress_explicit_independent_calls_ -
          prior_explicit_independent_count;
      root_ingress_argument_preparation_ = prior_boundary;
      if (implicit_count != 0)
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT Root-ingress argument performed an unauthorized implicit independent copy");
      if (authority == "copy" && copy_count != 1)
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT checked Root-ingress copy was not evaluated exactly once");
      if (authority == "copy" && explicit_independent_count != 1)
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT checked Root-ingress copy did not create exactly one independent value");
      if (authority != "copy" && explicit_independent_count != 0)
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT Root-ingress argument created an unapproved independent value");
#ifdef MOSS_TEST_BUILD
      if (std::getenv("MOSS_TEST_TRACE_ROOT_INGRESS_COPY_COUNTS"))
        std::cerr << "ROOT_INGRESS_INTERPRETER_COPY_COUNTS argument=" << index
                  << " authority=" << authority
                  << " explicit_copy_evaluations=" << copy_count
                  << " authorized_independent_calls=" << explicit_independent_count
                  << " implicit_independent_calls=" << implicit_count << "\n";
#endif
      call.arguments.push_back(std::move(value));
    }
    return call;
  }

  PreparedHandlerCall prepare_handler_call(const std::string& invocation, Frame& caller,
                                           int line, std::ostream& output) {
    if (!caller.domain)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT Root-ingress message lacks an exact checked EST occurrence");
    std::string callee; std::vector<std::string> arguments;
    if (!expression_call(invocation, callee, arguments,
                         "prepare_handler_call")) throw RuntimeError(line, "internal error: malformed checked message");
    auto member = expression_member(callee);
    if (!member) throw RuntimeError(line, "internal error: missing checked message receiver");
    Value handle = evaluate_expression_source(member->first, caller, line, output);
    if (handle.kind != Value::Kind::DomainHandle || !handle.domain || !handle.domain->initialized)
      throw RuntimeError(line, "internal error: message target is not a published concrete domain");
    auto& instance = *handle.domain;
    const Handler* target = nullptr;
    for (const auto& handler : instance.definition->handlers) if (handler.name == member->second) target = &handler;
    if (!target || target->params.size() != arguments.size()) throw RuntimeError(line, "internal error: unresolved checked handler");
    PreparedHandlerCall call;
    call.instance = &instance;
    call.target = target;
    call.line = line;
    for (const auto& argument : arguments)
      call.arguments.push_back(independent(evaluate_expression_source(argument, caller, line, output)));
    return call;
  }

  PreparedHandlerCall prepare_handler_call(const ExpressionTreeView& tree,
      const est::Message& message_node, Frame& caller, int line,
      std::ostream& output, est::ExprId message_expression) {
    Value handle = evaluate_tree_node(tree, message_node.target, caller, line,
                                      output);
    if (handle.kind != Value::Kind::DomainHandle || !handle.domain ||
        !handle.domain->initialized)
      throw RuntimeError(line,
          "internal error: message target is not a published concrete domain");
    auto& instance = *handle.domain;
    const Handler* target = nullptr;
    for (const auto& handler : instance.definition->handlers)
      if (handler.name == message_node.handler) target = &handler;
    if (!target || target->params.size() != message_node.arguments.size())
      throw RuntimeError(line, "internal error: unresolved checked handler");
    if (!caller.domain) {
      if (!active_statement_ || !active_statement_->message_root_ingress)
        throw RuntimeError(line,
            "MOSS_INTERNAL_INVARIANT Root-ingress statement authority unavailable");
      return prepare_root_ingress_handler_call(
          tree, message_node, est::unparenthesized(*tree.arena,
              message_expression), instance, *target, caller, line, output);
    }
    PreparedHandlerCall call;
    call.instance = &instance;
    call.target = target;
    call.line = line;
    for (const auto argument : message_node.arguments)
      call.arguments.push_back(independent(evaluate_tree_node(
          tree, argument, caller, line, output)));
    return call;
  }

  PreparedHandlerCall prepare_handler_call(const ExpressionTreeView& tree,
      const RootSubmissionPlan& plan, const est::Call& invocation,
      Frame& caller, int line, std::ostream& output) {
    (void)output;
    auto domain_value = caller.locals.find(plan.target_domain_binding);
    if (domain_value == caller.locals.end() ||
        domain_value->second.kind != Value::Kind::DomainHandle ||
        !domain_value->second.domain ||
        !domain_value->second.domain->initialized)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT checked executor target binding unavailable");
    auto& instance = *domain_value->second.domain;
    if (plan.target_domain_semantic_identity !=
        "domain:" + instance.definition->name + "@" +
            std::to_string(instance.definition->line))
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT checked executor target identity mismatch");
    const Handler* target = nullptr;
    for (const auto& handler : instance.definition->handlers)
      if (handler.name == plan.handler_name) target = &handler;
    if (!target ||
        "handler:" + instance.definition->name + "." + target->name + "@" +
            std::to_string(target->line) != plan.handler_semantic_identity ||
        invocation.arguments.size() != plan.argument_types.size() ||
        target->params.size() != invocation.arguments.size())
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT checked executor handler shape mismatch");
    PreparedHandlerCall call;
    call.instance = &instance;
    call.target = target;
    call.line = line;
    for (const auto argument : invocation.arguments)
      call.arguments.push_back(independent(evaluate_tree_node(
          tree, argument, caller, line, output)));
    return call;
  }

  // Phase 20: a top-level `message` from `main` (checker bit
  // message_root_ingress) is an independent synchronous Root. Fast Debug's
  // sequential schedule already gives it Root semantics: arguments are
  // snapshotted (prepare_handler_call) and the Root runs to completion before
  // the caller continues. Nested messages inside it are ordinary nested calls.
  Value message(const std::string& invocation, Frame& caller, int line, std::ostream& output) {
    return run_handler_call(prepare_handler_call(invocation, caller, line, output), caller, output);
  }

  Value message(const ExpressionTreeView& tree, const est::Message& node,
                Frame& caller, int line, std::ostream& output,
                est::ExprId message_expression) {
    return run_handler_call(prepare_handler_call(tree, node, caller, line,
                                                 output, message_expression),
                            caller, output);
  }

  Value run_handler_call(PreparedHandlerCall call, Frame& caller, std::ostream& output) {
    auto& instance = *call.instance;
    const Handler* target = call.target;
    int line = call.line;
    const bool root_ingress = caller.domain == nullptr;
    Frame frame; frame.handler_scope = true; frame.domain = &instance; frame.handler = target->name;
    frame.function = instance.definition->name + "." + target->name;
    frame.source_file = target->execution_source_file.empty()
        ? instance.definition->source_file : target->execution_source_file;
    frame.semantic_identity = "handler:" + instance.definition->name + "." +
        target->name + "@" + std::to_string(target->line);
    const auto& specialization =
        program_.domain_specializations.at(*instance.concrete->specialization_index);
    const auto handler_position = static_cast<std::size_t>(
        target - instance.definition->handlers.data());
    if (handler_position >= specialization.handler_spec_ids.size())
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT domain handler SpecId table is incomplete");
    frame.spec_id = specialization.handler_spec_ids[handler_position];
    if (frame.spec_id == InvalidSpecId ||
        frame.spec_id >= program_.specialization_table.size() ||
        program_.specialization_table[frame.spec_id].id != frame.spec_id)
      throw RuntimeError(line,
          "MOSS_INTERNAL_INVARIANT domain handler SpecId is missing or contradictory");
    auto parameter_types = specialization.handler_parameter_types.find(target->name);
    std::string payload;
    for (size_t i = 0; i < call.arguments.size(); ++i) {
      Value value = std::move(call.arguments[i]);
      if (i) payload += ", ";
      payload += summary(value);
      frame.locals[target->params[i].name] = std::move(value);
      std::string type = parameter_types != specialization.handler_parameter_types.end() &&
          i < parameter_types->second.size() ? parameter_types->second[i] : target->params[i].type;
      if (!type.empty()) frame.local_types[target->params[i].name] = type;
    }
    Frame call_event = caller; call_event.domain = &instance; call_event.handler = target->name;
    int message_id = emit("message_call", call_event, line, payload);
    MessageScope message_scope(*this, message_id);
    EventScope message_event_scope(*this, message_id);
    int handler_id = emit("handler_enter", frame, target->line);
    Flow flow;
    {
      EventScope handler_scope(*this, handler_id, true, true);
      try {
        const auto& executable_body = target->execution_body
            ? *target->execution_body : target->body;
        flow = execute(executable_body, frame, output);
      } catch (const RaisedSignal& raised) {
        if (!root_ingress || target->failure_arms.empty()) throw;
        const FailureArm* selected = nullptr;
        for (size_t index = 0; index < target->failure_arms.size(); ++index)
          if (target->failure_arms[index].exceptional_alternatives.count(
                  raised.identity)) {
            selected = &target->failure_arms[index];
            break;
          }
        if (!selected)
          throw RuntimeError(line,
              "checked Root on_fail has no arm for '" +
              raised.identity.canonical() + "'");

        // The normal attempt's lexical frame is abandoned at this boundary.
        // Make the failure continuation from its checked owned captures and
        // immutable incoming payloads, rather than exposing every old local.
        Frame failure = frame;
        failure.function += "$fail";
        failure.locals.clear();
        failure.local_types.clear();
        failure.aliases.clear();
        for (const auto& parameter : target->params) {
          auto value = frame.locals.find(parameter.name);
          if (value != frame.locals.end())
            failure.locals[parameter.name] = independent(value->second);
          auto type = frame.local_types.find(parameter.name);
          if (type != frame.local_types.end())
            failure.local_types[parameter.name] = type->second;
        }
        for (const auto& capture : selected->captures) {
          auto edge = raised.edge_locals.find(capture.name);
          auto value = frame.locals.find(capture.name);
          const Value* captured = edge != raised.edge_locals.end() ? &edge->second
              : value != frame.locals.end() ? &value->second : nullptr;
          if (!captured)
            throw RuntimeError(selected->line,
                "checked on_fail capture '" + capture.name +
                "' is absent at its exceptional edge");
          failure.locals[capture.name] = independent(*captured);
          failure.local_types[capture.name] = capture.type;
        }
        if (!selected->binding.empty()) {
          failure.locals[selected->binding] = independent(raised.value);
          failure.local_types[selected->binding] = raised.identity.type;
        }
        for (const auto& pattern : selected->est_patterns)
          for (const auto& binding :
               error_pattern_payloads(pattern, raised, selected->line)) {
            failure.locals[binding.first] = binding.second.first;
            failure.local_types[binding.first] = binding.second.second;
          }
        emit("on_fail", failure, selected->line,
             raised.identity.canonical());
        const auto& executable_body = selected->execution_body
            ? *selected->execution_body : selected->body;
        flow = execute(executable_body, failure, output);
      }
      bool needs_reply = target->reply_type.has_value() || specialization.handler_reply_types.count(target->name);
      if (needs_reply && !flow.returned) throw RuntimeError(line, "handler completed without its required reply");
      emit("handler_exit", frame, target->line);
      annotate_last_handler(handler_id);
    }
    emit("message_return", call_event, line, summary(flow.value));
    annotate_last_handler(handler_id);
    return flow.value;
  }
