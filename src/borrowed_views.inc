// Backend-only static access traits let owned values and borrowed state views
// share ordinary helper/method bodies. No dynamic dispatch or source trait is added.
  bool view_object_type(const string& type) const {
    auto found = objects_.find(canonical_type_name(type));
    return found != objects_.end() && !found->second->fields.empty();
  }

  string access_trait(const string& type) const {
    return "MossAccess_" + canonical_type_name(type);
  }

  // Return a view-aware place only for checked paths rooted in state or a
  // generic access parameter. This is representation selection, not effects.
  std::optional<string> view_place(const string& path, const Domain* d,
      const std::set<string>& locals, const std::unordered_map<string,string>* types) const {
    auto parts = split_top_level(trim(path), '.');
    if (parts.empty() || !std::all_of(parts.begin(), parts.end(), plain_identifier)) return std::nullopt;
    string root = parts.front(), type, rendered;
    bool access = false;
    size_t next = 1;
    if (view_parameters_.count(root)) {
      if (!types || !types->count(root)) return std::nullopt;
      type = types->at(root); rendered = root; access = true;
    } else if (view_method_ && d && (root == "self" || !locals.count(root))) {
      if (root == "self") { type = d->name; rendered = "self"; }
      else {
        auto field = std::find_if(d->state.begin(), d->state.end(), [&](const auto& f) { return f.name == root; });
        if (field == d->state.end()) return std::nullopt;
        type = field->type;
        rendered = "self.__moss_field_" + root + (mutable_projection_ ? "_mut()" : "()");
      }
      access = true;
    } else if (view_domain_ && d && !locals.count(root)) {
      auto field = std::find_if(d->state.begin(), d->state.end(), [&](const auto& f) { return f.name == root; });
      if (field == d->state.end()) {
        auto route = std::find_if(d->routes.begin(), d->routes.end(), [&](const auto& r) { return r.name == root; });
        if (route != d->routes.end() && parts.size() == 1) return "state." + rust_identifier(root);
        return std::nullopt;
      }
      type = field->type; rendered = "state." + root;
      access = view_domain_access_ && view_object_type(type);
    } else return std::nullopt;
    for (size_t i = next; i < parts.size(); ++i) {
      auto object = objects_.find(canonical_type_name(type));
      if (object == objects_.end()) return std::nullopt;
      auto field = std::find_if(object->second->fields.begin(), object->second->fields.end(),
          [&](const auto& f) { return f.name == parts[i]; });
      if (field == object->second->fields.end()) return std::nullopt;
      rendered += access ? ".__moss_field_" + parts[i] + (mutable_projection_ ? "_mut()" : "()") : "." + rust_identifier(parts[i]);
      type = field->type;
    }
    if (view_object_type(type) && (!access || (view_domain_access_ && parts.size() == 1 && !view_parameters_.count(root)))) return rendered;
    return "*" + rendered;
  }

  string place_expr(const string& expression, const Domain* d, const std::set<string>& locals,
                    const std::unordered_map<string,string>* types) const {
    bool saved = mutable_projection_; mutable_projection_ = true;
    string result = expr(expression, d, locals, types);
    mutable_projection_ = saved;
    return result;
  }

  bool is_object_view(const string& expression, const Domain* d, const std::set<string>& locals,
                      const std::unordered_map<string,string>* types) const {
    return view_object_type(generated_expr_type(expression, types).value_or("")) &&
           view_place(expression, d, locals, types).has_value();
  }

  bool borrowed_view_expression(const string& expression, const Domain* d, const std::set<string>& locals,
      const std::unordered_map<string,string>* types) const {
    if (view_place(expression, d, locals, types)) return true;
    string base, index;
    return parse_index(trim(expression), base, index) && borrowed_view_expression(base, d, locals, types);
  }

  string viewed_function_name(const string& name, const vector<string>& args, const Domain* d,
      const std::set<string>& locals, const std::unordered_map<string,string>* types) const {
    string emitted = emitted_function_name(name, args, types);
    auto found = functions_.find(name);
    if (found == functions_.end()) return emitted;
    for (size_t i = 0; i < args.size(); ++i)
      if (function_effect(*found->second, i) != Effect::Consume && is_object_view(args[i], d, locals, types))
        return "__moss_view_" + emitted;
    return emitted;
  }

  string method_receiver_place(const string& receiver, const string& name, const vector<string>& args,
      const Domain* d, const std::set<string>& locals, const std::unordered_map<string,string>* types) const {
    auto type = generated_expr_type(receiver, types);
    vector<string> argument_types;
    for (const auto& arg : args) argument_types.push_back(generated_expr_type(arg, types).value_or(""));
    const Method* method = type ? resolve_object_method(objects_, canonical_type_name(*type), name,
                                                       argument_types, true, nullptr) : nullptr;
    bool writes = method && method->receiver_effect == Effect::Write;
    if (!method && type && (name == "push" || name == "pop" || name == "push_back" || name == "pop_front")) {
      string concrete = canonical_type_name(*type);
      writes = concrete == "vector" || concrete == "queue" || starts_with(concrete, "vector[") || starts_with(concrete, "queue[");
    }
    string result = writes ? write_call_place(receiver, d, locals, types) : read_call_place(receiver, d, locals, types);
    return starts_with(result, "*") ? "(" + result + ")" : result;
  }

  string viewed_method_name(const string& receiver, const string& name, const vector<string>& args,
      const Domain* d, const std::set<string>& locals, const std::unordered_map<string,string>* types) const {
    if (is_object_view(receiver, d, locals, types)) return name;
    for (const auto& argument : args)
      if (is_object_view(argument, d, locals, types)) return "__moss_view_" + name;
    return name;
  }

  void gen_object_access(std::ostringstream& o, const ObjectType& t, bool owns) {
    const string trait = access_trait(t.name);
    if (owns) {
      o << ((t.exported || p_.explicit_module) ? "pub " : "") << "trait " << trait << ": std::fmt::Debug {\n";
      for (const auto& field : t.fields) {
        string result = rust_type(field.type);
        if (view_object_type(field.type)) {
          o << "    type Field_" << field.name << ": " << access_trait(field.type) << ";\n";
          result = "Self::Field_" + field.name;
        }
        o << "    fn __moss_field_" << field.name << "(&self) -> &" << result << ";\n"
          << "    fn __moss_field_" << field.name << "_mut(&mut self) -> &mut " << result << ";\n";
      }
      o << "    fn __moss_value(&self) -> " << t.name << " { " << t.name << " { ";
      for (const auto& field : t.fields)
        o << rust_identifier(field.name) << ": self.__moss_field_" << field.name << "()."
          << (view_object_type(field.type) ? "__moss_value()" : "clone()") << ", ";
      o << "} }\n    fn __moss_replace(&mut self, value: " << t.name << ") {\n";
      for (const auto& field : t.fields) {
        if (view_object_type(field.type)) o << "        self.__moss_field_" << field.name << "_mut().__moss_replace(value." << field.name << ");\n";
        else o << "        *self.__moss_field_" << field.name << "_mut() = value." << field.name << ";\n";
      }
      o << "    }\n";
      // Methods are statically dispatched defaults, with the same checked Moss
      // bodies and source identities as their ordinary owned implementations.
      for (const auto& method : t.methods) {
        if (method.receiver_effect == Effect::Consume) continue;
        string identity = method_semantic_identity(t, method);
        tooling_begin(o, 4, "method", identity, trait + "::" + method.name);
        source_comment(o, 4, method.line, "fn " + method.name);
        debug_symbol_attributes(o, 4, "", false);
        o << "    fn " << method.name << (method.receiver_effect == Effect::Write ? "(&mut self" : "(&self");
        view_parameters_.clear(); write_through_parameters_.clear();
        for (size_t i = 0; i < method.params.size(); ++i) {
          const auto& p = method.params[i]; Effect effect = method_effect(method, i);
          bool object = effect != Effect::Consume && view_object_type(p.type);
          if (object) view_parameters_.insert(p.name);
          else if (effect == Effect::Write) write_through_parameters_.insert(p.name);
          bool borrow = effect != Effect::Consume && (effect == Effect::Write || borrowable_type(p.type));
          o << ", " << p.name << ": " << (borrow ? (effect == Effect::Write ? "&mut " : "&") : "")
            << (object ? "impl " + access_trait(p.type) : rust_type(p.type));
        }
        o << ")";
        if (method.return_type) o << " -> " << rust_type(*method.return_type);
        o << " {\n";
        Domain receiver; receiver.name = t.name; receiver.state = t.fields;
        std::set<string> locals;
        std::unordered_map<string,string> types{{"self", t.name}};
        for (const auto& field : t.fields) types[field.name] = field.type;
        for (const auto& p : method.params) { locals.insert(p.name); types[p.name] = p.type; }
        view_method_ = true;
        gen_stmts(o, method.body, &receiver, nullptr, "", locals, types, 2, false, true, functional_method_context(t, method));
        if (method.result_expression) {
          auto context = functional_method_context(t, method);
          auto pipeline = method.result_functional_pipeline_ids.find(context);
          o << "        " << expr(*method.result_expression, &receiver, locals, &types,
              pipeline == method.result_functional_pipeline_ids.end() ? 0 : pipeline->second) << "\n";
        }
        view_method_ = false; view_parameters_.clear(); write_through_parameters_.clear();
        o << "    }\n"; tooling_end(o, 4, identity);
      }
      o << "}\nimpl " << trait << " for " << t.name << " {\n";
      for (const auto& field : t.fields) {
        string result = rust_type(field.type);
        if (view_object_type(field.type)) o << "    type Field_" << field.name << " = " << result << ";\n";
        o << "    fn __moss_field_" << field.name << "(&self) -> &" << result << " { &self." << field.name << " }\n"
          << "    fn __moss_field_" << field.name << "_mut(&mut self) -> &mut " << result << " { &mut self." << field.name << " }\n";
      }
      o << "}\n";
      // `_shared` handlers accept a logical object access capability.  This
      // forwarding implementation lets an owned value lend `&T` directly to
      // that ABI, and also lets an incoming generic view forward as `&P`
      // without rebuilding an owned object.
      o << "impl<T: " << trait << " + ?Sized> " << trait << " for &T {\n";
      for (const auto& field : t.fields) {
        string result = rust_type(field.type);
        if (view_object_type(field.type)) {
          o << "    type Field_" << field.name << " = T::Field_" << field.name << ";\n";
          result = "Self::Field_" + field.name;
        }
        o << "    fn __moss_field_" << field.name << "(&self) -> &" << result
          << " { (*self).__moss_field_" << field.name << "() }\n"
          << "    fn __moss_field_" << field.name << "_mut(&mut self) -> &mut " << result
          << " { std::process::abort() }\n";
      }
      o << "}\n";
    }
    string generics, bounds;
    for (size_t i = 0; i < t.fields.size(); ++i) {
      if (i) { generics += ", "; bounds += ", "; }
      string arg = "F" + std::to_string(i);
      generics += arg;
      bounds += arg + ": ";
      const auto& field = t.fields[i];
      bounds += view_object_type(field.type) ? access_trait(field.type) :
          "std::ops::DerefMut<Target = " + rust_type(field.type) + "> + std::fmt::Debug";
    }
    o << "struct " << t.name << "View<" << generics << "> {\n";
    for (size_t i = 0; i < t.fields.size(); ++i)
      o << "    " << rust_identifier(t.fields[i].name) << ": F" << i << ",\n";
    o << "}\nimpl<" << bounds << "> std::fmt::Debug for " << t.name << "View<" << generics << "> {\n"
      << "    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n"
      << "        f.debug_struct(\"" << t.name << "\")";
    for (const auto& field : t.fields) o << ".field(\"" << field.name << "\", &self." << field.name << ")";
    o << ".finish()\n    }\n}\nimpl<" << bounds << "> " << trait << " for " << t.name << "View<" << generics << "> {\n";
    for (size_t i = 0; i < t.fields.size(); ++i) {
      const auto& field = t.fields[i];
      string result = rust_type(field.type), access = "self." + rust_identifier(field.name);
      if (view_object_type(field.type)) {
        result = "F" + std::to_string(i);
        o << "    type Field_" << field.name << " = " << result << ";\n";
      } else access = "*" + access;
      o << "    fn __moss_field_" << field.name << "(&self) -> &" << result << " { &" << access << " }\n"
        << "    fn __moss_field_" << field.name << "_mut(&mut self) -> &mut " << result << " { &mut " << access << " }\n";
    }
    o << "}\n\n";
  }
