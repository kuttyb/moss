// Phase 20 Agent D -- Fast Debug (interpreter) support for `Executor` /
// `executor.invoke` and the minimal root-local `FileIO` adapter described in
// executor_invoke_lowering.inc and file_chunk_lowering.inc. Fast Debug is an
// independent execution engine from native codegen (see the class-level
// comment at the top of this file); this performs real blocking POSIX I/O
// so differential native/Fast-Debug testing is meaningful, not a symbolic
// stand-in.
//
// `executor.invoke(domain.Handler(args...))` reuses `message(...)` directly:
// Fast Debug already executes every root/handler on the same call stack, so
// "submit a one-way root inline" and "call a handler synchronously and
// discard its (absent) reply" are the same operation here. This mirrors the
// native lowering's documented inline adapter (no Agent-C runtime).

  static bool interp_is_executor_construction(const std::string& text) {
    std::string value = trim_copy(text);
    while (true) {
      auto member = top_level_member(value);
      if (!member) {
        std::string callee;
        std::vector<std::string> args;
        return parse_call(value, callee, args) && callee == "Executor" && args.empty();
      }
      value = member->first;
    }
  }

  static bool interp_parse_fileio_open(const std::string& text,
                                       std::vector<std::string>& args) {
    auto member = top_level_member(text);
    if (!member || member->first != "FileIO") return false;
    std::string method_name;
    return parse_call(member->second, method_name, args) && method_name == "open" &&
        args.size() == 2;
  }

  Value fileio_open(const std::string& path, const std::string& mode, int line) {
    int flags = 0;
    if (mode == "ro") flags = O_RDONLY;
    else if (mode == "rw") flags = O_RDWR;
    else if (mode == "create") flags = O_RDWR | O_CREAT;
    else throw RuntimeError(line, "internal error: unchecked FileIO.open mode '" + mode + "'");
    int fd = ::open(path.c_str(), flags, 0644);
    if (fd < 0) throw RuntimeError(line, "FileIO.open failed for '" + path + "'");
    struct stat info;
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
      ::close(fd);
      throw RuntimeError(line, "FileIO.open requires a regular file: '" + path + "'");
    }
    auto key = std::make_pair(static_cast<unsigned long>(info.st_dev),
                              static_cast<unsigned long>(info.st_ino));
    if (!fileio_registry_.insert(key).second) {
      ::close(fd);
      throw RuntimeError(line,
          "a live FileIO already refers to this file's (device, inode)");
    }
    Value result = Value::struct_value("__moss_fileio__");
    result.object->fields["__fd"] = Value::int_value(fd);
    result.object->fields["__dev"] = Value::int_value(static_cast<std::int64_t>(info.st_dev));
    result.object->fields["__ino"] = Value::int_value(static_cast<std::int64_t>(info.st_ino));
    return result;
  }

  static int fileio_fd(const Value& handle, int line) {
    if (handle.kind != Value::Kind::Struct || !handle.object ||
        !handle.object->fields.count("__fd"))
      throw RuntimeError(line, "internal error: unchecked FileIO receiver");
    return static_cast<int>(handle.object->fields.at("__fd").integer);
  }

  Value fileio_read(const Value& handle, std::int64_t offset, std::int64_t size, int line) {
    int fd = fileio_fd(handle, line);
    std::string buffer(size < 0 ? 0 : static_cast<size_t>(size), '\0');
    size_t total = 0;
    while (total < buffer.size()) {
      ssize_t got = ::pread(fd, buffer.data() + total, buffer.size() - total,
                           static_cast<off_t>(offset) + static_cast<off_t>(total));
      if (got < 0) throw RuntimeError(line, "FileIO.read failed");
      if (got == 0) break;
      total += static_cast<size_t>(got);
    }
    buffer.resize(total);
    return Value::string_value(std::move(buffer));
  }

  void fileio_write(const Value& handle, std::int64_t offset, const std::string& data, int line) {
    int fd = fileio_fd(handle, line);
    size_t total = 0;
    while (total < data.size()) {
      ssize_t wrote = ::pwrite(fd, data.data() + total, data.size() - total,
                              static_cast<off_t>(offset) + static_cast<off_t>(total));
      if (wrote < 0) throw RuntimeError(line, "FileIO.write failed");
      total += static_cast<size_t>(wrote);
    }
  }

  void fileio_sync(const Value& handle, bool dataonly, int line) {
    int fd = fileio_fd(handle, line);
    int result = dataonly ? ::fdatasync(fd) : ::fsync(fd);
    if (result != 0) throw RuntimeError(line, "FileIO.sync failed");
  }

  void fileio_close(const Value& handle, int line) {
    int fd = fileio_fd(handle, line);
    auto key = std::make_pair(
        static_cast<unsigned long>(handle.object->fields.at("__dev").integer),
        static_cast<unsigned long>(handle.object->fields.at("__ino").integer));
    fileio_registry_.erase(key);
    if (::close(fd) != 0) throw RuntimeError(line, "FileIO.close failed");
  }

  static bool interp_parse_fileio_chunks_source(const std::string& source,
                                                std::string& receiver,
                                                std::string& size_expression) {
    auto member = top_level_member(source);
    if (!member) return false;
    std::string method_name;
    std::vector<std::string> args;
    if (!parse_call(member->second, method_name, args)) return false;
    if (method_name != "chunks" || args.size() != 1) return false;
    receiver = member->first;
    size_expression = args.front();
    return true;
  }

  // Sequential reference semantics for `file.chunks(size) |> map(f) |>
  // reduce(initial, combine)` (docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md
  // sec. 20.1): read the chunk at the current offset, stop on a zero-length
  // read without folding, map+fold a non-empty chunk, stop after folding a
  // short chunk. Fast Debug has no branch runtime either, so this is the
  // same sequential loop as native codegen (file_chunk_codegen.inc).
  Value eval_fileio_chunk_pipeline(const std::string& fileio_receiver,
                                   const std::string& size_expression,
                                   const std::vector<std::string>& stages,
                                   Frame& frame, int line, std::ostream& output) {
    if (stages.size() != 3)
      throw RuntimeError(line, "internal error: unchecked FileIO chunk pipeline shape");
    std::string map_callee;
    std::vector<std::string> map_args;
    if (!parse_call(stages[1], map_callee, map_args) || map_callee != "map" ||
        map_args.size() != 1)
      throw RuntimeError(line, "internal error: unchecked FileIO chunk pipeline map stage");
    std::string reduce_callee;
    std::vector<std::string> reduce_args;
    if (!parse_call(stages[2], reduce_callee, reduce_args) || reduce_callee != "reduce" ||
        reduce_args.size() != 2)
      throw RuntimeError(line, "internal error: unchecked FileIO chunk pipeline reduce stage");

    Value receiver = eval(fileio_receiver, frame, line, output);
    std::int64_t size = eval(size_expression, frame, line, output).integer;
    std::int64_t offset = 0;
    Value accumulator = eval(reduce_args[0], frame, line, output);
    while (true) {
      Value bytes = fileio_read(receiver, offset, size, line);
      if (bytes.string.empty()) break;
      std::int64_t chunk_len = static_cast<std::int64_t>(bytes.string.size());
      Value mapped = invoke_pipeline_callable(map_args.front(), {bytes}, frame, line, output);
      accumulator = invoke_pipeline_callable(reduce_args[1], {accumulator, mapped}, frame, line, output);
      offset += chunk_len;
      if (chunk_len < size) break;
    }
    return accumulator;
  }

  // Recognizes and fully executes one Executor-lifecycle or FileIO statement.
  // Returns false for every ordinary statement, which then falls through to
  // the generic switch in execute() exactly as before.
  bool interp_executor_or_fileio_statement(const Stmt& statement, Frame& frame,
                                           std::ostream& output) {
    if (statement.kind == Stmt::Kind::Assign || statement.kind == Stmt::Kind::Let ||
        statement.kind == Stmt::Kind::Var) {
      if (interp_is_executor_construction(statement.b)) {
        frame.locals[statement.a] = Value::unit();
        frame.local_types[statement.a] = "executor";
        return true;
      }
      std::vector<std::string> open_arguments;
      if (interp_parse_fileio_open(statement.b, open_arguments)) {
        Value path = eval(open_arguments[0], frame, statement.line, output);
        frame.locals[statement.a] =
            fileio_open(path.string, trim_copy(open_arguments[1]), statement.line);
        frame.local_types[statement.a] = "fileio";
        return true;
      }
      auto member = top_level_member(statement.b);
      if (member) {
        auto receiver_type = frame.local_types.find(trim_copy(member->first));
        if (receiver_type != frame.local_types.end() && receiver_type->second == "fileio") {
          std::string method_name;
          std::vector<std::string> method_args;
          if (parse_call(member->second, method_name, method_args) &&
              method_name == "read" && method_args.size() == 2) {
            Value receiver = eval(member->first, frame, statement.line, output);
            Value offset = eval(method_args[0], frame, statement.line, output);
            Value size = eval(method_args[1], frame, statement.line, output);
            frame.locals[statement.a] =
                fileio_read(receiver, offset.integer, size.integer, statement.line);
            frame.local_types[statement.a] = "string";
            return true;
          }
        }
      }
      return false;
    }
    if (statement.kind != Stmt::Kind::Call) return false;
    auto receiver_type = frame.local_types.find(statement.a);
    if (receiver_type == frame.local_types.end()) return false;
    if (receiver_type->second == "executor") {
      if (statement.b == "join") return true;
      if (statement.b == "invoke") {
        message(statement.args.front(), frame, statement.line, output);
        return true;
      }
    }
    if (receiver_type->second == "fileio") {
      Value receiver = eval(statement.a, frame, statement.line, output);
      if (statement.b == "write" && statement.args.size() == 2) {
        Value offset = eval(statement.args[0], frame, statement.line, output);
        Value data = eval(statement.args[1], frame, statement.line, output);
        fileio_write(receiver, offset.integer, data.string, statement.line);
        return true;
      }
      if (statement.b == "sync" && statement.args.size() <= 1) {
        bool dataonly = !statement.args.empty() && trim_copy(statement.args[0]) == "dataonly";
        fileio_sync(receiver, dataonly, statement.line);
        return true;
      }
      if (statement.b == "close" && statement.args.empty()) {
        fileio_close(receiver, statement.line);
        return true;
      }
    }
    return false;
  }
