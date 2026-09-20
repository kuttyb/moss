// Static lowering of the stored plan. No synchronization analysis lives here.
  const DomainSynchronizationPlan& physical_domain_plan(const string& binding) const {
    const auto& graph = semantic_program_->concrete_domain_graph;
    auto instance = std::find_if(graph.instances.begin(), graph.instances.end(),
        [&](const auto& candidate) { return candidate.binding == binding; });
    synchronization_require(instance != graph.instances.end(), "missing concrete constructor identity");
    const auto& domains = semantic_program_->synchronization_plan.domains;
    auto found = std::find_if(domains.begin(), domains.end(),
        [&](const auto& candidate) { return candidate.concrete_instance_id == instance->identity; });
    synchronization_require(found != domains.end(), "missing physical instance plan");
    return *found;
  }

  const DomainSynchronizationPlan* typed_domain_plan(const Domain& d) const {
    const DomainSynchronizationPlan* selected = nullptr;
    for (const auto& instance : semantic_program_->concrete_domain_graph.instances) {
      auto specialized = specialization_names_.find(instance.domain + "\n" + instance.binding);
      string name = specialized == specialization_names_.end() ? instance.domain : specialized->second;
      if (name != d.name) continue;
      const auto& candidate = physical_domain_plan(instance.binding);
      if (selected) {
        // Reuse only an identical typed layout and handler contract. Concrete
        // identity/rank stay instance-specific test metadata, never execution data.
        synchronization_require(selected->state_leaves == candidate.state_leaves &&
            selected->leaf_to_class == candidate.leaf_to_class &&
            selected->handlers.size() == candidate.handlers.size(), "incompatible shared typed layout");
        for (size_t i = 0; i < candidate.handlers.size(); ++i) {
          const auto& a = selected->handlers[i]; const auto& b = candidate.handlers[i];
          synchronization_require(a.name == b.name && a.class_modes == b.class_modes &&
              a.effects.reads == b.effects.reads && a.effects.writes == b.effects.writes &&
              a.effects.consumes == b.effects.consumes, "incompatible shared typed handler");
        }
      } else selected = &candidate;
    }
    // An uninstantiated source template has no runtime state or executable body.
    return selected;
  }

  void physical_leaves(const string& path, const string& type,
                       vector<std::pair<string, string>>& leaves) const {
    auto object = objects_.find(canonical_type_name(type));
    if (object != objects_.end() && !object->second->fields.empty()) {
      for (const auto& field : object->second->fields)
        physical_leaves(path + "." + field.name, field.type, leaves);
    } else leaves.emplace_back(path, type);
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


  string typed_view(const string& path, const string& type, const DomainSynchronizationPlan& plan,
                    const HandlerSynchronizationPlan& handler, bool as_type, bool object_member = false) const {
    auto object = objects_.find(canonical_type_name(type));
    if (object != objects_.end() && !object->second->fields.empty()) {
      string result = object->first + "View" + (as_type ? "<" : " { ");
      for (size_t i = 0; i < object->second->fields.size(); ++i) {
        const auto& field = object->second->fields[i];
        if (i) result += ", ";
        if (!as_type) result += field.name + ": ";
        result += typed_view(path + "." + field.name, field.type, plan, handler, as_type, true);
      }
      return result + (as_type ? ">" : " }");
    }
    auto effect = handler.effects.normalized(path);
    if (!effect) return as_type ? "MossAbsent<" + rust_type(type) + ">" : "MossAbsent(std::marker::PhantomData)";
    bool writes = *effect != Effect::Read;
    if (as_type) {
      if (object_member) return string(writes ? "MossWrite<'a, " : "MossRead<'a, ") + rust_type(type) + ">";
      return string(writes ? "&'a mut " : "&'a ") + rust_type(type);
    }
    auto cls = plan.leaf_to_class.find(path);
    string place = cls == plan.leaf_to_class.end() ? "self.state.immutable." :
        "__values" + std::to_string(cls->second) + ".";
    place += physical_leaf_binding(path);
    string reference = string(writes ? "&mut " : "&") + place;
    return object_member ? string(writes ? "MossWrite(" : "MossRead(") + reference + ")" : reference;
  }

  bool typed_field_used(const Field& field, const HandlerSynchronizationPlan& handler) const {
    for (const auto* effects : {&handler.effects.reads, &handler.effects.writes, &handler.effects.consumes})
      for (const auto& leaf : *effects)
        if (leaf == field.name || starts_with(leaf, field.name + ".")) return true;
    return false;
  }

  void typed_event(std::ostringstream& o, const string& event, const HandlerSynchronizationPlan& h,
                   const string& rank = "usize::MAX", bool exclusive = false) const {
    o << "        #[cfg(any(test, moss_perf))] moss_lock_event(" << rust_string_literal(event)
      << ", self.state.identity, self.state.handler_" << h.name
      << ", self.state.rank, " << rank << ", " << (exclusive ? "true" : "false") << ");\n";
  }

  void gen_planned_domain(std::ostringstream& o, const Domain& d) {
    const auto* plan = typed_domain_plan(d);
    const string identity = "domain:" + d.name + "@" + std::to_string(d.line);
    vector<std::pair<string,string>> leaves;
    for (const auto& field : d.state) physical_leaves(field.name, field.type, leaves);
    tooling_begin(o, 0, "domain", identity, d.name + "Runtime");
    source_comment(o, 0, d.line, d.header);
    for (const auto& field : d.state) source_comment(o, 0, field.line, state_field_signature(field));
    o << "// SynchronizationPlan is compile-time only: typed class-owned state.\n";
    if (plan) {
      for (const auto& cls : plan->classes) {
        o << "struct " << d.name << "Class" << cls.class_rank << " {\n";
        for (const auto& leaf : leaves) if (cls.member_leaves.count(leaf.first))
          o << "    " << physical_leaf_binding(leaf.first) << ": " << rust_type(leaf.second) << ",\n";
        o << "}\n";
      }
    }
    o << "struct " << d.name << "Immutable {\n";
    if (plan) for (const auto& leaf : leaves) if (!plan->protected_mutable_leaves.count(leaf.first))
      o << "    " << physical_leaf_binding(leaf.first) << ": " << rust_type(leaf.second) << ",\n";
    o << "}\nstruct " << d.name << "Runtime {\n";
    if (plan) for (const auto& cls : plan->classes)
      o << "    class" << cls.class_rank << ": std::sync::RwLock<" << d.name << "Class" << cls.class_rank << ">,\n";
    o << "    immutable: " << d.name << "Immutable,\n"
      << "    #[cfg(any(test, moss_perf))] identity: &'static str,\n"
      << "    #[cfg(any(test, moss_perf))] rank: usize,\n";
    for (const auto& h : d.handlers)
      o << "    #[cfg(any(test, moss_perf))] handler_" << h.name << ": &'static str,\n";
    o << "}\n";
    tooling_end(o, 0, identity);
    for (const auto& h : d.handlers) {
      const HandlerSynchronizationPlan* hp = nullptr;
      if (plan) for (const auto& candidate : plan->handlers) if (candidate.name == h.name) hp = &candidate;
      string state_type = d.name + h.name + "State";
      o << "impl " << d.name << "Ref {\n    #[inline]\n    fn " << h.name << "_shared(&self";
      for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
      string ret = h.reply_type ? "Option<" + rust_type(*h.reply_type) + ">" : "()";
      o << ") -> " << ret << " {\n";
      if (!hp) { o << "        std::process::abort() // Uninstantiated source template.\n    }\n}\n"; continue; }
      typed_event(o, "handler_enter", *hp);
      for (const auto& entry : hp->class_modes) {
        size_t rank = plan->classes.at(entry.first).class_rank;
        bool exclusive = entry.second == SynchronizationMode::Exclusive;
        string r = std::to_string(rank), mode = exclusive ? "write" : "read";
        typed_event(o, "lock_acquire", *hp, r, exclusive);
        o << "        #[cfg(not(moss_perf))]\n        let " << (exclusive ? "mut " : "") << "__class" << r
          << " = moss_" << mode << "_or_abort(&self.state.class" << r << ");\n"
          << "        #[cfg(moss_perf)]\n        let " << (exclusive ? "mut " : "") << "__class" << r
          << " = match self.state.class" << r << ".try_" << mode << "() {\n"
          << "            Ok(guard) => guard,\n            Err(std::sync::TryLockError::WouldBlock) => {\n";
        typed_event(o, "lock_contended", *hp, r, exclusive);
        o << "                moss_" << mode << "_or_abort(&self.state.class" << r << ")\n"
          << "            },\n            Err(_) => std::process::abort(),\n        };\n"
          << "        let __abort" << r << " = MossAbortOnUnwind;\n";
        typed_event(o, "lock_acquired", *hp, r, exclusive);
      }
      for (const auto& entry : hp->class_modes)
        o << "        let __values" << entry.first << " = &"
          << (entry.second == SynchronizationMode::Exclusive ? "mut " : "")
          << "*__class" << entry.first << ";\n";
      o << "        let __abort_body = MossAbortOnUnwind;\n        let mut state = " << state_type << " {\n";
      for (const auto& f : d.state) if (typed_field_used(f, *hp))
        o << "            " << f.name << ": " << typed_view(f.name, f.type, *plan, *hp, false) << ",\n";
      for (const auto& r : d.routes) o << "            " << r.name << ": &self." << r.name << ",\n";
      o << "            __lifetime: std::marker::PhantomData,\n        };\n        let result = __moss_body_" << d.name << "_" << h.name << "(&mut state";
      for (const auto& p : h.params) o << ", " << p.name;
      o << ");\n";
      if (h.reply_type) o << "        if result.is_none() { std::process::abort(); }\n";
      typed_event(o, "handler_complete", *hp);
      for (auto entry = hp->class_modes.rbegin(); entry != hp->class_modes.rend(); ++entry) {
        string rank = std::to_string(plan->classes.at(entry->first).class_rank);
        bool exclusive = entry->second == SynchronizationMode::Exclusive;
        typed_event(o, "lock_releasing", *hp, rank, exclusive);
        o << "        drop(__class" << rank << ");\n";
        typed_event(o, "lock_release", *hp, rank, exclusive);
      }
      typed_event(o, "handler_exit", *hp);
      o << "        result\n    }\n";
      o << "}\n";
    }
    o << (d.exported ? "pub " : "") << "fn construct_" << snake_case(d.name) << "(";
    for (const auto& r : d.routes) o << r.name << ": " << rust_type(r.type) << ", ";
    for (const auto& f : d.state) o << f.name << ": " << rust_type(f.type) << ", ";
    o << "__identity: &'static str, __rank: usize";
    for (const auto& h : d.handlers) o << ", __handler_" << h.name << ": &'static str";
    o << ") -> " << d.name << "Ref {\n";
    if (!plan) { o << "    std::process::abort()\n}\n"; return; }
    o << "    __moss_require_send::<" << d.name << "Ref>();\n";
    for (const auto& leaf : leaves) o << "    __moss_require_send::<" << rust_type(leaf.second) << ">();\n";
    for (const auto& h : d.handlers) {
      for (const auto& p : h.params) o << "    __moss_require_send::<" << rust_type(p.type) << ">();\n";
      if (h.reply_type) o << "    __moss_require_send::<" << rust_type(*h.reply_type) << ">();\n";
    }
    for (const auto& f : d.state) decompose_physical_value(o, f.name, f.type, f.name);
    o << "    " << d.name << "Ref { state: Arc::new(" << d.name << "Runtime {\n";
    for (const auto& cls : plan->classes) {
      o << "        class" << cls.class_rank << ": std::sync::RwLock::new(" << d.name << "Class" << cls.class_rank << " { ";
      for (const auto& leaf : cls.member_leaves) o << physical_leaf_binding(leaf) << ", ";
      o << "}),\n";
    }
    o << "        immutable: " << d.name << "Immutable { ";
    for (const auto& leaf : leaves) if (!plan->protected_mutable_leaves.count(leaf.first)) o << physical_leaf_binding(leaf.first) << ", ";
    o << "},\n        #[cfg(any(test, moss_perf))] identity: __identity,\n        #[cfg(any(test, moss_perf))] rank: __rank,\n";
    for (const auto& h : d.handlers)
      o << "        #[cfg(any(test, moss_perf))] handler_" << h.name << ": __handler_" << h.name << ",\n";
    o << "    }), ";
    for (const auto& r : d.routes) o << r.name << ", ";
    o << "}\n}\n";
  }

  void gen_domain_body_abi(std::ostringstream& o, const Domain& d) {
    // Route contracts have semantic methods only: no lock layout crosses crates.
    o << ((d.exported || p_.explicit_module) ? "pub " : "") << "trait " << d.name << "Messages {\n";
    for (const auto& h : d.handlers) {
      for (const string& prefix : {string(""), string("__moss_message_")}) {
        o << "    fn " << prefix << h.name << (prefix.empty() ? "_shared" : "") << "(&self";
        for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
        o << ") -> " << (prefix.empty() ? (h.reply_type ? "Option<" + rust_type(*h.reply_type) + ">" : "()") : rust_type(h.reply_type.value_or("unit"))) << ";\n";
      }
    }
    o << "}\n";
    const auto* plan = typed_domain_plan(d);
    for (const auto& h : d.handlers) {
      HandlerSynchronizationPlan fallback;
      fallback.name = h.name;
      if (h.state_effects) fallback.effects = *h.state_effects;
      const HandlerSynchronizationPlan* hp = &fallback;
      if (plan) for (const auto& candidate : plan->handlers) if (candidate.name == h.name) hp = &candidate;
      if (!plan && !h.state_effects) continue; // Uninstantiated inferred template.
      string state_type = d.name + h.name + "State";
      string generic = "'a", args = "'a", bodyargs = "'_";
      for (const auto& f : d.state) if (typed_field_used(f, *hp) && view_object_type(f.type)) {
        generic += ", F_" + f.name + ": " + access_trait(f.type);
        args += ", F_" + f.name;
        bodyargs += ", impl " + access_trait(f.type);
      }
      for (const auto& r : d.routes) {
        generic += ", R_" + r.name + ": " + canonical_type_name(r.type) + "Messages";
        args += ", R_" + r.name;
        bodyargs += ", impl " + canonical_type_name(r.type) + "Messages";
      }
      o << ((d.exported || p_.explicit_module) ? "pub " : "") << "struct " << state_type << "<" << generic << "> {\n";
      for (const auto& f : d.state) if (typed_field_used(f, *hp)) {
        bool writes = false;
        for (const auto* set : {&hp->effects.writes, &hp->effects.consumes})
          for (const auto& leaf : *set) writes |= leaf == f.name || starts_with(leaf, f.name + ".");
        o << "    pub " << f.name << ": ";
        if (view_object_type(f.type)) o << "F_" << f.name;
        else o << "&'a " << (writes ? "mut " : "") << rust_type(f.type);
        o << ",\n";
      }
      for (const auto& r : d.routes) o << "    pub " << r.name << ": &'a R_" << r.name << ",\n";
      o << "    pub __lifetime: std::marker::PhantomData<&'a ()>,\n}\n";
      string semantic_identity = handler_semantic_identity(d, h);
      string native = tooling_native_symbol("handler", d.name + "__" + h.name, semantic_identity);
      tooling_begin(o, 0, "handler", semantic_identity, "__moss_body_" + d.name + "_" + h.name, native);
      source_comment(o, 0, h.line, handler_signature(h));
      // Generic provider bodies are monomorphized in the final application;
      // exported native symbols are possible only for nongeneric bodies.
      bool generic_body = bodyargs != "'_";
      debug_symbol_attributes(o, 0, native, !generic_body);
      o << ((d.exported || p_.explicit_module) ? "pub " : "") << "fn __moss_body_" << d.name << "_" << h.name
        << "(state: &mut " << state_type << "<" << bodyargs << ">";
      for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
      o << ") -> " << (h.reply_type ? "Option<" + rust_type(*h.reply_type) + ">" : "()") << " {\n";
      string result = reply_binding(d, h);
      if (h.reply_type) o << "    let mut " << result << " = None;\n";
      o << "    " << (handler_needs_label(h) ? "'handler: " : "") << "{\n";
      std::set<string> locals{result};
      std::unordered_map<string,string> types{{"self", d.name}};
      for (const auto& f : d.state) types[f.name] = f.type;
      for (const auto& r : d.routes) types[r.name] = r.type;
      for (const auto& p : h.params) { locals.insert(p.name); types[p.name] = p.type; }
      view_domain_ = true; view_domain_access_ = true;
      gen_stmts(o, h.body, &d, &h, result, locals, types, 2, true, false, functional_handler_context(d, h));
      view_domain_ = false; view_domain_access_ = false;
      o << "    }\n";
      if (h.reply_type) o << "    " << result << "\n";
      o << "}\n"; tooling_end(o, 0, semantic_identity);
    }
  }

  void gen_route_contract(std::ostringstream& o, const Domain& d, const string& handle) {
    o << "impl " << d.name << "Messages for " << handle << " {\n";
    for (const auto& h : d.handlers) {
      bool compatible = true;
      if (handle == d.name + "Handle") for (const auto& candidate : specialized_domains_) {
        if (specialization_sources_.at(candidate.name) != d.name) continue;
        const auto* other = find_handler(candidate, h.name);
        if (!other || other->reply_type != h.reply_type || other->params.size() != h.params.size()) { compatible = false; continue; }
        for (size_t i = 0; i < h.params.size(); ++i)
          compatible &= rust_type(h.params[i].type) == rust_type(other->params[i].type);
      }
      for (const string& prefix : {string(""), string("__moss_message_")}) {
        o << "    fn " << prefix << h.name << (prefix.empty() ? "_shared" : "") << "(&self";
        for (const auto& p : h.params) o << ", " << p.name << ": " << rust_type(p.type);
        o << ") -> " << (prefix.empty() ? (h.reply_type ? "Option<" + rust_type(*h.reply_type) + ">" : "()") : rust_type(h.reply_type.value_or("unit")))
          << " { ";
        if (!compatible) { o << "std::process::abort() }\n"; continue; }
        o << "self." << h.name << "_shared(";
        for (const auto& p : h.params) o << p.name << ", ";
        o << ")";
        if (!prefix.empty() && h.reply_type) o << ".unwrap_or_else(|| std::process::abort())";
        o << " }\n";
      }
    }
    o << "}\n";
  }
