// Compiler facts for expressions whose size is fixed by source construction.
// This query is shared semantic machinery; FileIO only consumes its result.
  std::map<string, string> static_bound_bindings_;

  struct StaticBindingScope {
    std::map<string, string>& bindings;
    std::map<string, string> saved;
    explicit StaticBindingScope(std::map<string, string>& value)
        : bindings(value), saved(value) {}
    ~StaticBindingScope() { bindings = std::move(saved); }
  };

  void static_mark_possible_mutation(string expression,
                                     const std::set<string>& fileio_owners,
                                     std::set<string>& mutated) const {
    expression = strip_redundant_outer_parentheses(trim(expression));
    for (const auto& prefix : vector<string>{"echo ", "reply ",
                                              "return ", "message "})
      if (starts_with(expression, prefix))
        expression = trim(expression.substr(prefix.size()));
    string receiver, method;
    vector<string> args;
    if (parse_member_call(expression, receiver, method, args)) {
      bool fileio_borrow = fileio_owners.count(receiver) &&
          (method == "read" || method == "write" || method == "chunks" ||
           method == "open" || method == "sync" || method == "close");
      if (!fileio_borrow) {
        mutated.insert(receiver);
        for (const auto& argument : args)
          if (simple_identifier(trim(argument))) mutated.insert(trim(argument));
      }
      for (const auto& argument : args)
        static_mark_possible_mutation(argument, fileio_owners, mutated);
      return;
    }
    string callee;
    if (parse_simple_call(expression, callee, args)) {
      if (!objects_.count(callee) && callee != "range" && callee != "Some")
        for (const auto& argument : args)
          if (simple_identifier(trim(argument))) mutated.insert(trim(argument));
      for (const auto& argument : args) {
        string name, value;
        static_mark_possible_mutation(
            parse_named_argument(argument, name, value) ? value : argument,
            fileio_owners, mutated);
      }
      return;
    }
    for (const auto& operators : vector<vector<string>>{
             {" or ", " and "}, {"==", "!=", "<=", ">=", "<", ">"},
             {"+", "-"}, {"*", "/", "%"}})
      if (auto binary = split_binary(expression, operators)) {
        static_mark_possible_mutation(binary->first, fileio_owners, mutated);
        static_mark_possible_mutation(binary->second, fileio_owners, mutated);
        return;
      }
    if (starts_with(expression, "[") && ends_with(expression, "]"))
      for (const auto& item : split_top_level(
               expression.substr(1, expression.size() - 2), ','))
        static_mark_possible_mutation(item, fileio_owners, mutated);
  }

  void static_bindings_for_body(const vector<Stmt>& statements,
                                const TypeEnv& entry) {
    static_bound_bindings_.clear();
    std::map<string, int> writes;
    std::set<string> mutated;
    std::set<string> fileio_owners;
    for (const auto& binding : entry)
      if (canonical_type_name(binding.second) == "FileIO")
        fileio_owners.insert(binding.first);
    for (const auto& statement : statements)
      if ((statement.kind == Stmt::Kind::Let ||
           statement.kind == Stmt::Kind::Var ||
           statement.kind == Stmt::Kind::Assign) &&
          starts_with(trim(statement.b), "FileIO.open("))
        fileio_owners.insert(statement.a);
    for (const auto& statement : statements) {
      if (statement.kind == Stmt::Kind::Let ||
          statement.kind == Stmt::Kind::Var ||
          statement.kind == Stmt::Kind::Assign) {
        if (simple_identifier(statement.a)) ++writes[statement.a];
        string base, index;
        if (parse_index(statement.a, base, index)) mutated.insert(base);
      }
      if (statement.kind == Stmt::Kind::Call) {
        if (!statement.b.empty()) mutated.insert(statement.a);
        else for (const auto& argument : statement.args)
          if (simple_identifier(trim(argument))) mutated.insert(trim(argument));
      }
      string expression = statement.kind == Stmt::Kind::Call ||
          statement.kind == Stmt::Kind::Raw ||
          statement.kind == Stmt::Kind::Echo ||
          statement.kind == Stmt::Kind::Message ? statement.text :
          statement.kind == Stmt::Kind::Return ||
          statement.kind == Stmt::Kind::Reply ||
          statement.kind == Stmt::Kind::If ||
          statement.kind == Stmt::Kind::While ||
          statement.kind == Stmt::Kind::Match ? statement.a : statement.b;
      static_mark_possible_mutation(expression, fileio_owners, mutated);
    }
    for (const auto& statement : statements)
      if (statement.indent == 0 &&
          (statement.kind == Stmt::Kind::Let ||
           statement.kind == Stmt::Kind::Var ||
           statement.kind == Stmt::Kind::Assign) &&
          simple_identifier(statement.a) && writes[statement.a] == 1 &&
          !entry.count(statement.a) && !mutated.count(statement.a))
        static_bound_bindings_[statement.a] = statement.b;
  }

  string static_source_expression(string source, int depth = 0) const {
    source = trim(source);
    if (depth > 32) return source;
    auto found = static_bound_bindings_.find(source);
    return found == static_bound_bindings_.end() ? source :
        static_source_expression(found->second, depth + 1);
  }

  std::optional<int64_t> static_exact_integer(string source,
                                              int depth = 0) const {
    if (depth > 32) return std::nullopt;
    source = strip_redundant_outer_parentheses(
        static_source_expression(source));
    if (source.empty()) return std::nullopt;
    // Split at the lowest precedence first, using the checker's expression
    // splitter so this follows the language's arithmetic syntax.
    for (const auto& operators : vector<vector<string>>{{"+", "-"},
                                                          {"*", "/", "%"}}) {
      if (auto binary = split_binary(source, operators)) {
        auto left = static_exact_integer(binary->first, depth + 1);
        auto right = static_exact_integer(binary->second, depth + 1);
        if (!left || !right) return std::nullopt;
        int64_t answer = 0;
        // Recover the actual operator from the original split boundary.
        size_t at = binary->first.size();
        while (at < source.size() && std::isspace(
            static_cast<unsigned char>(source[at]))) ++at;
        char operation = at < source.size() ? source[at] : '\0';
        if (operation == '+') {
          if (__builtin_add_overflow(*left, *right, &answer)) return std::nullopt;
        } else if (operation == '-') {
          if (__builtin_sub_overflow(*left, *right, &answer)) return std::nullopt;
        } else if (operation == '*') {
          if (__builtin_mul_overflow(*left, *right, &answer)) return std::nullopt;
        } else if (operation == '/' || operation == '%') {
          if (*right == 0 || (*left == INT64_MIN && *right == -1))
            return std::nullopt;
          answer = operation == '/' ? *left / *right : *left % *right;
        } else return std::nullopt;
        return answer;
      }
    }
    try {
      size_t used = 0;
      int64_t value = std::stoll(source, &used, 0);
      if (used == source.size()) return value;
    } catch (const std::exception&) {}
    return std::nullopt;
  }

  std::optional<uint64_t> static_upper_bound(const string& source) const {
    auto exact = static_exact_integer(source);
    if (!exact || *exact < 0) return std::nullopt;
    return static_cast<uint64_t>(*exact);
  }

  std::optional<uint64_t> static_payload_byte_bound(
      const string& source) const {
    string value = static_source_expression(source);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
      return static_cast<uint64_t>(value.size() - 2);
    if (starts_with(value, "[") && ends_with(value, "]")) {
      string items = trim(value.substr(1, value.size() - 2));
      if (items.empty()) return uint64_t{0};
      auto elements = split_top_level(items, ',');
      for (const auto& element : elements) {
        auto byte = static_exact_integer(element);
        if (!byte || *byte < 0 || *byte > 255) return std::nullopt;
      }
      // The source value is a Vector[Int]. Even if lowering chooses a packed
      // byte representation, eight bytes per element is a safe byte extent.
      if (elements.size() > UINT64_MAX / sizeof(int64_t)) return std::nullopt;
      return static_cast<uint64_t>(elements.size()) * sizeof(int64_t);
    }
    return std::nullopt;
  }

  std::optional<vector<string>> static_collection_entries(
      const string& source) const {
    string value = static_source_expression(source);
    if (!starts_with(value, "[") || !ends_with(value, "]"))
      return std::nullopt;
    string items = trim(value.substr(1, value.size() - 2));
    return items.empty() ? vector<string>{} : split_top_level(items, ',');
  }
