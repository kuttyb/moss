// Logical domains in Fast Debug use checked concrete identities and ordinary
// interpreter frames. No physical synchronization plan or scheduler is involved.
  static Value independent(const Value& source) {
    Value result = source;
    if (source.object) {
      result.object = std::make_shared<StructValue>();
      result.object->type = source.object->type;
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
    // Only checked lvalue-shaped expressions participate in location lookup.
    size_t end = 0;
    while (end < expression.size() && (std::isalnum(static_cast<unsigned char>(expression[end])) || expression[end] == '_')) ++end;
    if (!end || !identifier(expression.substr(0, end))) return {};
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
        auto index = eval(expression.substr(start, end - start), frame, line, output);
        if (place.value->map) {
          bool found = false;
          for (auto& entry : *place.value->map) {
            if (equal(entry.first, index)) { place.value = &entry.second; found = true; break; }
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
      const std::vector<std::string>& arguments, Frame& caller, Frame& frame, int line, std::ostream& output) {
    frame.domain = caller.domain; frame.handler = caller.handler;
    // Domain context is trace provenance only in helpers; state names remain
    // lexically resolved through explicit actual/formal bindings.
    for (size_t i = 0; i < arguments.size(); ++i) {
      Effect effect = i < effects.size() ? effects[i] : Effect::Read;
      auto place = locate(arguments[i], caller, line, output);
      if (effect != Effect::Consume && place.value) frame.aliases[parameters[i].name] = place;
      else {
        frame.locals[parameters[i].name] = eval(arguments[i], caller, line, output);
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
      std::string name; std::vector<std::string> arguments;
      if (!parse_call(statement.b, name, arguments)) throw RuntimeError(statement.line, "internal error: missing domain constructor");
      const auto& specialization = program_.domain_specializations.at(*instance.concrete->specialization_index);
      std::unordered_map<std::string, std::string> initializers;
      for (const auto& argument : arguments) {
        auto colon = argument.find_first_of(":=");
        if (colon == std::string::npos) throw RuntimeError(statement.line, "internal error: unnamed state initializer");
        initializers.emplace(trim_copy(argument.substr(0, colon)), trim_copy(argument.substr(colon + 1)));
      }
      for (const auto& field : instance.definition->state) {
        auto explicit_value = initializers.find(field.name);
        auto type = specialization.state_types.find(field.name);
        instance.state[field.name] = explicit_value != initializers.end() ? eval(explicit_value->second, frame, statement.line, output)
            : !field.init.empty() ? eval(field.init, frame, field.line, output)
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

  Value message(const std::string& invocation, Frame& caller, int line, std::ostream& output) {
    std::string callee; std::vector<std::string> arguments;
    if (!parse_call(invocation, callee, arguments)) throw RuntimeError(line, "internal error: malformed checked message");
    auto member = top_level_member(callee);
    if (!member) throw RuntimeError(line, "internal error: missing checked message receiver");
    Value handle = eval(member->first, caller, line, output);
    if (handle.kind != Value::Kind::DomainHandle || !handle.domain || !handle.domain->initialized)
      throw RuntimeError(line, "internal error: message target is not a published concrete domain");
    auto& instance = *handle.domain;
    const Handler* target = nullptr;
    for (const auto& handler : instance.definition->handlers) if (handler.name == member->second) target = &handler;
    if (!target || target->params.size() != arguments.size()) throw RuntimeError(line, "internal error: unresolved checked handler");
    Frame frame; frame.handler_scope = true; frame.domain = &instance; frame.handler = target->name;
    frame.function = instance.definition->name + "." + target->name;
    frame.source_file = instance.definition->source_file;
    frame.semantic_identity = "handler:" + instance.definition->name + "." + target->name;
    std::string payload;
    for (size_t i = 0; i < arguments.size(); ++i) {
      Value value = independent(eval(arguments[i], caller, line, output));
      if (i) payload += ", ";
      payload += summary(value);
      frame.locals[target->params[i].name] = std::move(value);
    }
    Frame call_event = caller; call_event.domain = &instance; call_event.handler = target->name;
    emit("message_call", call_event, line, payload);
    emit("handler_enter", frame, target->line);
    Flow flow = execute(target->body, frame, output);
    const auto& specialization = program_.domain_specializations.at(*instance.concrete->specialization_index);
    bool needs_reply = target->reply_type.has_value() || specialization.handler_reply_types.count(target->name);
    if (needs_reply && !flow.returned) throw RuntimeError(line, "handler completed without its required reply");
    emit("handler_exit", frame, target->line);
    emit("message_return", call_event, line, summary(flow.value));
    return flow.value;
  }
