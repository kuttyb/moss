  void validate_borrowed_view_write_effects() const {
    auto reject = [&](int line, const string& callable,
                      const string& parameter, const string& type,
                      Effect effect, bool writes) {
      if (canonical_type_name(type) != "range" &&
          canonical_type_name(type) != "rangebatch") return;
      if (effect != Effect::Write && !writes) return;
      err(line, "parameter '" + parameter + "' of type '" + type +
          "' is a borrowed FileIO view and cannot have WRITE access in " +
          callable, "FILEIO_VIEW_PARAMETER_WRITE");
    };

    for (const auto& function : p_.functions) {
      for (size_t index = 0; index < function.params.size(); ++index) {
        Effect effect = index < function.parameter_effects.size()
            ? function.parameter_effects[index] : Effect::Read;
        bool writes = index < function.parameter_mutations.size() &&
            function.parameter_mutations[index];
        reject(function.line, "fn " + function.name,
               function.params[index].name, function.params[index].type,
               effect, writes);
      }
      for (const auto& specialization : function.specializations) {
        for (size_t index = 0; index < specialization.parameter_types.size(); ++index) {
          Effect effect = index < specialization.parameter_effects.size()
              ? specialization.parameter_effects[index] : Effect::Read;
          bool writes = index < specialization.parameter_mutations.size() &&
              specialization.parameter_mutations[index];
          string parameter = index < function.params.size()
              ? function.params[index].name : "#" + std::to_string(index + 1);
          reject(function.line,
                 "specialization of fn " + function.name, parameter,
                 specialization.parameter_types[index], effect, writes);
        }
      }
    }
    for (const auto& object : p_.objects) {
      for (const auto& method : object.methods) {
        for (size_t index = 0; index < method.params.size(); ++index) {
          Effect effect = index < method.parameter_effects.size()
              ? method.parameter_effects[index] : Effect::Read;
          reject(method.line, "method " + object.name + "." + method.name,
                 method.params[index].name, method.params[index].type,
                 effect, false);
        }
      }
    }
  }
