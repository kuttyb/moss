// Generator members for lowering the stored SynchronizationPlan.
  const DomainSynchronizationPlan& physical_domain_plan(const string& binding) const {
    const auto& graph = semantic_program_->concrete_domain_graph;
    auto instance = std::find_if(graph.instances.begin(), graph.instances.end(),
        [&](const auto& candidate) { return candidate.binding == binding; });
    synchronization_require(instance != graph.instances.end(), "missing concrete constructor identity");
    const auto& domains = semantic_program_->synchronization_plan.domains;
    auto found = std::find_if(domains.begin(), domains.end(),
        [&](const auto& candidate) { return candidate.concrete_instance_id == instance->identity; });
    synchronization_require(found != domains.end(), "missing physical synchronization plan");
    return *found;
  }

  void emit_physical_descriptor(std::ostringstream& o, const string& binding) const {
    const auto& domain = physical_domain_plan(binding);
    o << "(" << rust_string_literal(domain.concrete_instance_id) << ", " << domain.domain_rank << ", vec![";
    for (const auto& cls : domain.classes) {
      o << "vec![";
      for (const auto& leaf : cls.member_leaves) o << rust_string_literal(leaf) << ",";
      o << "],";
    }
    o << "], vec![";
    for (const auto& h : domain.handlers) {
      o << "(" << rust_string_literal(h.name) << ", vec![";
      // std::map is ordered, but explicitly use and check the stored class_rank.
      vector<std::pair<size_t, SynchronizationMode>> acquisitions(h.class_modes.begin(), h.class_modes.end());
      std::sort(acquisitions.begin(), acquisitions.end(), [&](const auto& a, const auto& b) {
        return domain.classes.at(a.first).class_rank < domain.classes.at(b.first).class_rank;
      });
      for (const auto& entry : acquisitions)
        o << "(" << domain.classes.at(entry.first).class_rank << ", "
          << (entry.second == SynchronizationMode::Exclusive ? "true" : "false") << "),";
      o << "], vec![";
      for (const auto& leaf : h.effects.reads) o << rust_string_literal(leaf) << ",";
      o << "], vec![";
      for (const auto& leaf : h.exclusive_set) o << rust_string_literal(leaf) << ",";
      o << "], " << rust_string_literal(h.handler_identity) << "),";
    }
    o << "])";
  }

  // Layout traversal only. Effects, class membership and acquisition modes are
  // never inferred here; the final application's stored plan supplies them.
  void physical_leaves(const string& path, const string& type,
                       vector<std::pair<string, string>>& leaves) const {
    auto object = objects_.find(canonical_type_name(type));
    if (object != objects_.end() && !object->second->fields.empty()) {
      for (const auto& field : object->second->fields)
        physical_leaves(path + "." + field.name, field.type, leaves);
    } else leaves.emplace_back(path, type);
  }

  string physical_view_type(const string& type) const {
    auto object = objects_.find(canonical_type_name(type));
    return object != objects_.end() && !object->second->fields.empty() ? object->first + "View<'a>" : "MossSlot<'a, " + rust_type(type) + ">";
  }

  string physical_working_value(const string& path, const string& type, const string& domain,
                                const vector<std::pair<string,string>>& leaves) const {
    auto object = objects_.find(canonical_type_name(type));
    if (object != objects_.end() && !object->second->fields.empty()) {
      string value = rust_type(type) + "View { ";
      for (const auto& field : object->second->fields)
        value += field.name + ": " + physical_working_value(path + "." + field.name, field.type, domain, leaves) + ", ";
      return value + "}";
    }
    auto leaf = std::find_if(leaves.begin(), leaves.end(), [&](const auto& entry) { return entry.first == path; });
    synchronization_require(leaf != leaves.end(), "missing physical leaf layout");
    string variant = domain + "Leaf::L" + std::to_string(leaf - leaves.begin());
    string key = rust_string_literal(path);
    return "match __exclusive.remove(" + key + ") { Some(" + variant + "(value)) => MossSlot::Exclusive(value), "
        "None => match __frame.read(" + key + ") { Some(" + variant + "(value)) => MossSlot::Read(value), "
        "None => MossSlot::Absent, _ => std::process::abort() }, _ => std::process::abort() }";
  }

  static string physical_leaf_binding(string path) {
    string result = "__moss_storage_";
    for (const auto& part : split_top_level(path, '.')) result += std::to_string(part.size()) + "_" + part;
    return result;
  }

  void decompose_physical_value(std::ostringstream& o, const string& path, const string& type, const string& value) const {
    auto object = objects_.find(canonical_type_name(type));
    if (object == objects_.end() || object->second->fields.empty()) {
      o << "    let " << physical_leaf_binding(path) << " = " << value << ";\n";
      return;
    }
    o << "    let (";
    for (const auto& field : object->second->fields)
      o << physical_leaf_binding(path + "." + field.name) << ",";
    o << ") = " << value << ".__moss_into_parts();\n";
    for (const auto& field : object->second->fields) {
      string member = path + "." + field.name;
      if (view_object_type(field.type))
        decompose_physical_value(o, member, field.type, physical_leaf_binding(member));
    }
  }

  void gen_planned_domain(std::ostringstream& o, const Domain& d) {
    const string identity = "domain:" + d.name + "@" + std::to_string(d.line);
    tooling_begin(o, 0, "domain", identity, d.name + "State");
    source_comment(o, 0, d.line, d.header);
    o << "// SynchronizationPlan: per-instance class storage; no coarse domain guard.\n"
      << "struct " << d.name << "State<'a> {\n";
    vector<std::pair<string,string>> leaves;
    for (const auto& field : d.state) {
      source_comment(o, 4, field.line, state_field_signature(field));
      o << "    " << field.name << ": " << physical_view_type(field.type) << ",\n";
      physical_leaves(field.name, field.type, leaves);
    }
    for (const auto& route : d.routes) o << "    " << route.name << ": &'a " << rust_type(route.type) << ",\n";
    o << "    __lifetime: std::marker::PhantomData<&'a ()>,\n}\nenum " << d.name << "Leaf {\n";
    for (size_t index = 0; index < leaves.size(); ++index)
      o << "    L" << index << "(" << rust_type(leaves[index].second) << "),\n";
    o << "    Unused,\n}\n";
    tooling_end(o, 0, identity);
    o << "impl " << d.name << "Ref {\n";
    for (const auto& h : d.handlers) {
      string result = reply_binding(d, h);
      string semantic_identity = handler_semantic_identity(d, h);
      string native = tooling_native_symbol("handler", d.name + "__" + h.name, semantic_identity);
      o << "    // Acquire exactly ClassSet in increasing class_rank, hold through\n"
        << "    // complete body and nested messages; restore state before release.\n"
        << "    fn " << h.name << "_shared(&self";
      for (const auto& p : h.params)
        o << ", " << p.name << ": " << rust_type(p.type);
      string ret = h.reply_type ? "Option<" + rust_type(*h.reply_type) + ">" : "()";
      o << ") -> " << ret << " {\n"
        << "        let _abort = MossAbortOnUnwind;\n"
        << "        let mut __frame = self.state.enter(" << rust_string_literal(h.name) << ");\n"
        << "        let mut __exclusive = __frame.take_exclusive();\n"
        << "        let mut state = " << d.name << "State {\n";
      for (const auto& field : d.state)
        o << "            " << field.name << ": " << physical_working_value(field.name, field.type, d.name, leaves) << ",\n";
      for (const auto& route : d.routes) o << "            " << route.name << ": &self." << route.name << ",\n";
      o << "            __lifetime: std::marker::PhantomData,\n        };\n        let result = self." << h.name << "_body(&mut state";
      for (const auto& p : h.params) o << ", " << p.name;
      o << ");\n";
      if (h.reply_type)
        o << "        if result.is_none() { eprintln!(\"Moss message failed: " << d.name << "." << h.name
          << " completed without a reply\"); std::process::abort(); }\n";
      o << "        let __restored: Vec<(&str, Option<" << d.name << "Leaf>)> = vec![\n";
      for (size_t index = 0; index < leaves.size(); ++index)
        o << "            (" << rust_string_literal(leaves[index].first) << ", state." << leaves[index].first
          << ".into_exclusive().map(" << d.name << "Leaf::L" << index << ")),\n";
      o << "        ];\n        for (leaf, value) in __restored { if let Some(value) = value { __frame.restore(leaf, value); } }\n";
      o << "        drop(__frame);\n        result\n    }\n";
      tooling_begin(o, 4, "handler", semantic_identity, d.name + "Ref::" + h.name + "_body", native);
      source_comment(o, 4, h.line, handler_signature(h));
      debug_symbol_attributes(o, 4, native);
      o << "    fn " << h.name << "_body(&self, state: &mut " << d.name << "State<'_>";
      for (const auto& p : h.params)
        o << ", " << p.name << ": " << rust_type(p.type);
      o << ") -> " << ret << " {\n        let self_ref = self;\n";
      if (h.reply_type) o << "        let mut " << result << ": " << ret << " = None;\n";
      o << "        " << (handler_needs_label(h) ? "'handler: " : "") << "{\n";
      std::set<string> locals{"self_ref", result};
      std::unordered_map<string,string> types{{"self", d.name}};
      for (const auto& field : d.state) types[field.name] = field.type;
      for (const auto& route : d.routes) types[route.name] = route.type;
      for (const auto& p : h.params) {
        locals.insert(p.name); types[p.name] = p.type;
      }
      view_domain_ = true;
      gen_stmts(o, h.body, &d, &h, result, locals, types, 3, true, false, functional_handler_context(d, h));
      view_domain_ = false;
      o << "        }\n";
      if (h.reply_type) o << "        " << result << "\n";
      o << "    }\n";
      tooling_end(o, 4, semantic_identity);
    }
    o << "}\n";
    o << (d.exported ? "pub " : "") << "fn construct_" << snake_case(d.name) << "(";
    for (const auto& route : d.routes) o << route.name << ": " << rust_type(route.type) << ", ";
    for (const auto& field : d.state) o << field.name << ": " << rust_type(field.type) << ", ";
    o << "__plan: MossPhysicalPlan) -> " << d.name << "Ref {\n"
      << "    __moss_require_send::<" << d.name << "Ref>();\n"
      << "    let mut leaves = MossLeaves::new();\n";
    for (const auto& field : d.state)
      decompose_physical_value(o, field.name, field.type, field.name);
    for (size_t index = 0; index < leaves.size(); ++index) {
      const auto& leaf = leaves[index];
      o << "    __moss_require_send::<" << rust_type(leaf.second) << ">();\n"
        << "    leaves.insert(" << rust_string_literal(leaf.first) << ", " << d.name << "Leaf::L" << index << "(" << physical_leaf_binding(leaf.first) << "));\n";
    }
    for (const auto& h : d.handlers) {
      for (const auto& p : h.params) o << "    __moss_require_send::<" << rust_type(p.type) << ">();\n";
      if (h.reply_type) o << "    __moss_require_send::<" << rust_type(*h.reply_type) << ">();\n";
    }
    o << "    " << d.name << "Ref { state: Arc::new(MossClassRuntime::new(__plan, leaves)), ";
    for (const auto& route : d.routes) o << route.name << ", ";
    o << "}\n}\n\n";
  }
