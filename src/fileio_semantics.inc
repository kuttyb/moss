// Phase 20's built-in capability surface. These helpers are members of Checker;
// ownership and state effects continue through the ordinary checker walks.
  static bool fileio_scoped_type(const string& type) {
    string t = canonical_type_name(type);
    return t == "FileIO" || t == "Range" || t == "RangeBatch" ||
           t == "seq[Range]";
  }

  static bool fileio_contains_scoped_type(const string& type) {
    string t = canonical_type_name(type);
    if (fileio_scoped_type(t)) return true;
    auto open = t.find('[');
    if (open == string::npos || !ends_with(t, "]")) return false;
    auto arguments = split_top_level(
        t.substr(open + 1, t.size() - open - 2), ',');
    for (const auto& argument : arguments)
      if (fileio_contains_scoped_type(argument)) return true;
    return false;
  }

  static bool fileio_forbidden_collection_type(const string& type) {
    string t = canonical_type_name(type);
    bool collection = starts_with(t, "vector[") || starts_with(t, "map[") ||
        starts_with(t, "queue[") || starts_with(t, "table[") ||
        starts_with(t, "seq[");
    return collection && fileio_contains_scoped_type(
        t.substr(t.find('[') + 1, t.size() - t.find('[') - 2));
  }

  // Conservative Phase 20 proof: integer literals and compile-time arithmetic
  // have finite upper bounds. Unknown runtime values are deliberately rejected.
  static std::optional<uint64_t> fileio_static_integer(const string& source) {
    string value = trim(source);
    if (value.empty()) return std::nullopt;
    try {
      size_t used = 0;
      long long number = std::stoll(value, &used, 0);
      if (used != value.size() || number < 0) return std::nullopt;
      return static_cast<uint64_t>(number);
    } catch (const std::exception&) { return std::nullopt; }
  }

  static bool fileio_is_negative_integer(const string& source) {
    string value = trim(source);
    if (value.empty()) return false;
    try {
      size_t used = 0;
      long long number = std::stoll(value, &used, 0);
      return used == value.size() && number < 0;
    } catch (const std::exception&) { return false; }
  }

  static std::optional<uint64_t> fileio_static_payload_bytes(const string& source) {
    string value = trim(source);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
      return static_cast<uint64_t>(value.size() - 2); // ASCII source literals.
    if (starts_with(value, "[") && ends_with(value, "]")) {
      string inside = value.substr(1, value.size() - 2);
      if (trim(inside).empty()) return uint64_t{0};
      auto items = split_top_level(inside, ',');
      return static_cast<uint64_t>(items.size());
    }
    return std::nullopt;
  }

  void fileio_require_bound(int line, const string& what,
                            const std::optional<uint64_t>& bound) {
    if (!bound) err(line, "FileIO " + what + " must have a statically provable finite bound",
                     "FILEIO_UNBOUNDED_REQUEST");
  }

  static std::optional<Effect> fileio_operation_effect(const string& method) {
    if (method == "read" || method == "chunks") return Effect::Read;
    if (method == "open" || method == "write" || method == "sync" ||
        method == "close") return Effect::Write;
    return std::nullopt;
  }

  std::optional<string> fileio_result_type(const string& expression,
                                            const TypeEnv& env) const {
    string receiver, method;
    vector<string> args;
    if (!parse_member_call(expression, receiver, method, args)) return std::nullopt;
    if (receiver == "FileIO" && method == "open") return string("FileIO");
    auto owner = inferred_expr_type(receiver, env);
    if (!owner) return std::nullopt;
    string type = canonical_type_name(*owner);
    if (type == "FileIO") {
      if (method == "read") {
        if (args.size() == 2) return string("Range");
        if (args.size() == 1) {
          string batch = trim(args[0]);
          if (starts_with(batch, "[") && ends_with(batch, "]"))
            return string("RangeBatch");
        }
        return std::nullopt;
      }
      if (method == "chunks") return string("seq[Range]");
      if (fileio_operation_effect(method)) return string("unit");
    }
    if (type == "RangeBatch" && method == "length") return string("int");
    if (type == "Range" && method == "length") return string("int");
    return std::nullopt;
  }

  bool check_fileio_operation(int line, const string& receiver,
                              const string& method, const vector<string>& args,
                              const TypeEnv& env) {
    bool constructor = receiver == "FileIO" && method == "open";
    auto owner = constructor ? std::optional<string>("FileIO") :
        inferred_expr_type(receiver, env);
    if (!owner) return false;
    string type = canonical_type_name(*owner);
    if (type != "FileIO" && type != "Range" && type != "RangeBatch")
      return false;
    auto fail = [&](const string& message) {
      err(line, message, "FILEIO_INVALID_OPERATION");
    };
    if (type == "Range" || type == "RangeBatch") {
      if (method != "length" || !args.empty()) fail(type + " supports only length()");
      return true;
    }
    if (!fileio_operation_effect(method)) fail("unknown FileIO operation '" + method + "'");
    if (constructor && args.size() != 2) fail("FileIO.open expects path and mode");
    if (!constructor && method == "open" && args.size() != 2)
      fail("FileIO.open expects path and mode");
    if (method == "read" && args.size() != 1 && args.size() != 2)
      fail("FileIO.read expects offset and size, or one batch");
    if (method == "write" && args.size() != 2)
      fail("FileIO.write expects offset and data");
    if (method == "chunks" && args.size() != 1)
      fail("FileIO.chunks expects a chunk size");
    if (method == "sync" && args.size() > 1) fail("FileIO.sync accepts at most dataonly");
    if (method == "close" && !args.empty()) fail("FileIO.close accepts no arguments");
    if (method == "sync" && args.size() == 1 && trim(args[0]) != "dataonly")
      fail("FileIO.sync accepts only dataonly");
    if ((method == "open" && args.size() != 2) ||
        (method == "read" && args.size() != 1 && args.size() != 2) ||
        (method == "write" && args.size() != 2) ||
        (method == "chunks" && args.size() != 1)) return true;
    if (method == "open") {
      auto path = inferred_expr_type(args[0], env);
      if (!path || canonical_type_name(*path) != "string")
        fail("FileIO.open path must be String");
      string mode = trim(args[1]);
      if (mode != "ro" && mode != "rw" && mode != "create")
        fail("FileIO mode must be ro, rw, or create");
    }
    if (method == "read" && args.size() == 2) {
      for (const auto& arg : args) {
        auto actual = inferred_expr_type(arg, env);
        if (!actual || canonical_type_name(*actual) != "int")
          fail("FileIO.read offset and size must be Int");
      }
    }
    if (method == "chunks") {
      auto actual = inferred_expr_type(args[0], env);
      if (!actual || canonical_type_name(*actual) != "int")
        fail("FileIO.chunks size must be Int");
    }
    if (method == "write") {
      auto offset = inferred_expr_type(args[0], env);
      if (!offset || canonical_type_name(*offset) != "int")
        fail("FileIO.write offset must be Int");
    }
    auto check_offset = [&](const string& expression) {
      auto value = fileio_static_integer(expression);
      if (fileio_is_negative_integer(expression))
        err(line, "FileIO offset must be nonnegative", "FILEIO_INVALID_BOUND");
      if (!value) err(line, "FileIO offset must be statically known and nonnegative",
                      "FILEIO_INVALID_BOUND");
    };
    auto check_size = [&](const string& expression, const string& what,
                          bool positive = false) {
      auto value = fileio_static_integer(expression);
      if (fileio_is_negative_integer(expression))
        err(line, "FileIO " + what + " must be nonnegative",
            "FILEIO_INVALID_BOUND");
      if (value && positive && *value == 0)
        err(line, "FileIO " + what + " must be positive", "FILEIO_INVALID_BOUND");
      fileio_require_bound(line, what, value);
    };
    if (method == "read" && args.size() == 2) {
      check_offset(args[0]);
      check_size(args[1], "read size");
    }
    if (method == "chunks") check_size(args[0], "chunk size", true);
    if (method == "write") {
      check_offset(args[0]);
      fileio_require_bound(line, "write payload",
                           fileio_static_payload_bytes(args[1]));
    }
    if (method == "read" && args.size() == 1) {
      string batch = trim(args[0]);
      if (starts_with(batch, "[") && ends_with(batch, "]")) {
        string entries = batch.substr(1, batch.size() - 2);
        auto requests = trim(entries).empty() ? vector<string>{} :
            split_top_level(entries, ',');
        fileio_require_bound(line, "batch count",
                             static_cast<uint64_t>(requests.size()));
        uint64_t total = 0;
        bool total_known = true;
        for (const auto& entry : requests) {
          string pair = trim(entry);
          if (!starts_with(pair, "(") || !ends_with(pair, ")")) {
            fail("batch entries must be (offset, size) pairs");
            continue;
          }
          auto coordinates = split_top_level(pair.substr(1, pair.size() - 2), ',');
          if (coordinates.size() != 2) {
            fail("batch entries must be (offset, size) pairs");
            continue;
          }
          for (const auto& coordinate : coordinates) {
            auto actual = inferred_expr_type(coordinate, env);
            if (!actual || canonical_type_name(*actual) != "int")
              fail("batch offset and size must be Int");
          }
          check_offset(coordinates[0]);
          auto size = fileio_static_integer(coordinates[1]);
          if (fileio_is_negative_integer(coordinates[1]))
            err(line, "FileIO batch entry size must be nonnegative",
                "FILEIO_INVALID_BOUND");
          fileio_require_bound(line, "batch entry size", size);
          if (size && UINT64_MAX - total >= *size) total += *size;
          else total_known = false;
        }
        fileio_require_bound(line, "batch total bytes",
            total_known ? std::optional<uint64_t>(total) : std::nullopt);
      } else {
        fail("one-argument FileIO.read requires a literal batch of (Int, Int) pairs");
      }
    }
    if (!constructor) check_expression(line, receiver, env);
    for (const auto& arg : args)
      if (trim(arg) != "ro" && trim(arg) != "rw" &&
          trim(arg) != "create" && trim(arg) != "dataonly")
        check_expression(line, arg, env);
    return true;
  }

  // Keep the capability's open/closed fact on the normal control-flow paths.
  // Value movement is rejected by the ordinary ownership walk.
  struct FileIOFlow {
    TypeEnv types;
    std::map<string, int> state;  // 0 closed, 1 open, 2 not statically known
    std::map<string, string> mode;
    std::set<string> local_owners;
    std::map<string, int> batch_generation;
    std::map<string, std::pair<string, int>> range_origin;
    bool continuing = true;
  };

  void fileio_require_closed(int line, const FileIOFlow& flow) const {
    for (const auto& owner : flow.local_owners) {
      auto found = flow.state.find(owner);
      if (found != flow.state.end() && found->second != 0)
        err(line, "root-local FileIO '" + owner +
            "' must be explicitly closed before normal exit",
            "FILEIO_MUST_CLOSE");
    }
  }

  static int fileio_join_state(int left, int right) {
    return left == right ? left : 2;
  }

  static FileIOFlow fileio_join_flow(const FileIOFlow& left,
                                    const FileIOFlow& right) {
    if (!left.continuing) return right;
    if (!right.continuing) return left;
    FileIOFlow merged = left;
    for (const auto& entry : right.state) {
      auto found = merged.state.find(entry.first);
      if (found == merged.state.end()) merged.state[entry.first] = entry.second;
      else found->second = fileio_join_state(found->second, entry.second);
    }
    for (const auto& entry : right.mode) {
      auto found = merged.mode.find(entry.first);
      if (found == merged.mode.end()) merged.mode[entry.first] = entry.second;
      else if (found->second != entry.second) found->second.clear();
    }
    merged.local_owners.insert(right.local_owners.begin(), right.local_owners.end());
    for (const auto& entry : right.types) merged.types.emplace(entry);
    for (const auto& entry : right.batch_generation) {
      auto found = merged.batch_generation.find(entry.first);
      if (found == merged.batch_generation.end())
        merged.batch_generation[entry.first] = entry.second;
      else if (found->second != entry.second)
        found->second = std::max(found->second, entry.second) + 1;
    }
    for (const auto& entry : right.range_origin) {
      auto found = merged.range_origin.find(entry.first);
      if (found == merged.range_origin.end()) merged.range_origin[entry.first] = entry.second;
      else if (found->second != entry.second) {
        merged.range_origin[entry.first] = entry.second;
        ++merged.batch_generation[entry.second.first];
      }
    }
    return merged;
  }

  void fileio_lifecycle_expression(int line, const string& expression,
                                   FileIOFlow& flow) const {
    string callee;
    vector<string> call_args;
    string member_receiver, member_method;
    vector<string> member_args;
    bool is_member_call = parse_member_call(trim(expression), member_receiver,
                                             member_method, member_args);
    if (!is_member_call && parse_simple_call(trim(expression), callee, call_args)) {
      auto function = functions_.find(callee);
      if (function != functions_.end() &&
          function->second->params.size() == call_args.size()) {
        FileIOFlow helper = flow;
        helper.local_owners.clear();
        helper.continuing = true;
        vector<std::pair<string, string>> aliases;
        for (size_t i = 0; i < call_args.size(); ++i) {
          const auto& parameter = function->second->params[i];
          auto actual = trim(call_args[i]);
          if (canonical_type_name(parameter.type) != "FileIO" ||
              !simple_identifier(actual)) continue;
          helper.types[parameter.name] = "FileIO";
          helper.state[parameter.name] = flow.state.count(actual)
              ? flow.state.at(actual) : 2;
          if (flow.mode.count(actual)) helper.mode[parameter.name] = flow.mode.at(actual);
          aliases.emplace_back(parameter.name, actual);
        }
        size_t helper_index = 0;
        check_fileio_lifecycle_block(function->second->body, helper_index, 0, helper);
        for (const auto& alias : aliases) {
          auto found = helper.state.find(alias.first);
          if (found != helper.state.end()) flow.state[alias.second] = found->second;
          auto mode = helper.mode.find(alias.first);
          if (mode != helper.mode.end()) flow.mode[alias.second] = mode->second;
        }
      }
      return;
    }
    string receiver, method;
    vector<string> args;
    if (!parse_member_call(trim(expression), receiver, method, args)) return;
    if (receiver == "FileIO") return;
    auto type = inferred_expr_type(receiver, flow.types);
    if (!type || canonical_type_name(*type) != "FileIO") return;
    if (!fileio_operation_effect(method)) return;
    auto owner = trim(receiver);
    auto found = flow.state.find(owner);
    int state = found == flow.state.end() ? 2 : found->second;
    if (method == "open") {
      if (state == 1)
        err(line, "FileIO '" + owner + "' is already open",
            "FILEIO_ALREADY_OPEN");
      flow.state[owner] = 1;
      if (args.size() == 2) flow.mode[owner] = trim(args[1]);
    } else {
      if (state == 0)
        err(line, "FileIO '" + owner + "' is closed during " + method,
            "FILEIO_CLOSED_OPERATION");
      if (method == "close") flow.state[owner] = 0;
      if (method == "write" && flow.mode[owner] == "ro")
        err(line, "read-only FileIO '" + owner + "' cannot be written",
            "FILEIO_READ_ONLY");
    }
  }

  void check_fileio_lifecycle_block(const vector<Stmt>& body, size_t& index,
                                    int indent, FileIOFlow& flow) const {
    while (index < body.size() && body[index].indent == indent) {
      const Stmt& s = body[index];
      if (!flow.continuing) { ++index; continue; }
      string use = s.kind == Stmt::Kind::Call || s.kind == Stmt::Kind::Raw ||
                   s.kind == Stmt::Kind::Echo || s.kind == Stmt::Kind::Message
          ? s.text : s.b.empty() ? s.a : s.b;
      for (const auto& origin : flow.range_origin)
        if (expression_uses(use, origin.first) &&
            flow.batch_generation[origin.second.first] != origin.second.second)
          err(s.line, "Range '" + origin.first +
              "' outlived its RangeBatch", "RANGE_BATCH_LIFETIME");
      if (s.kind == Stmt::Kind::Match) {
        FileIOFlow incoming = flow;
        FileIOFlow joined;
        joined.continuing = false;
        bool any_case = false;
        ++index;
        while (index < body.size() && body[index].indent == indent + 1 &&
               body[index].kind == Stmt::Kind::Case) {
          any_case = true;
          ++index;
          FileIOFlow arm = incoming;
          check_fileio_lifecycle_block(body, index, indent + 2, arm);
          joined = fileio_join_flow(joined, arm);
        }
        flow = any_case ? joined : incoming;
        continue;
      }
      if (s.kind == Stmt::Kind::If || s.kind == Stmt::Kind::While ||
          s.kind == Stmt::Kind::For) {
        FileIOFlow incoming = flow;
        ++index;
        FileIOFlow branch = incoming;
        bool range_batch_iteration = s.kind == Stmt::Kind::For &&
            canonical_type_name(inferred_expr_type(s.b, flow.types).value_or("")) ==
                "RangeBatch";
        if (range_batch_iteration)
          branch.range_origin[s.a] = {trim(s.b), branch.batch_generation[trim(s.b)]};
        check_fileio_lifecycle_block(body, index, indent + 1, branch);
        if (s.kind == Stmt::Kind::If && index < body.size() &&
            body[index].indent == indent && body[index].kind == Stmt::Kind::Else) {
          ++index;
          FileIOFlow alternative = incoming;
          check_fileio_lifecycle_block(body, index, indent + 1, alternative);
          flow = fileio_join_flow(branch, alternative);
        } else {
          flow = fileio_join_flow(incoming, branch);
        }
        if (s.kind == Stmt::Kind::For) flow.range_origin.erase(s.a);
        continue;
      }
      if (s.kind == Stmt::Kind::Else || s.kind == Stmt::Kind::Case) return;
      if (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var ||
          s.kind == Stmt::Kind::Assign) {
        fileio_lifecycle_expression(s.line, s.b, flow);
        auto type = inferred_expr_type(s.b, flow.types);
        if (type && canonical_type_name(*type) == "RangeBatch" &&
            storage_location(s.b, flow.types))
          err(s.line, "RangeBatch cannot be copied or moved from its owning scope",
              "RANGE_BATCH_LIFETIME");
        if (type && canonical_type_name(*type) == "RangeBatch")
          ++flow.batch_generation[s.a];
        string source, offset;
        if (type && canonical_type_name(*type) == "Range" &&
            parse_index(trim(s.b), source, offset) &&
            canonical_type_name(inferred_expr_type(source, flow.types).value_or("")) ==
                "RangeBatch")
          flow.range_origin[s.a] = {source, flow.batch_generation[source]};
        else if (type && canonical_type_name(*type) == "Range" &&
                 simple_identifier(trim(s.b)) &&
                 flow.range_origin.count(trim(s.b)))
          flow.range_origin[s.a] = flow.range_origin[trim(s.b)];
        else flow.range_origin.erase(s.a);
        if (type && canonical_type_name(*type) == "FileIO" &&
            starts_with(trim(s.b), "FileIO.open(")) {
          flow.types[s.a] = "FileIO";
          flow.local_owners.insert(s.a);
          flow.state[s.a] = 1;
          string receiver, method;
          vector<string> args;
          if (parse_member_call(trim(s.b), receiver, method, args) &&
              args.size() == 2) flow.mode[s.a] = trim(args[1]);
        } else if (type && simple_identifier(s.a)) {
          flow.types[s.a] = *type;
        }
      } else if (s.kind == Stmt::Kind::Call || s.kind == Stmt::Kind::Raw) {
        string call_text = s.text;
        if (s.kind == Stmt::Kind::Call && s.b.empty() && !s.a.empty() &&
            s.a.find('.') == string::npos) {
          call_text = s.a + "(";
          for (size_t i = 0; i < s.args.size(); ++i) {
            if (i) call_text += ", ";
            call_text += s.args[i];
          }
          call_text += ")";
        }
        fileio_lifecycle_expression(s.line, call_text, flow);
        // The common single-operation helper case also has a direct summary.
        // This keeps the caller fact robust even when a call statement's
        // normalized expression is unavailable on an alternate parser path.
        string summary_callee;
        vector<string> summary_args;
        if (parse_simple_call(call_text, summary_callee, summary_args)) {
          auto function = functions_.find(summary_callee);
          if (function != functions_.end() &&
              function->second->params.size() == summary_args.size()) {
            for (size_t i = 0; i < summary_args.size(); ++i) {
              const auto& parameter = function->second->params[i];
              string actual = trim(summary_args[i]);
              if (canonical_type_name(parameter.type) != "FileIO" ||
                  !simple_identifier(actual)) continue;
              int summarized = flow.state.count(actual) ? flow.state[actual] : 2;
              bool simple = true;
              bool saw_operation = false;
              for (const auto& operation : function->second->body) {
                if (operation.kind == Stmt::Kind::Pass ||
                    (operation.kind == Stmt::Kind::Return && operation.a.empty()))
                  continue;
                if (operation.kind == Stmt::Kind::If ||
                    operation.kind == Stmt::Kind::While ||
                    operation.kind == Stmt::Kind::For ||
                    operation.kind == Stmt::Kind::Match) {
                  simple = false;
                  break;
                }
                if (operation.kind != Stmt::Kind::Call &&
                    operation.kind != Stmt::Kind::Raw) continue;
                string receiver, method;
                vector<string> arguments;
                bool member = operation.kind == Stmt::Kind::Call &&
                    !operation.b.empty();
                if (member) {
                  receiver = operation.a;
                  method = operation.b;
                  arguments = operation.args;
                } else {
                  member = parse_member_call(trim(operation.text), receiver,
                                             method, arguments);
                }
                if (!member || receiver != parameter.name) continue;
                if (method != "open" && method != "close" && method != "read" &&
                    method != "write" && method != "sync" && method != "chunks")
                  continue;
                saw_operation = true;
                if (method == "open") summarized = 1;
                else if (method == "close") summarized = 0;
              }
              if (simple && saw_operation) flow.state[actual] = summarized;
            }
          }
        }
      } else if (s.kind == Stmt::Kind::Reply || s.kind == Stmt::Kind::Return) {
        fileio_require_closed(s.line, flow);
        flow.continuing = false;
      }
      ++index;
    }
  }

  void check_fileio_lifecycle(const vector<Stmt>& body, const TypeEnv& entry,
                              const Domain* domain, int exit_line) const {
    FileIOFlow flow;
    flow.types = entry;
    if (domain)
      for (const auto& field : domain->state)
        if (canonical_type_name(field.type) == "FileIO") flow.state[field.name] = 2;
    size_t index = 0;
    check_fileio_lifecycle_block(body, index, 0, flow);
    if (flow.continuing) fileio_require_closed(exit_line, flow);
  }

  std::optional<string> fileio_blocking_helper(const string& name,
                                                std::set<string>& visited) const {
    auto function = functions_.find(name);
    if (function == functions_.end() || !visited.insert(name).second)
      return std::nullopt;
    TypeEnv env;
    for (const auto& param : function->second->params) env[param.name] = param.type;
    for (const auto& statement : function->second->body) {
      string expression = statement.kind == Stmt::Kind::Call ||
          statement.kind == Stmt::Kind::Raw ? statement.text :
          statement.kind == Stmt::Kind::Reply ||
          statement.kind == Stmt::Kind::Return ? statement.a : statement.b;
      string receiver, method;
      vector<string> args;
      if (parse_member_call(trim(expression), receiver, method, args) &&
          fileio_operation_effect(method) &&
          (receiver == "FileIO" ||
           canonical_type_name(inferred_expr_type(receiver, env).value_or("")) ==
               "FileIO")) return method;
      string callee;
      if (parse_simple_call(trim(expression), callee, args))
        if (auto nested = fileio_blocking_helper(callee, visited)) return nested;
      if (statement.kind == Stmt::Kind::Let ||
          statement.kind == Stmt::Kind::Var ||
          statement.kind == Stmt::Kind::Assign) {
        auto type = inferred_expr_type(statement.b, env);
        if (type && simple_identifier(statement.a)) env[statement.a] = *type;
      }
    }
    return std::nullopt;
  }

  void warn_fileio_blocking_with_shared_write() {
    for (const auto& domain : p_.domains) {
      for (const auto& handler : domain.handlers) {
        if (!handler.state_effects) continue;
        std::set<string> other_state;
        for (const auto* leaves : {&handler.state_effects->writes,
                                   &handler.state_effects->consumes})
          for (const auto& leaf : *leaves) {
            string root = leaf.substr(0, leaf.find_first_of(".["));
            auto field = std::find_if(domain.state.begin(), domain.state.end(),
                [&](const Field& candidate) { return candidate.name == root; });
            if (field != domain.state.end() &&
                canonical_type_name(field->type) != "FileIO")
              other_state.insert(root);
          }
        if (other_state.empty()) continue;
        TypeEnv env;
        for (const auto& field : domain.state) env[field.name] = field.type;
        for (const auto& param : handler.params) env[param.name] = param.type;
        for (const auto& statement : handler.body) {
          string operation = statement.kind == Stmt::Kind::Call ||
              statement.kind == Stmt::Kind::Raw ? statement.text :
              statement.kind == Stmt::Kind::Reply ||
              statement.kind == Stmt::Kind::Return ? statement.a : statement.b;
          string receiver, method;
          vector<string> args;
          bool blocking = parse_member_call(trim(operation), receiver, method, args) &&
              fileio_operation_effect(method) &&
              (receiver == "FileIO" ||
               canonical_type_name(inferred_expr_type(receiver, env).value_or("")) ==
                   "FileIO");
          if (!blocking) {
            string callee;
            if (parse_simple_call(trim(operation), callee, args)) {
              std::set<string> visited;
              if (auto helper_operation = fileio_blocking_helper(callee, visited)) {
                method = *helper_operation + " via " + callee;
                blocking = true;
              }
            }
          }
          if (blocking) {
            string states;
            for (const auto& state : other_state) {
              if (!states.empty()) states += ", ";
              states += state;
            }
            warnings_.push_back(Warning{statement.line,
                "blocking FileIO " + method + " may hold WRITE/CONSUME protection of " +
                states, "FILEIO_BLOCKING_WITH_SHARED_WRITE", statement.source_file});
          }
          if (statement.kind == Stmt::Kind::Let ||
              statement.kind == Stmt::Kind::Var ||
              statement.kind == Stmt::Kind::Assign) {
            auto type = inferred_expr_type(statement.b, env);
            if (type && simple_identifier(statement.a)) env[statement.a] = *type;
          }
        }
      }
    }
  }
