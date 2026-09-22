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

  static vector<Stmt> path_branch_body(const Handler& h, bool then_branch) {
    if (h.body.empty() || h.body.front().kind != Stmt::Kind::If || h.body.front().indent != 0)
      return {};
    const auto& branch = h.body.front();
    size_t then_begin = 1, then_end = then_begin;
    while (then_end < h.body.size() && h.body[then_end].indent > branch.indent) ++then_end;
    size_t else_begin = then_end, else_end = then_end;
    if (then_end < h.body.size() && h.body[then_end].kind == Stmt::Kind::Else &&
        h.body[then_end].indent == branch.indent) {
      else_begin = then_end + 1; else_end = else_begin;
      while (else_end < h.body.size() && h.body[else_end].indent > branch.indent) ++else_end;
    }
    size_t begin = then_branch ? then_begin : else_begin;
    size_t end = then_branch ? then_end : else_end;
    vector<Stmt> result;
    for (size_t i = begin; i < end; ++i) {
      Stmt statement = h.body[i]; statement.indent -= branch.indent + 1;
      result.push_back(std::move(statement));
    }
    const size_t suffix = else_begin == then_end ? then_end : else_end;
    for (size_t i = suffix; i < h.body.size(); ++i) result.push_back(h.body[i]);
    return result;
  }

  HandlerSynchronizationPlan path_fragment_plan(
      const DomainSynchronizationPlan& plan, const HandlerSynchronizationPlan& whole,
      const StateLeafEffects& effects) const {
    HandlerSynchronizationPlan fragment;
    fragment.name = whole.name; fragment.handler_identity = whole.handler_identity;
    fragment.effects = effects;
    fragment.exclusive_set = effects.writes;
    fragment.exclusive_set.insert(effects.consumes.begin(), effects.consumes.end());
    for (const auto& leaf : effects.reads)
      if (plan.protected_mutable_leaves.count(leaf)) fragment.protected_read_set.insert(leaf);
    fragment.lock_set = fragment.exclusive_set;
    fragment.lock_set.insert(fragment.protected_read_set.begin(), fragment.protected_read_set.end());
    for (const auto& leaf : fragment.lock_set) {
      const size_t cls = plan.leaf_to_class.at(leaf);
      fragment.class_modes.emplace(cls, whole.class_modes.at(cls));
    }
    return fragment;
  }

  void emit_path_state_definition(std::ostringstream& o, const Domain& d,
                                  const HandlerSynchronizationPlan& hp,
                                  const string& state_type) {
    string generic = "'a", bodyargs = "'_";
    for (const auto& f : d.state) if (typed_field_used(f, hp) && view_object_type(f.type)) {
      generic += ", F_" + f.name + ": " + access_trait(f.type);
      bodyargs += ", impl " + access_trait(f.type);
    }
    for (const auto& r : d.routes) {
      generic += ", R_" + r.name + ": " + canonical_type_name(r.type) + "Messages";
      bodyargs += ", impl " + canonical_type_name(r.type) + "Messages";
    }
    o << "struct " << state_type << "<" << generic << "> {\n";
    for (const auto& f : d.state) if (typed_field_used(f, hp)) {
      bool writes = false;
      for (const auto* set : {&hp.effects.writes, &hp.effects.consumes})
        for (const auto& leaf : *set) writes |= leaf == f.name || starts_with(leaf, f.name + ".");
      o << "    " << f.name << ": ";
      if (view_object_type(f.type)) o << "F_" << f.name;
      else o << "&'a " << (writes ? "mut " : "") << rust_type(f.type);
      o << ",\n";
    }
    for (const auto& r : d.routes) o << "    " << r.name << ": &'a R_" << r.name << ",\n";
    o << "    __lifetime: std::marker::PhantomData<&'a ()>,\n}\n";
  }

  void emit_path_body(std::ostringstream& o, const Domain& d, const Handler& h,
                      const HandlerSynchronizationPlan& hp, const string& tag,
                      const vector<Stmt>& body) {
    const string state_type = d.name + h.name + tag + "State";
    emit_path_state_definition(o, d, hp, state_type);
    string bodyargs = "'_";
    for (const auto& f : d.state) if (typed_field_used(f, hp) && view_object_type(f.type))
      bodyargs += ", impl " + access_trait(f.type);
    for (const auto& r : d.routes)
      bodyargs += ", impl " + canonical_type_name(r.type) + "Messages";
    o << "fn __moss_body_" << d.name << "_" << h.name << "_" << tag
      << "(state: &mut " << state_type << "<" << bodyargs << ">";
    for (const auto& p : h.params) o << ", " << shared_message_parameter(p);
    o << ") -> " << (h.reply_type ? "Option<" + rust_type(*h.reply_type) + ">" : "()") << " {\n";
    string result = reply_binding(d, h);
    if (h.reply_type) o << "    let mut " << result << " = None;\n";
    o << "    " << (handler_needs_label(h) ? "'handler: " : "") << "{\n";
    std::set<string> locals{result}; std::unordered_map<string,string> types{{"self", d.name}};
    for (const auto& f : d.state) types[f.name] = f.type;
    for (const auto& r : d.routes) types[r.name] = r.type;
    for (const auto& p : h.params) { locals.insert(p.name); types[p.name] = p.type; }
    view_domain_ = true; view_domain_access_ = true; write_through_parameters_.clear();
    view_parameters_.clear(); borrowed_message_parameters_.clear();
    for (const auto& p : h.params) if (borrowed_message_type(p.type)) {
      borrowed_message_parameters_.insert(p.name);
      if (view_object_type(p.type)) view_parameters_.insert(p.name);
    }
    gen_stmts(o, body, &d, &h, result, locals, types, 2, true, false,
              functional_handler_context(d, h));
    view_domain_ = false; view_domain_access_ = false; borrowed_message_parameters_.clear();
    view_parameters_.clear(); write_through_parameters_.clear();
    o << "    }\n";
    if (h.reply_type) o << "    " << result << "\n";
    o << "}\n";
  }

  void emit_path_acquire(std::ostringstream& o, const HandlerSynchronizationPlan& h,
                         const DomainSynchronizationPlan& plan,
                         const std::map<size_t, SynchronizationMode>& modes,
                         int spaces) const {
    const string pad(spaces, ' ');
    for (const auto& entry : modes) {
      const size_t rank = plan.classes.at(entry.first).class_rank;
      const bool exclusive = entry.second == SynchronizationMode::Exclusive;
      const string r = std::to_string(rank), mode = exclusive ? "write" : "read";
      typed_event(o, "lock_acquire", h, r, exclusive);
      o << pad << "#[cfg(not(moss_perf))]\n" << pad << "let "
        << (exclusive ? "mut " : "") << "__class" << r
        << " = moss_" << mode << "_or_abort(&self.state.class" << r << ");\n"
        << pad << "#[cfg(moss_perf)]\n" << pad << "let "
        << (exclusive ? "mut " : "") << "__class" << r
        << " = match self.state.class" << r << ".try_" << mode << "() {\n"
        << pad << "    Ok(guard) => guard,\n" << pad
        << "    Err(std::sync::TryLockError::WouldBlock) => {\n";
      typed_event(o, "lock_contended", h, r, exclusive);
      o << pad << "        moss_" << mode << "_or_abort(&self.state.class" << r << ")\n"
        << pad << "    },\n" << pad << "    Err(_) => std::process::abort(),\n"
        << pad << "};\n" << pad << "let __abort" << r << " = MossAbortOnUnwind;\n";
      typed_event(o, "lock_acquired", h, r, exclusive);
    }
  }

  void emit_path_release(std::ostringstream& o, const HandlerSynchronizationPlan& h,
                         const DomainSynchronizationPlan& plan,
                         const std::map<size_t, SynchronizationMode>& modes) const {
    for (auto entry = modes.rbegin(); entry != modes.rend(); ++entry) {
      const string rank = std::to_string(plan.classes.at(entry->first).class_rank);
      const bool exclusive = entry->second == SynchronizationMode::Exclusive;
      typed_event(o, "lock_releasing", h, rank, exclusive);
      o << "        drop(__class" << rank << ");\n";
      typed_event(o, "lock_release", h, rank, exclusive);
    }
  }

  void emit_path_values_and_state(std::ostringstream& o, const Domain& d,
                                  const HandlerSynchronizationPlan& fragment,
                                  const DomainSynchronizationPlan& plan,
                                  const string& state_type) const {
    for (const auto& entry : fragment.class_modes)
      o << "        let __values" << entry.first << " = &"
        << (entry.second == SynchronizationMode::Exclusive ? "mut " : "")
        << "*__class" << entry.first << ";\n";
    o << "        let mut state = " << state_type << " {\n";
    for (const auto& f : d.state) if (typed_field_used(f, fragment))
      o << "            " << f.name << ": " << typed_view(f.name, f.type, plan, fragment, false) << ",\n";
    for (const auto& r : d.routes) o << "            " << r.name << ": &self." << r.name << ",\n";
    o << "            __lifetime: std::marker::PhantomData,\n        };\n";
  }

  void emit_path_sensitive_wrapper(std::ostringstream& o, const Domain& d, const Handler& h,
                                    const DomainSynchronizationPlan& plan,
                                    const HandlerSynchronizationPlan& hp) {
    const auto& placement = hp.path_placement;
    const auto condition = path_fragment_plan(plan, hp, placement.condition_effects);
    const auto then_fragment = path_fragment_plan(plan, hp, placement.then_effects);
    const auto else_fragment = path_fragment_plan(plan, hp, placement.else_effects);
    auto retained_entry = [&](const std::set<size_t>& cancelled) {
      auto result = placement.entry_modes;
      for (const auto rank : cancelled) result.erase(rank);
      return result;
    };
    typed_event(o, "handler_enter", hp);
    emit_path_acquire(o, hp, plan, placement.entry_modes, 8);
    o << "        let __abort_body = MossAbortOnUnwind;\n";
    // The condition view is scoped so rank-forced but untouched entry guards
    // can be cancelled in the selected continuation before any later acquire.
    o << "        let __moss_branch = {\n";
    emit_path_values_and_state(o, d, condition, plan, d.name + h.name + "ConditionState");
    std::set<string> locals; std::unordered_map<string,string> types{{"self", d.name}};
    for (const auto& f : d.state) types[f.name] = f.type;
    for (const auto& r : d.routes) types[r.name] = r.type;
    for (const auto& p : h.params) { locals.insert(p.name); types[p.name] = p.type; }
    view_domain_ = true; view_domain_access_ = true;
    o << "        " << expr(h.body.front().a, &d, locals, &types) << "\n        };\n";
    view_domain_ = false; view_domain_access_ = false;
    o << "        let result = if __moss_branch {\n";
    for (const auto rank : placement.cancel_then) {
      const string r = std::to_string(plan.classes.at(rank).class_rank);
      typed_event(o, "lock_cancelled", hp, r, false);
      o << "        drop(__class" << r << ");\n";
    }
    emit_path_acquire(o, hp, plan, placement.then_modes, 8);
    o << "        let __moss_then_result = {\n";
    emit_path_values_and_state(o, d, then_fragment, plan, d.name + h.name + "ThenState");
    o << "        let result = __moss_body_" << d.name << "_" << h.name << "_Then(&mut state";
    for (const auto& p : h.params) o << ", " << p.name;
    o << ");\n";
    if (h.reply_type) o << "        if result.is_none() { std::process::abort(); }\n";
    o << "        result\n        };\n";
    typed_event(o, "handler_complete", hp);
    emit_path_release(o, hp, plan, placement.then_modes);
    emit_path_release(o, hp, plan, retained_entry(placement.cancel_then));
    o << "        __moss_then_result\n";
    o << "        } else {\n";
    for (const auto rank : placement.cancel_else) {
      const string r = std::to_string(plan.classes.at(rank).class_rank);
      typed_event(o, "lock_cancelled", hp, r, false);
      o << "        drop(__class" << r << ");\n";
    }
    emit_path_acquire(o, hp, plan, placement.else_modes, 8);
    o << "        let __moss_else_result = {\n";
    emit_path_values_and_state(o, d, else_fragment, plan, d.name + h.name + "ElseState");
    o << "        let result = __moss_body_" << d.name << "_" << h.name << "_Else(&mut state";
    for (const auto& p : h.params) o << ", " << p.name;
    o << ");\n";
    if (h.reply_type) o << "        if result.is_none() { std::process::abort(); }\n";
    o << "        result\n        };\n";
    typed_event(o, "handler_complete", hp);
    emit_path_release(o, hp, plan, placement.else_modes);
    emit_path_release(o, hp, plan, retained_entry(placement.cancel_else));
    o << "        __moss_else_result\n";
    o << "        };\n";
    typed_event(o, "handler_exit", hp);
    o << "        result\n";
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
      for (const auto& p : h.params) o << ", " << shared_message_parameter(p);
      string ret = h.reply_type ? "Option<" + rust_type(*h.reply_type) + ">" : "()";
      o << ") -> " << ret << " {\n";
      if (!hp) { o << "        std::process::abort() // Uninstantiated source template.\n    }\n}\n"; continue; }
      if (hp->path_placement.enabled) {
        emit_path_sensitive_wrapper(o, d, h, *plan, *hp);
        o << "    }\n";
        o << "}\n";
        continue;
      }
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
        bool shared = prefix.empty();
        o << "    fn " << prefix << h.name << (prefix.empty() ? "_shared" : "") << "(&self";
        for (const auto& p : h.params)
          o << ", " << (shared ? shared_message_parameter(p)
                                 : p.name + ": " + rust_type(p.type));
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
      bool generic_body = bodyargs != "'_" || std::any_of(
          h.params.begin(), h.params.end(), [&](const Param& parameter) {
            return borrowed_message_type(parameter.type) &&
                view_object_type(parameter.type);
          });
      if (plan && hp->path_placement.enabled) o << "#[allow(dead_code)]\n";
      debug_symbol_attributes(o, 0, native, !generic_body);
      o << ((d.exported || p_.explicit_module) ? "pub " : "") << "fn __moss_body_" << d.name << "_" << h.name
        << "(state: &mut " << state_type << "<" << bodyargs << ">";
      for (const auto& p : h.params) o << ", " << shared_message_parameter(p);
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
      write_through_parameters_.clear(); view_parameters_.clear();
      borrowed_message_parameters_.clear();
      for (const auto& p : h.params) {
        if (!borrowed_message_type(p.type)) continue;
        borrowed_message_parameters_.insert(p.name);
        if (view_object_type(p.type)) view_parameters_.insert(p.name);
      }
      gen_stmts(o, h.body, &d, &h, result, locals, types, 2, true, false, functional_handler_context(d, h));
      view_domain_ = false; view_domain_access_ = false;
      borrowed_message_parameters_.clear(); view_parameters_.clear();
      write_through_parameters_.clear();
      o << "    }\n";
      if (h.reply_type) o << "    " << result << "\n";
      o << "}\n"; tooling_end(o, 0, semantic_identity);
      // The wrapper below selects one statically typed continuation.  The
      // duplicated suffix is intentional: it keeps a path-touched guard live
      // beyond the source CFG join without a runtime guard descriptor.
      if (plan && hp->path_placement.enabled) {
        auto condition = path_fragment_plan(*plan, *hp, hp->path_placement.condition_effects);
        auto then_fragment = path_fragment_plan(*plan, *hp, hp->path_placement.then_effects);
        auto else_fragment = path_fragment_plan(*plan, *hp, hp->path_placement.else_effects);
        emit_path_state_definition(o, d, condition, d.name + h.name + "ConditionState");
        emit_path_body(o, d, h, then_fragment, "Then", path_branch_body(h, true));
        emit_path_body(o, d, h, else_fragment, "Else", path_branch_body(h, false));
      }
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
        bool shared = prefix.empty();
        o << "    fn " << prefix << h.name << (prefix.empty() ? "_shared" : "") << "(&self";
        for (const auto& p : h.params)
          o << ", " << (shared ? shared_message_parameter(p)
                                 : p.name + ": " + rust_type(p.type));
        o << ") -> " << (prefix.empty() ? (h.reply_type ? "Option<" + rust_type(*h.reply_type) + ">" : "()") : rust_type(h.reply_type.value_or("unit")))
          << " { ";
        if (!compatible) { o << "std::process::abort() }\n"; continue; }
        o << "self." << h.name << "_shared(";
        for (const auto& p : h.params)
          o << shared_message_forward_argument(p, !shared) << ", ";
        o << ")";
        if (!prefix.empty() && h.reply_type) o << ".unwrap_or_else(|| std::process::abort())";
        o << " }\n";
      }
    }
    o << "}\n";
  }
