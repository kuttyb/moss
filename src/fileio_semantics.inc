// Phase 20's built-in capability surface. These helpers are members of Checker;
// ownership and state effects continue through the ordinary checker walks.
  string fileio_owner_initializer_;
  struct FileIOOwnerScope {
    string& target;
    string saved;
    FileIOOwnerScope(string& target, string value)
        : target(target), saved(target) {
      target = strip_redundant_outer_parentheses(trim(value));
    }
    ~FileIOOwnerScope() { target = std::move(saved); }
  };

  static bool fileio_scoped_type(const string& type) {
    string t = canonical_type_name(type);
    return t == "FileIO" || t == "Range" || t == "RangeBatch" ||
           t == "seq[Range]";
  }

  bool fileio_contains_scoped_type(const string& type,
                                   std::set<string>& visited) const {
    string t = canonical_type_name(type);
    if (fileio_scoped_type(t)) return true;
    auto object = objects_.find(t);
    if (object != objects_.end() && visited.insert(t).second)
      for (const auto& field : object->second->fields)
        if (fileio_contains_scoped_type(field.type, visited)) return true;
    auto open = t.find('[');
    if (open == string::npos || !ends_with(t, "]")) return false;
    auto arguments = split_top_level(
        t.substr(open + 1, t.size() - open - 2), ',');
    for (const auto& argument : arguments)
      if (fileio_contains_scoped_type(argument, visited)) return true;
    return false;
  }

  bool fileio_contains_scoped_type(const string& type) const {
    std::set<string> visited;
    return fileio_contains_scoped_type(type, visited);
  }

  // The checked boundary fact for message/executor.invoke arguments: the
  // Executor handle, or any type that structurally reaches a scoped FileIO
  // capability (through collections or object fields).
  bool phase20_capability_type(const string& type) const {
    return phase20_is_nontransferable_capability(type) ||
        fileio_contains_scoped_type(type);
  }

  bool fileio_forbidden_collection_type(const string& type) const {
    string t = canonical_type_name(type);
    bool collection = starts_with(t, "vector[") || starts_with(t, "map[") ||
        starts_with(t, "queue[") || starts_with(t, "table[") ||
        starts_with(t, "seq[");
    return collection && fileio_contains_scoped_type(
        t.substr(t.find('[') + 1, t.size() - t.find('[') - 2));
  }

  bool fileio_request_record_type(const string& type) const {
    auto object = objects_.find(canonical_type_name(type));
    if (object == objects_.end() || object->second->fields.size() != 2)
      return false;
    bool offset = false, size = false;
    for (const auto& field : object->second->fields) {
      if (canonical_type_name(field.type) != "int") return false;
      if (field.name == "offset") offset = true;
      else if (field.name == "size") size = true;
      else return false;
    }
    return offset && size;
  }

  bool fileio_request_vector_type(const string& type) const {
    string canonical = canonical_type_name(type);
    return starts_with(canonical, "vector[") && ends_with(canonical, "]") &&
        fileio_request_record_type(canonical.substr(7, canonical.size() - 8));
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

  // Audited Phase 21 origin sets. These are intentionally operation-precise:
  // programmer misuse (closed owners, invalid bounds/modes) stays on the fatal
  // panic channel and is not represented here.
  static RaiseSet fileio_operation_raise_set(const string& method) {
    auto variant = [](const char* name) {
      return RaisedIdentity::enum_variant("FileError", name);
    };
    if (method == "open")
      return {variant("NotFound"), variant("PermissionDenied"),
              variant("NotRegularFile"), variant("InUse"),
              variant("Full"), variant("IO")};
    if (method == "read" || method == "chunks")
      return {variant("IO")};
    if (method == "write" || method == "sync")
      return {variant("Full"), variant("IO")};
    if (method == "close")
      return {variant("IO")};
    return {};
  }

  static std::optional<string> fileio_method_requirement_result(
      const string& type, const string& method, size_t arity) {
    string concrete = canonical_type_name(type);
    if (concrete == "Range" || concrete == "RangeBatch") {
      if (method == "length" && arity == 0) return string("int");
      if (concrete == "Range" && method == "slice" && arity == 2)
        return string("Range");
      return std::nullopt;
    }
    if (concrete != "FileIO") return std::nullopt;
    if (method == "read")
      return arity == 2 ? std::optional<string>("Range") :
          arity == 1 ? std::optional<string>("RangeBatch") : std::nullopt;
    if (method == "chunks" && arity == 1) return string("seq[Range]");
    if ((method == "open" || method == "write") && arity == 2)
      return string("unit");
    if (method == "sync" && arity <= 1) return string("unit");
    if (method == "close" && arity == 0) return string("unit");
    return std::nullopt;
  }

  std::optional<string> fileio_result_type(const string& expression,
                                            const TypeEnv& env) const {
    // `file.chunks(n) |> map(f) |> reduce(...)` is typed by the functional
    // pipeline IR; parse_member_call would misread it as `file.chunks(...)`.
    if (parse_functional_pipeline(expression)) return std::nullopt;
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
        if (args.size() == 1 &&
            (static_collection_entries(args[0]) ||
             fileio_request_vector_type(
                 inferred_expr_type(args[0], env).value_or(""))))
          return string("RangeBatch");
        return std::nullopt;
      }
      if (method == "chunks") return string("seq[Range]");
      if (fileio_operation_effect(method)) return string("unit");
    }
    if (type == "RangeBatch" && method == "length") return string("int");
    if (type == "Range" && method == "length") return string("int");
    if (type == "Range" && method == "slice" && args.size() == 2)
      return string("Range");
    return std::nullopt;
  }

  bool check_fileio_operation(int line, const string& receiver,
                              const string& method, const vector<string>& args,
                              const TypeEnv& env) {
    bool constructor = receiver == "FileIO" && method == "open";
    if (constructor) {
      string owner_receiver, owner_method;
      vector<string> owner_args;
      if (!parse_member_call(trim(fileio_owner_initializer_), owner_receiver,
                             owner_method, owner_args) ||
          owner_receiver != "FileIO" || owner_method != "open")
        err(line, "FileIO.open must initialize a pinned local owner",
            "FILEIO_PINNED_OWNERSHIP");
    }
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
      if (method == "length" && args.empty()) return true;
      if (type == "Range" && method == "slice" && args.size() == 2) {
        for (const auto& argument : args) {
          auto actual = inferred_expr_type(argument, env);
          if (!actual || canonical_type_name(*actual) != "int")
            fail("Range.slice start and length must be Int");
        }
        return true;
      }
      fail(type + (type == "Range" ? " supports length() and slice(Int, Int)" :
                                      " supports only length()"));
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
      auto payload = inferred_expr_type(args[1], env);
      if (!payload || (canonical_type_name(*payload) != "string" &&
                       canonical_type_name(*payload) != "Range"))
        err(line, "FileIO.write data must be a bounded String or Range byte payload",
            "FILEIO_INVALID_PAYLOAD");
    }
    auto check_offset = [&](const string& expression) {
      auto value = static_exact_integer(expression);
      if (value && *value < 0)
        err(line, "FileIO offset must be nonnegative", "FILEIO_INVALID_BOUND");
    };
    auto check_size = [&](const string& expression, const string& what,
                          bool positive = false) {
      auto exact = static_exact_integer(expression);
      auto value = static_upper_bound(expression);
      if (exact && *exact < 0)
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
                           static_payload_byte_bound(args[1]));
    }
    if (method == "read" && args.size() == 1) {
      auto batch_type = inferred_expr_type(args[0], env);
      auto requests = static_collection_entries(args[0]);
      if (requests || (batch_type && fileio_request_vector_type(*batch_type))) {
        fileio_require_bound(line, "batch count",
            requests ? std::optional<uint64_t>(requests->size()) : std::nullopt);
        uint64_t total = 0;
        bool total_known = requests.has_value();
        auto request_sizes = static_request_size_values(args[0]);
        size_t request_index = 0;
        for (const auto& entry : requests.value_or(vector<string>{})) {
          auto request = static_batch_request(entry);
          if (!request) {
            fail("batch entries must be (offset, size) pairs");
            continue;
          }
          for (const auto& coordinate :
               vector<string>{request->first, request->second}) {
            auto actual = inferred_expr_type(coordinate, env);
            if (!actual || canonical_type_name(*actual) != "int")
              fail("batch offset and size must be Int");
          }
          check_offset(request->first);
          auto exact = request_sizes && request_index < request_sizes->size()
              ? (*request_sizes)[request_index]
              : static_exact_integer(request->second);
          auto size = exact && *exact >= 0
              ? std::optional<uint64_t>(static_cast<uint64_t>(*exact))
              : std::nullopt;
          ++request_index;
          if (exact && *exact < 0)
            err(line, "FileIO batch entry size must be nonnegative",
                "FILEIO_INVALID_BOUND");
          fileio_require_bound(line, "batch entry size", size);
          if (size && UINT64_MAX - total >= *size) total += *size;
          else total_known = false;
        }
        fileio_require_bound(line, "batch total bytes",
            total_known ? std::optional<uint64_t>(total) : std::nullopt);
      } else {
        fail("one-argument FileIO.read requires a bounded collection of (Int, Int) requests");
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
    std::set<string> uncertain_state;
    std::set<string> may_be_read_only;
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
    merged.uncertain_state.insert(right.uncertain_state.begin(),
                                  right.uncertain_state.end());
    merged.may_be_read_only.insert(right.may_be_read_only.begin(),
                                   right.may_be_read_only.end());
    for (const auto& entry : right.state) {
      auto found = merged.state.find(entry.first);
      if (found == merged.state.end()) {
        merged.state[entry.first] = 2;
        merged.uncertain_state.insert(entry.first);
      } else {
        if (found->second != entry.second)
          merged.uncertain_state.insert(entry.first);
        found->second = fileio_join_state(found->second, entry.second);
      }
    }
    for (const auto& entry : left.state)
      if (!right.state.count(entry.first)) {
        merged.state[entry.first] = 2;
        merged.uncertain_state.insert(entry.first);
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
    string value = strip_redundant_outer_parentheses(trim(expression));
    if (value.empty()) return;
    for (const auto& operation : vector<string>{" or ", " and "})
      if (auto binary = split_binary(value, {operation})) {
        fileio_lifecycle_expression(line, binary->first, flow);
        FileIOFlow right = flow;
        fileio_lifecycle_expression(line, binary->second, right);
        flow = fileio_join_flow(flow, right);
        return;
      }
    for (const auto& operators : vector<vector<string>>{
             {"==", "!=", "<=", ">=", "<", ">"},
             {"+", "-"}, {"*", "/", "%"}})
      if (auto binary = split_binary(value, operators)) {
        fileio_lifecycle_expression(line, binary->first, flow);
        fileio_lifecycle_expression(line, binary->second, flow);
        return;
      }
    if (starts_with(value, "[") && ends_with(value, "]")) {
      for (const auto& item : split_top_level(
               value.substr(1, value.size() - 2), ','))
        fileio_lifecycle_expression(line, item, flow);
      return;
    }
    string callee;
    vector<string> call_args;
    string member_receiver, member_method;
    vector<string> member_args;
    bool is_member_call = parse_member_call(value, member_receiver,
                                             member_method, member_args);
    if (!is_member_call && parse_simple_call(value, callee, call_args)) {
      for (const auto& argument : call_args)
        fileio_lifecycle_expression(line, argument, flow);
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
          auto actual_type = inferred_expr_type(actual, flow.types);
          if (!simple_identifier(actual) || !actual_type ||
              canonical_type_name(*actual_type) != "FileIO") continue;
          int actual_state = flow.state.count(actual) ?
              flow.state.at(actual) : 2;
          if (actual_state == 2 &&
              (flow.local_owners.count(actual) ||
               flow.uncertain_state.count(actual)))
            err(line, "FileIO '" + actual +
                "' has no definite lifecycle state before helper call",
                "FILEIO_UNKNOWN_STATE");
          helper.types[parameter.name] = "FileIO";
          helper.state[parameter.name] = flow.state.count(actual)
              ? flow.state.at(actual) : 2;
          if (flow.mode.count(actual)) helper.mode[parameter.name] = flow.mode.at(actual);
          if (flow.uncertain_state.count(actual))
            helper.uncertain_state.insert(parameter.name);
          if (flow.may_be_read_only.count(actual))
            helper.may_be_read_only.insert(parameter.name);
          aliases.emplace_back(parameter.name, actual);
        }
        vector<FileIOFlow> normal_exits;
        size_t helper_index = 0;
        check_fileio_lifecycle_block(function->second->body, helper_index, 0,
                                     helper, &normal_exits);
        if (helper.continuing) {
          if (function->second->result_expression)
            fileio_lifecycle_expression(
                function->second->result_line,
                *function->second->result_expression, helper);
          fileio_require_closed(function->second->line, helper);
          normal_exits.push_back(helper);
        }
        FileIOFlow returned;
        returned.continuing = false;
        for (const auto& normal : normal_exits)
          returned = fileio_join_flow(returned, normal);
        for (const auto& alias : aliases) {
          auto found = returned.state.find(alias.first);
          if (found != returned.state.end()) flow.state[alias.second] = found->second;
          auto mode = returned.mode.find(alias.first);
          if (mode != returned.mode.end()) flow.mode[alias.second] = mode->second;
          if (returned.uncertain_state.count(alias.first))
            flow.uncertain_state.insert(alias.second);
          else flow.uncertain_state.erase(alias.second);
          if (returned.may_be_read_only.count(alias.first))
            flow.may_be_read_only.insert(alias.second);
          else flow.may_be_read_only.erase(alias.second);
        }
      }
      return;
    }
    string receiver, method;
    vector<string> args;
    if (!parse_member_call(value, receiver, method, args)) return;
    fileio_lifecycle_expression(line, receiver, flow);
    for (const auto& argument : args)
      fileio_lifecycle_expression(line, argument, flow);
    if (receiver == "FileIO") return;
    auto type = inferred_expr_type(receiver, flow.types);
    if (!type || canonical_type_name(*type) != "FileIO") return;
    if (!fileio_operation_effect(method)) return;
    auto owner = trim(receiver);
    auto found = flow.state.find(owner);
    int state = found == flow.state.end() ? 2 : found->second;
    if (state == 2 && (flow.local_owners.count(owner) ||
                       flow.uncertain_state.count(owner)))
      err(line, "FileIO '" + owner +
          "' has no definite lifecycle state during " + method,
          "FILEIO_UNKNOWN_STATE");
    if (method == "open") {
      if (state == 1)
        err(line, "FileIO '" + owner + "' is already open",
            "FILEIO_ALREADY_OPEN");
      flow.state[owner] = 1;
      flow.uncertain_state.erase(owner);
      if (args.size() == 2) {
        flow.mode[owner] = trim(args[1]);
        if (flow.mode[owner] == "ro") flow.may_be_read_only.insert(owner);
        else flow.may_be_read_only.erase(owner);
      }
    } else {
      if (state == 0)
        err(line, "FileIO '" + owner + "' is closed during " + method,
            "FILEIO_CLOSED_OPERATION");
      if (method == "close") {
        flow.state[owner] = 0;
        flow.uncertain_state.erase(owner);
      }
      if (method == "write" && flow.may_be_read_only.count(owner))
        err(line, "read-only FileIO '" + owner + "' cannot be written",
            "FILEIO_READ_ONLY");
    }
  }

  void check_fileio_lifecycle_block(const vector<Stmt>& body, size_t& index,
                                    int indent, FileIOFlow& flow,
                                    vector<FileIOFlow>* normal_exits = nullptr) const {
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
        fileio_lifecycle_expression(s.line, s.a, flow);
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
          check_fileio_lifecycle_block(body, index, indent + 2, arm,
                                       normal_exits);
          joined = fileio_join_flow(joined, arm);
        }
        flow = any_case ? joined : incoming;
        continue;
      }
      if (s.kind == Stmt::Kind::Try) {
        FileIOFlow incoming = flow;
        ++index;
        FileIOFlow normal = incoming;
        check_fileio_lifecycle_block(body, index, indent + 1, normal,
                                     normal_exits);
        FileIOFlow joined = normal;
        bool saw_recover = false;
        while (index < body.size() && body[index].indent == indent &&
               body[index].kind == Stmt::Kind::Recover) {
          saw_recover = true;
          ++index;
          // The try suite's lexical frame has ended on this exceptional edge.
          // Owners first bound there are therefore absent in the sibling arm;
          // owners that predate the try retain their incoming state.
          FileIOFlow recovered = incoming;
          check_fileio_lifecycle_block(body, index, indent + 1, recovered,
                                       normal_exits);
          joined = fileio_join_flow(joined, recovered);
        }
        if (!saw_recover)
          err(s.line, "try must be followed by at least one recover arm",
              "TRY_REQUIRES_RECOVER");
        flow = joined;
        continue;
      }
      if (s.kind == Stmt::Kind::If || s.kind == Stmt::Kind::While ||
          s.kind == Stmt::Kind::For) {
        fileio_lifecycle_expression(s.line,
            s.kind == Stmt::Kind::For ? s.b : s.a, flow);
        FileIOFlow incoming = flow;
        ++index;
        FileIOFlow branch = incoming;
        bool range_batch_iteration = s.kind == Stmt::Kind::For &&
            canonical_type_name(inferred_expr_type(s.b, flow.types).value_or("")) ==
                "RangeBatch";
        if (range_batch_iteration)
          branch.range_origin[s.a] = {trim(s.b), branch.batch_generation[trim(s.b)]};
        check_fileio_lifecycle_block(body, index, indent + 1, branch,
                                     normal_exits);
        if (s.kind == Stmt::Kind::If && index < body.size() &&
            body[index].indent == indent && body[index].kind == Stmt::Kind::Else) {
          ++index;
          FileIOFlow alternative = incoming;
          check_fileio_lifecycle_block(body, index, indent + 1, alternative,
                                       normal_exits);
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
        vector<string> open_args;
        if (type && canonical_type_name(*type) == "FileIO" &&
            is_direct_fileio_open_initializer(s.b, &open_args)) {
          flow.types[s.a] = "FileIO";
          flow.local_owners.insert(s.a);
          flow.state[s.a] = 1;
          string receiver, method;
          vector<string> args;
          if (open_args.size() == 2) {
            flow.mode[s.a] = trim(open_args[1]);
            if (flow.mode[s.a] == "ro") flow.may_be_read_only.insert(s.a);
            else flow.may_be_read_only.erase(s.a);
          }
        } else if (type && simple_identifier(s.a)) {
          flow.types[s.a] = *type;
        }
      } else if (s.kind == Stmt::Kind::Echo) {
        for (const auto& argument : s.args)
          fileio_lifecycle_expression(s.line, argument, flow);
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
      } else if (s.kind == Stmt::Kind::Reply || s.kind == Stmt::Kind::Return) {
        fileio_lifecycle_expression(s.line, s.a, flow);
        fileio_require_closed(s.line, flow);
        if (normal_exits) normal_exits->push_back(flow);
        flow.continuing = false;
      } else if (s.kind == Stmt::Kind::Raise) {
        // Raised exits are compiler-managed abnormal scope exits. Root-local
        // owners are closed after dependent Branches quiesce; unlike an
        // ordinary return they do not trigger FILEIO_MUST_CLOSE.
        flow.continuing = false;
      } else if (s.kind == Stmt::Kind::Recover) {
        return;
      }
      ++index;
    }
  }

  void check_fileio_lifecycle(const vector<Stmt>& body, const TypeEnv& entry,
                              const Domain* domain, int exit_line,
                              const std::optional<string>& result_expression =
                                  std::nullopt) const {
    FileIOFlow flow;
    flow.types = entry;
    if (domain)
      for (const auto& field : domain->state)
        if (canonical_type_name(field.type) == "FileIO") flow.state[field.name] = 2;
    size_t index = 0;
    check_fileio_lifecycle_block(body, index, 0, flow);
    if (flow.continuing && result_expression)
      fileio_lifecycle_expression(exit_line, *result_expression, flow);
    if (flow.continuing) fileio_require_closed(exit_line, flow);
  }

  string fileio_message_source(const Stmt& statement) const {
    std::ostringstream source;
    source << "message " << statement.a << "." << statement.b << "(";
    for (size_t index = 0; index < statement.args.size(); ++index) {
      if (index) source << ", ";
      source << statement.args[index];
    }
    source << ")";
    return source.str();
  }

  std::optional<string> fileio_blocking_expression(
      string expression, const TypeEnv& env,
      std::set<string>& visited) const {
    expression = strip_redundant_outer_parentheses(trim(expression));
    if (starts_with(expression, "message ")) {
      string target, handler;
      vector<string> arguments;
      if (parse_member_call(trim(expression.substr(8)), target, handler,
                            arguments))
        if (auto nested = fileio_blocking_message(
                target, handler, env, arguments, visited))
          return *nested + " via message " + target + "." + handler;
    }
    for (const auto& prefix : vector<string>{"echo ", "reply ",
                                              "return ", "message "})
      if (starts_with(expression, prefix))
        expression = trim(expression.substr(prefix.size()));
    if (expression.empty()) return std::nullopt;
    auto values = split_top_level(expression, ',');
    if (values.size() > 1) {
      for (const auto& value : values)
        if (auto nested = fileio_blocking_expression(value, env, visited))
          return nested;
      return std::nullopt;
    }
    string receiver, method;
    vector<string> args;
    if (parse_member_call(expression, receiver, method, args)) {
      if (auto nested = fileio_blocking_expression(receiver, env, visited))
        return nested;
      for (const auto& argument : args)
        if (auto nested = fileio_blocking_expression(argument, env, visited))
          return nested;
      if (fileio_operation_effect(method) &&
          (receiver == "FileIO" ||
           canonical_type_name(inferred_expr_type(receiver, env).value_or("")) ==
               "FileIO")) return method;
      return std::nullopt;
    }
    string callee;
    if (parse_simple_call(expression, callee, args)) {
      for (const auto& argument : args)
        if (auto nested = fileio_blocking_expression(argument, env, visited))
          return nested;
      if (auto nested = fileio_blocking_helper(callee, visited, env, args))
        return *nested + " via " + callee;
      return std::nullopt;
    }
    for (const auto& operators : vector<vector<string>>{
             {" or ", " and "}, {"==", "!=", "<=", ">=", "<", ">"},
             {"+", "-"}, {"*", "/", "%"}})
      if (auto binary = split_binary(expression, operators)) {
        if (auto left = fileio_blocking_expression(binary->first, env, visited))
          return left;
        return fileio_blocking_expression(binary->second, env, visited);
      }
    if (starts_with(expression, "[") && ends_with(expression, "]"))
      for (const auto& item : split_top_level(
               expression.substr(1, expression.size() - 2), ','))
        if (auto nested = fileio_blocking_expression(item, env, visited))
          return nested;
    return std::nullopt;
  }

  std::optional<string> fileio_blocking_helper(
      const string& name, std::set<string>& visited,
      const TypeEnv& caller_env, const vector<string>& call_args) const {
    auto function = functions_.find(name);
    if (function == functions_.end() || !visited.insert(name).second)
      return std::nullopt;
    struct VisitScope {
      std::set<string>& visited;
      const string& name;
      ~VisitScope() { visited.erase(name); }
    } visit_scope{visited, name};
    TypeEnv env;
    for (size_t i = 0; i < function->second->params.size(); ++i) {
      const auto& parameter = function->second->params[i];
      env[parameter.name] = parameter.type.empty() && i < call_args.size()
          ? inferred_expr_type(call_args[i], caller_env).value_or("") :
            parameter.type;
    }
    for (const auto& statement : function->second->body) {
      string expression = statement.kind == Stmt::Kind::Message
          ? fileio_message_source(statement) :
          statement.kind == Stmt::Kind::Call ||
          statement.kind == Stmt::Kind::Raw ||
          statement.kind == Stmt::Kind::Echo ? statement.text :
          statement.kind == Stmt::Kind::Reply ||
          statement.kind == Stmt::Kind::Return ||
          statement.kind == Stmt::Kind::If ||
          statement.kind == Stmt::Kind::While ||
          statement.kind == Stmt::Kind::Match ? statement.a : statement.b;
      auto blocking = statement.kind == Stmt::Kind::Message
          ? fileio_blocking_message(statement.a, statement.b, env,
                                    statement.args, visited)
          : fileio_blocking_expression(expression, env, visited);
      if (blocking)
        return blocking;
      if (statement.kind == Stmt::Kind::Let ||
          statement.kind == Stmt::Kind::Var ||
          statement.kind == Stmt::Kind::Assign) {
        auto type = inferred_expr_type(statement.b, env);
        if (type && simple_identifier(statement.a)) env[statement.a] = *type;
      }
    }
    if (function->second->result_expression)
      return fileio_blocking_expression(*function->second->result_expression,
                                        env, visited);
    return std::nullopt;
  }

  std::optional<string> fileio_blocking_message(
      const string& receiver, const string& handler_name,
      const TypeEnv& caller_env, const vector<string>& call_args,
      std::set<string>& visited) const {
    auto binding = caller_env.find(receiver);
    if (binding == caller_env.end()) return std::nullopt;
    auto domain = domains_.find(canonical_type_name(binding->second));
    if (domain == domains_.end()) return std::nullopt;
    const Handler* handler = find_handler(*domain->second, handler_name);
    if (!handler) return std::nullopt;
    if (handler->state_effects) {
      for (const auto& field : domain->second->state) {
        if (canonical_type_name(field.type) != "FileIO") continue;
        auto has_leaf = [&](const std::set<string>& leaves) {
          return std::any_of(leaves.begin(), leaves.end(),
              [&](const string& leaf) {
                return leaf.substr(0, leaf.find_first_of(".[")) == field.name;
              });
        };
        if (has_leaf(handler->state_effects->reads) ||
            has_leaf(handler->state_effects->writes) ||
            has_leaf(handler->state_effects->consumes))
          return string("FileIO operation");
      }
    }
    string identity = "message:" + domain->first + "." + handler_name;
    if (!visited.insert(identity).second) return std::nullopt;
    struct VisitScope {
      std::set<string>& visited;
      const string& identity;
      ~VisitScope() { visited.erase(identity); }
    } visit_scope{visited, identity};
    TypeEnv env;
    env["self"] = domain->first;
    for (const auto& field : domain->second->state)
      env[field.name] = field.type;
    for (const auto& route : domain->second->routes)
      env[route.name] = route.type;
    for (size_t index = 0; index < handler->params.size(); ++index) {
      const auto& parameter = handler->params[index];
      env[parameter.name] = parameter.type.empty() && index < call_args.size()
          ? inferred_expr_type(call_args[index], caller_env).value_or("")
          : parameter.type;
    }
    for (const auto& statement : handler->body) {
      string expression = statement.kind == Stmt::Kind::Message
          ? fileio_message_source(statement) :
          statement.kind == Stmt::Kind::Call ||
          statement.kind == Stmt::Kind::Raw ||
          statement.kind == Stmt::Kind::Echo ? statement.text :
          statement.kind == Stmt::Kind::Reply ||
          statement.kind == Stmt::Kind::Return ||
          statement.kind == Stmt::Kind::If ||
          statement.kind == Stmt::Kind::While ||
          statement.kind == Stmt::Kind::Match ? statement.a : statement.b;
      auto blocking = statement.kind == Stmt::Kind::Message
          ? fileio_blocking_message(statement.a, statement.b, env,
                                    statement.args, visited)
          : fileio_blocking_expression(expression, env, visited);
      if (blocking)
        return blocking;
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
        for (const auto& route : domain.routes) env[route.name] = route.type;
        for (const auto& param : handler.params) env[param.name] = param.type;
        for (const auto& statement : handler.body) {
          string operation = statement.kind == Stmt::Kind::Message
              ? fileio_message_source(statement) :
              statement.kind == Stmt::Kind::Call ||
              statement.kind == Stmt::Kind::Raw ||
              statement.kind == Stmt::Kind::Echo ? statement.text :
              statement.kind == Stmt::Kind::Reply ||
              statement.kind == Stmt::Kind::Return ||
              statement.kind == Stmt::Kind::If ||
              statement.kind == Stmt::Kind::While ||
              statement.kind == Stmt::Kind::Match ? statement.a : statement.b;
          std::set<string> visited;
          auto blocking = statement.kind == Stmt::Kind::Message
              ? fileio_blocking_message(statement.a, statement.b, env,
                                        statement.args, visited)
              : fileio_blocking_expression(operation, env, visited);
          if (blocking) {
            string states;
            for (const auto& state : other_state) {
              if (!states.empty()) states += ", ";
              states += state;
            }
            warnings_.push_back(Warning{statement.line,
                "blocking FileIO " + *blocking + " may hold WRITE/CONSUME protection of " +
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
