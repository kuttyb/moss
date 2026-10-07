// Compiler facts for expressions whose size is fixed by source construction.
// This query is shared semantic machinery; FileIO only consumes its result.
  struct StaticBoundFact {
    const Stmt* binding_origin = nullptr;
    std::optional<int64_t> integer;
    std::optional<uint64_t> bytes;
    std::optional<vector<string>> entries;
    std::optional<vector<std::optional<int64_t>>> request_sizes;
    std::optional<vector<uint64_t>> batch_entries;
    bool operator==(const StaticBoundFact& other) const {
      return binding_origin == other.binding_origin &&
          integer == other.integer && bytes == other.bytes &&
          entries == other.entries && request_sizes == other.request_sizes &&
          batch_entries == other.batch_entries;
    }
  };
  using StaticBoundEnv = std::map<string, StaticBoundFact>;
  using StaticBoundSnapshots = std::map<const Stmt*, StaticBoundEnv>;
  StaticBoundEnv static_bound_bindings_;

  struct StaticBindingScope {
    StaticBoundEnv& bindings;
    StaticBoundEnv saved;
    explicit StaticBindingScope(StaticBoundEnv& value)
        : bindings(value), saved(value) {}
    ~StaticBindingScope() { bindings = std::move(saved); }
  };

  bool is_direct_fileio_open_initializer(
      const string& expression, vector<string>* open_args = nullptr) const {
    string initializer = strip_redundant_outer_parentheses(trim(expression));
    string receiver, method;
    vector<string> args;
    bool direct = parse_member_call(initializer, receiver, method, args) &&
        receiver == "FileIO" && method == "open" && args.size() == 2;
    if (direct && open_args) *open_args = std::move(args);
    return direct;
  }

  void static_mark_possible_mutation(string expression,
                                     const std::set<string>& fileio_owners,
                                     std::set<string>& mutated) const {
    expression = strip_redundant_outer_parentheses(trim(expression));
    for (const auto& prefix : vector<string>{"echo ", "reply ",
                                              "return ", "message "})
      if (starts_with(expression, prefix))
        expression = trim(expression.substr(prefix.size()));
    // Moss statements can contain expression sequences (notably echo's
    // comma-separated operands). Inspect every component before trying to
    // recognize a single call; otherwise a later call can mutate a value
    // while an earlier component hides it from the bound proof.
    auto sequence = split_top_level(expression, ',');
    if (sequence.size() > 1) {
      for (const auto& item : sequence)
        static_mark_possible_mutation(item, fileio_owners, mutated);
      return;
    }
    // Split compound expressions before call recognition: the textual call
    // parsers must not swallow `first() + grow(reqs, n)` as one call.
    for (const auto& operators : vector<vector<string>>{
             {" or ", " xor ", " and "}, {"==", "!=", "<=", ">=", "<", ">"},
             {"+", "-"}, {"*", "/", "%"}})
      if (auto binary = split_binary(expression, operators)) {
        static_mark_possible_mutation(binary->first, fileio_owners, mutated);
        static_mark_possible_mutation(binary->second, fileio_owners, mutated);
        return;
      }
    string receiver, method;
    vector<string> args;
    if (parse_member_call(expression, receiver, method, args)) {
      bool fileio_borrow = fileio_owners.count(receiver) &&
          (method == "read" || method == "write" || method == "chunks" ||
           method == "open" || method == "sync" || method == "close");
      bool non_mutating_member = method == "length" || method == "slice";
      if (!fileio_borrow && !non_mutating_member) {
        if (auto place = storage_location(receiver, {}, false))
          mutated.insert(place->root);
        for (const auto& argument : args) {
          string argument_name, argument_value;
          string actual = parse_named_argument(argument, argument_name,
                                                argument_value) ?
              trim(argument_value) : trim(argument);
          if (auto place = storage_location(actual, {}, false))
            mutated.insert(place->root);
        }
      }
      static_mark_possible_mutation(receiver, fileio_owners, mutated);
      for (const auto& argument : args) {
        string name, value;
        static_mark_possible_mutation(
            parse_named_argument(argument, name, value) ? value : argument,
            fileio_owners, mutated);
      }
      return;
    }
    string callee;
    if (parse_simple_call(expression, callee, args)) {
      if (!objects_.count(callee) && callee != "range" && callee != "Some")
        for (const auto& argument : args) {
          string argument_name, argument_value;
          string actual = parse_named_argument(argument, argument_name,
                                                argument_value) ?
              trim(argument_value) : trim(argument);
          if (auto place = storage_location(actual, {}, false))
            mutated.insert(place->root);
        }
      for (const auto& argument : args) {
        string name, value;
        static_mark_possible_mutation(
            parse_named_argument(argument, name, value) ? value : argument,
            fileio_owners, mutated);
      }
      return;
    }
    if (starts_with(expression, "not ") || starts_with(expression, "-")) {
      static_mark_possible_mutation(expression.substr(
          starts_with(expression, "not ") ? 4 : 1), fileio_owners, mutated);
      return;
    }
    string base, index;
    if (parse_index(expression, base, index)) {
      static_mark_possible_mutation(base, fileio_owners, mutated);
      static_mark_possible_mutation(index, fileio_owners, mutated);
      return;
    }
    auto dot = expression.rfind('.');
    if (dot != string::npos && simple_identifier(trim(expression.substr(dot + 1)))) {
      static_mark_possible_mutation(expression.substr(0, dot), fileio_owners, mutated);
      return;
    }
    if (starts_with(expression, "[") && ends_with(expression, "]"))
      for (const auto& item : split_top_level(
               expression.substr(1, expression.size() - 2), ','))
        static_mark_possible_mutation(item, fileio_owners, mutated);
  }

  // One statement-level mutation contract for both loop back-edge analysis
  // and the forward walk. Storage roots follow the ownership place parser,
  // including arbitrary alternating field/index projections.
  std::set<string> static_statement_mutation_roots(
      const Stmt& statement, const std::set<string>& fileio_owners) const {
    std::set<string> mutated;
    string expression;
    switch (statement.kind) {
      case Stmt::Kind::Let:
      case Stmt::Kind::Var:
      case Stmt::Kind::Assign:
        if (auto target = storage_location(statement.a, {}, false))
          mutated.insert(target->root);
        // Index expressions in a target are evaluated too.
        static_mark_possible_mutation(statement.a, fileio_owners, mutated);
        expression = statement.b;
        break;
      case Stmt::Kind::Call:
      case Stmt::Kind::Raw:
      case Stmt::Kind::Echo:
      case Stmt::Kind::Message:
        expression = statement.text;
        if (!statement.message_result.empty())
          mutated.insert(statement.message_result);
        break;
      case Stmt::Kind::Return:
      case Stmt::Kind::Reply:
      case Stmt::Kind::If:
      case Stmt::Kind::While:
      case Stmt::Kind::Match:
        expression = statement.a;
        break;
      case Stmt::Kind::For:
        expression = statement.b;
        break;
      default:
        break;
    }
    static_mark_possible_mutation(expression, fileio_owners, mutated);
    return mutated;
  }

  static StaticBoundEnv static_join_bounds(const StaticBoundEnv& left,
                                           const StaticBoundEnv& right) {
    StaticBoundEnv joined;
    for (const auto& binding : left) {
      auto other = right.find(binding.first);
      if (other != right.end() && other->second == binding.second)
        joined.insert(binding);
    }
    return joined;
  }

  void static_invalidate_mutation_roots(
      StaticBoundEnv& facts, const std::set<string>& roots) const {
    for (auto it = facts.begin(); it != facts.end();) {
      auto place = storage_location(it->first, {}, false);
      // Object method facts can use an implicit field name while canonical
      // ownership storage is rooted at self. Invalidate both spellings.
      if (roots.count(it->first) || (place && roots.count(place->root)))
        it = facts.erase(it);
      else
        ++it;
    }
  }

  std::set<string> static_loop_writes(const vector<Stmt>& statements,
                                     size_t first, int level,
                                     const std::set<string>& fileio_owners) const {
    std::set<string> writes;
    for (size_t i = first; i < statements.size() &&
                         statements[i].indent > level; ++i) {
      auto mutations = static_statement_mutation_roots(statements[i], fileio_owners);
      writes.insert(mutations.begin(), mutations.end());
    }
    return writes;
  }

  void static_bindings_block(const vector<Stmt>& statements, size_t& index,
                             int level, StaticBoundEnv& facts,
                             StaticBoundSnapshots& snapshots,
                             std::set<string>& fileio_owners) {
    while (index < statements.size() && statements[index].indent == level) {
      const Stmt& statement = statements[index];
      if (statement.kind == Stmt::Kind::Else ||
          statement.kind == Stmt::Kind::Case) return;
      std::set<string> loop_writes;
      if (statement.kind == Stmt::Kind::For ||
          statement.kind == Stmt::Kind::While) {
        loop_writes = static_loop_writes(statements, index + 1, level,
                                         fileio_owners);
        if (statement.kind == Stmt::Kind::While)
          static_invalidate_mutation_roots(facts, loop_writes);
      }
      if (statement.kind == Stmt::Kind::If ||
          statement.kind == Stmt::Kind::While ||
          statement.kind == Stmt::Kind::For ||
          statement.kind == Stmt::Kind::Match) {
        auto control_mutations = static_statement_mutation_roots(statement, fileio_owners);
        static_invalidate_mutation_roots(facts, control_mutations);
        if (!control_mutations.empty())
          for (auto it = facts.begin(); it != facts.end();) {
            if (it->second.entries || it->second.batch_entries)
              it = facts.erase(it);
            else ++it;
          }
      }
      static_bound_bindings_ = facts;
      snapshots[&statement] = facts;
      if (statement.kind == Stmt::Kind::If ||
          statement.kind == Stmt::Kind::While ||
          statement.kind == Stmt::Kind::For ||
          statement.kind == Stmt::Kind::Match) {
        StaticBoundEnv incoming = facts;
        StaticBoundEnv body = incoming;
        std::set<string> body_owners = fileio_owners;
        if (statement.kind == Stmt::Kind::For) {
          StaticBoundFact element;
          element.binding_origin = &statement;
          element.bytes = static_payload_byte_bound(statement.b);
          if (!element.bytes) {
            auto batch = static_batch_entry_bounds(statement.b);
            if (batch && !batch->empty())
              element.bytes = *std::max_element(batch->begin(), batch->end());
          }
          body.erase(statement.a);
          body_owners.erase(statement.a);
          if (element.bytes) body[statement.a] = element;
        }
        if (statement.kind == Stmt::Kind::For ||
            statement.kind == Stmt::Kind::While)
          static_invalidate_mutation_roots(body, loop_writes);
        ++index;
        if (statement.kind == Stmt::Kind::Match) {
          bool first = true;
          StaticBoundEnv joined;
          while (index < statements.size() &&
                 statements[index].indent == level + 1 &&
                 statements[index].kind == Stmt::Kind::Case) {
            const Stmt& arm = statements[index++];
            StaticBoundEnv branch = incoming;
            std::set<string> branch_owners = fileio_owners;
            for (const auto& name : arm.args) branch.erase(name);
            for (const auto& name : arm.args) branch_owners.erase(name);
            static_bindings_block(statements, index, level + 2, branch,
                                  snapshots, branch_owners);
            for (const auto& name : arm.args) {
              branch.erase(name);
              auto old = incoming.find(name);
              if (old != incoming.end()) branch[name] = old->second;
            }
            joined = first ? branch : static_join_bounds(joined, branch);
            first = false;
          }
          facts = first ? incoming : joined;
        } else {
          static_bindings_block(statements, index, level + 1, body,
                                snapshots, body_owners);
          if (statement.kind == Stmt::Kind::If &&
              index < statements.size() &&
              statements[index].indent == level &&
              statements[index].kind == Stmt::Kind::Else) {
            ++index;
            StaticBoundEnv alternative = incoming;
            std::set<string> alternative_owners = fileio_owners;
            static_bindings_block(statements, index, level + 1, alternative,
                                  snapshots, alternative_owners);
            facts = static_join_bounds(body, alternative);
          } else if (statement.kind == Stmt::Kind::For) {
            body.erase(statement.a);
            auto old = incoming.find(statement.a);
            if (old != incoming.end()) body[statement.a] = old->second;
            facts = static_join_bounds(incoming, body);
          } else {
            facts = static_join_bounds(incoming, body);
          }
        }
        continue;
      }
      std::optional<StaticBoundFact> new_fact;
      if ((statement.kind == Stmt::Kind::Let ||
           statement.kind == Stmt::Kind::Var ||
           statement.kind == Stmt::Kind::Assign) &&
          simple_identifier(statement.a)) {
        StaticBoundFact fact;
        string value = strip_redundant_outer_parentheses(trim(statement.b));
        if (simple_identifier(value)) {
          auto existing = facts.find(value);
          if (existing != facts.end()) fact = existing->second;
        } else {
          fact.integer = static_exact_integer(value);
          fact.bytes = static_payload_byte_bound(value);
          fact.entries = static_collection_entries(value);
          if (fact.entries) {
            bool requests = !fact.entries->empty();
            vector<std::optional<int64_t>> sizes;
            for (const auto& entry : *fact.entries) {
              auto request = static_batch_request(entry);
              if (!request) {
                requests = false;
                break;
              }
              sizes.push_back(static_exact_integer(request->second));
            }
            // Request values are captured at vector construction. Preserve
            // the original expression for type checking, and retain the
            // captured bound separately from any later alias rebinding.
            if (requests) fact.request_sizes = std::move(sizes);
          }
          fact.batch_entries = static_batch_entry_bounds(value);
        }
        if (fact.integer || fact.bytes || fact.entries ||
            fact.request_sizes || fact.batch_entries) {
          fact.binding_origin = &statement;
          new_fact = std::move(fact);
        }
      }
      auto mutated = static_statement_mutation_roots(statement, fileio_owners);
      static_invalidate_mutation_roots(facts, mutated);
      // Rebinding a plain name replaces its fact without mutating aliases.
      // A projected store or call can mutate shared collection storage.
      bool binding = (statement.kind == Stmt::Kind::Let ||
          statement.kind == Stmt::Kind::Var || statement.kind == Stmt::Kind::Assign) &&
          simple_identifier(statement.a);
      if (binding) mutated.erase(statement.a);
      if (!mutated.empty()) {
        // A collection alias may refer to the same mutable storage. Drop all
        // collection construction facts rather than guessing alias identity.
        for (auto it = facts.begin(); it != facts.end();) {
          if (it->second.entries || it->second.batch_entries)
            it = facts.erase(it);
          else ++it;
        }
      }
      if (new_fact) facts[statement.a] = std::move(*new_fact);
      if ((statement.kind == Stmt::Kind::Let ||
           statement.kind == Stmt::Kind::Var ||
           statement.kind == Stmt::Kind::Assign) &&
          simple_identifier(statement.a)) {
        fileio_owners.erase(statement.a);
        if (is_direct_fileio_open_initializer(statement.b))
          fileio_owners.insert(statement.a);
      }
      ++index;
    }
  }

  StaticBoundSnapshots static_bindings_for_body(const vector<Stmt>& statements,
                                                 const TypeEnv& entry) {
    StaticBoundSnapshots snapshots;
    StaticBoundEnv facts;
    std::set<string> fileio_owners;
    for (const auto& binding : entry)
      if (canonical_type_name(binding.second) == "FileIO")
        fileio_owners.insert(binding.first);
    size_t index = 0;
    static_bindings_block(statements, index, 0, facts, snapshots, fileio_owners);
    static_bound_bindings_ = std::move(facts);
    return snapshots;
  }

  string static_source_expression(string source, int depth = 0) const {
    (void)depth;
    return trim(source);
  }

  std::optional<int64_t> static_exact_integer(string source,
                                              int depth = 0) const {
    if (depth > 32) return std::nullopt;
    source = strip_redundant_outer_parentheses(
        static_source_expression(source));
    if (auto fact = static_bound_bindings_.find(source);
        fact != static_bound_bindings_.end()) return fact->second.integer;
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

  std::optional<vector<string>> static_collection_entries(
      const string& source) const {
    string value = static_source_expression(source);
    if (auto fact = static_bound_bindings_.find(value);
        fact != static_bound_bindings_.end()) return fact->second.entries;
    if (!starts_with(value, "[") || !ends_with(value, "]"))
      return std::nullopt;
    string items = trim(value.substr(1, value.size() - 2));
    return items.empty() ? vector<string>{} : split_top_level(items, ',');
  }

  std::optional<std::pair<string, string>> static_batch_request(
      string entry) const {
    entry = trim(entry);
    if (starts_with(entry, "(") && ends_with(entry, ")")) {
      auto coordinates = split_top_level(entry.substr(1, entry.size() - 2), ',');
      if (coordinates.size() == 2)
        return std::make_pair(coordinates[0], coordinates[1]);
    }
    string constructor;
    vector<string> fields;
    if (parse_simple_call(entry, constructor, fields) &&
        fileio_request_record_type(constructor) && fields.size() == 2) {
      std::map<string, string> named;
      for (const auto& field : fields) {
        string name, value;
        if (parse_named_argument(field, name, value)) named[name] = value;
      }
      if (named.count("offset") && named.count("size"))
        return std::make_pair(named["offset"], named["size"]);
    }
    return std::nullopt;
  }

  std::optional<vector<std::optional<int64_t>>> static_request_size_values(
      const string& source) const {
    string value = strip_redundant_outer_parentheses(trim(source));
    if (auto fact = static_bound_bindings_.find(value);
        fact != static_bound_bindings_.end()) return fact->second.request_sizes;
    auto entries = static_collection_entries(value);
    if (!entries) return std::nullopt;
    vector<std::optional<int64_t>> sizes;
    for (const auto& entry : *entries) {
      auto request = static_batch_request(entry);
      if (!request) return std::nullopt;
      sizes.push_back(static_exact_integer(request->second));
    }
    return sizes;
  }

  std::optional<vector<uint64_t>> static_batch_entry_bounds(
      const string& source) const {
    string value = strip_redundant_outer_parentheses(trim(source));
    if (auto fact = static_bound_bindings_.find(value);
        fact != static_bound_bindings_.end()) return fact->second.batch_entries;
    string receiver, method;
    vector<string> arguments;
    if (!parse_member_call(value, receiver, method, arguments) ||
        method != "read" || arguments.size() != 1) return std::nullopt;
    auto sizes = static_request_size_values(arguments[0]);
    if (!sizes) return std::nullopt;
    vector<uint64_t> bounds;
    for (const auto& size : *sizes) {
      if (!size || *size < 0) return std::nullopt;
      bounds.push_back(static_cast<uint64_t>(*size));
    }
    return bounds;
  }

  std::optional<uint64_t> static_payload_byte_bound(
      const string& source) const {
    string value = static_source_expression(source);
    if (auto fact = static_bound_bindings_.find(value);
        fact != static_bound_bindings_.end()) return fact->second.bytes;
    std::function<std::optional<uint64_t>(string)> bounded_string =
        [&](string expression) -> std::optional<uint64_t> {
      expression = strip_redundant_outer_parentheses(trim(expression));
      expression = static_source_expression(expression);
      if (expression.size() >= 2 && expression.front() == '"' &&
          expression.back() == '"')
        return static_cast<uint64_t>(expression.size() - 2);
      if (auto concatenation = split_binary(expression, {"+"})) {
        auto left = bounded_string(concatenation->first);
        auto right = bounded_string(concatenation->second);
        uint64_t total = 0;
        if (!left || !right || __builtin_add_overflow(*left, *right, &total))
          return std::nullopt;
        return total;
      }
      return std::nullopt;
    };
    if (auto bound = bounded_string(value)) return bound;
    string receiver, method;
    vector<string> arguments;
    if (parse_member_call(value, receiver, method, arguments)) {
      if (method == "read" && arguments.size() == 2)
        return static_upper_bound(arguments[1]);
      if (method == "chunks" && arguments.size() == 1)
        return static_upper_bound(arguments[0]);
      if (method == "slice" && arguments.size() == 2) {
        auto source_bound = static_payload_byte_bound(receiver);
        auto requested_len = static_upper_bound(arguments[1]);
        if (source_bound && requested_len) return std::min(*source_bound, *requested_len);
        if (source_bound) return *source_bound;
        if (requested_len) return *requested_len;
      }
    }
    string base, index_str;
    if (parse_index(value, base, index_str)) {
      auto entry_bounds = static_batch_entry_bounds(base);
      if (entry_bounds && !entry_bounds->empty()) {
        if (auto exact_idx = static_exact_integer(index_str)) {
          if (*exact_idx >= 0 &&
              static_cast<size_t>(*exact_idx) < entry_bounds->size())
            return (*entry_bounds)[static_cast<size_t>(*exact_idx)];
        }
        return *std::max_element(entry_bounds->begin(), entry_bounds->end());
      }
    }
    return std::nullopt;
  }
