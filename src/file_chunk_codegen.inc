// Phase 20 Agent D -- native Rust codegen for root-local FileIO statements
// and for `file.chunks(size) |> map(f) |> reduce(initial, combine)` chunk
// pipelines. See file_chunk_lowering.inc for the checker-side counterpart
// and docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md secs. 8-13, 20 for the
// normative sequential reference semantics this lowering must match.
//
// There is no real Agent-C branch-publish/branch-join runtime in this tree,
// so eligible chunk pipelines are *not* actually parallelized here -- they
// always execute the exact sequential reference loop (read chunk, map,
// fold, stop on short/zero-length chunk). Eligibility is still computed and
// recorded on the pipeline's existing `decision`/`optimization_notes` IR
// fields (file_chunk_lowering.inc) so the distinction between "ineligible"
// and "eligible but no runtime support yet" remains visible to tooling.

// The FileIO runtime preamble (fileio_runtime.hpp) is sizable boilerplate
// that several existing test assertions grep the *entire* generated file
// for specific unrelated substrings (e.g. a bare "break;" or "Vec::new()").
// Emit it only when the program actually constructs a FileIO, so programs
// that never use Phase 20 FileIO keep byte-for-byte unaffected native
// output.
static bool statements_use_fileio_open(const vector<Stmt>& statements) {
  for (const auto& statement : statements)
    if (statement.text.find("FileIO.open(") != string::npos ||
        statement.b.find("FileIO.open(") != string::npos)
      return true;
  return false;
}

bool program_uses_fileio() const {
  if (statements_use_fileio_open(p_.main ? p_.main->body : vector<Stmt>{})) return true;
  for (const auto& function : p_.functions)
    if (statements_use_fileio_open(function.body)) return true;
  for (const auto& domain : p_.domains)
    for (const auto& handler : domain.handlers)
      if (statements_use_fileio_open(handler.body)) return true;
  for (const auto& object : p_.objects)
    for (const auto& method : object.methods)
      if (statements_use_fileio_open(method.body)) return true;
  for (const auto& test : p_.tests)
    if (statements_use_fileio_open(test.body)) return true;
  for (const auto& benchmark : p_.benchmarks)
    if (statements_use_fileio_open(benchmark.body)) return true;
  return false;
}

bool gen_fileio_statement(std::ostringstream& o, const Stmt& s, int spaces,
                          const Domain* d, std::set<string>& locals,
                          std::unordered_map<string,string>& types) {
  const string pad(spaces, ' ');
  if (s.kind == Stmt::Kind::Assign || s.kind == Stmt::Kind::Let ||
      s.kind == Stmt::Kind::Var) {
    vector<string> open_arguments;
    if (parse_fileio_open(s.b, open_arguments)) {
      o << pad << "let mut " << s.a << " = MossFileIO::__moss_open(&("
        << expr(open_arguments[0], d, locals, &types) << "), "
        << rust_string_literal(trim(open_arguments[1])) << ");\n";
      locals.insert(s.a);
      types[s.a] = "fileio";
      return true;
    }
    string receiver, method;
    vector<string> arguments;
    if (parse_member_call(s.b, receiver, method, arguments) &&
        fileio_typed(types, receiver) && method == "read" &&
        arguments.size() == 2) {
      o << pad << "let " << s.a << " = " << expr(receiver, d, locals, &types)
        << ".__moss_read(" << expr(arguments[0], d, locals, &types) << ", "
        << expr(arguments[1], d, locals, &types) << ");\n";
      locals.insert(s.a);
      types[s.a] = "string";
      return true;
    }
    return false;
  }
  if (s.kind != Stmt::Kind::Call || !fileio_typed(types, s.a)) return false;
  string receiver_expr = expr(s.a, d, locals, &types);
  if (s.b == "write" && s.args.size() == 2) {
    o << pad << receiver_expr << ".__moss_write("
      << expr(s.args[0], d, locals, &types) << ", &("
      << expr(s.args[1], d, locals, &types) << "));\n";
    return true;
  }
  if (s.b == "sync" && s.args.size() <= 1) {
    bool dataonly = !s.args.empty() && trim(s.args[0]) == "dataonly";
    o << pad << receiver_expr << ".__moss_sync(" << (dataonly ? "true" : "false") << ");\n";
    return true;
  }
  if (s.b == "close" && s.args.empty()) {
    o << pad << receiver_expr << ".__moss_close();\n";
    return true;
  }
  throw std::runtime_error("internal error: unchecked FileIO operation '" + s.b + "'");
}

bool gen_executor_or_fileio_statement(std::ostringstream& o, const Stmt& s,
                                      int spaces, const Domain* d,
                                      std::set<string>& locals,
                                      std::unordered_map<string,string>& types) {
  return gen_executor_statement(o, s, spaces, locals, types) ||
      gen_fileio_statement(o, s, spaces, d, locals, types);
}

// Native lowering of a checked `file.chunks(size) |> map(f) |> reduce(initial, combine)`
// pipeline. Always sequential: read the chunk at the current offset, stop on
// a zero-length read without folding, map+fold a non-empty chunk, stop after
// folding a short chunk (matches docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md
// sec. 20.1 exactly).
std::optional<string> gen_fileio_chunk_pipeline(
    const ParsedFunctionalPipeline& pipeline, const Domain* domain,
    const std::set<string>& locals, const std::unordered_map<string,string>* types) const {
  string fileio_receiver, size_expression;
  if (!parse_fileio_chunks_source(pipeline.source, fileio_receiver, size_expression))
    return std::nullopt;
  if (!types || !fileio_typed(*types, fileio_receiver)) return std::nullopt;
  if (pipeline.stages.size() != 2 ||
      pipeline.stages[0].kind != FunctionalNodeKind::Map ||
      pipeline.stages[0].arguments.size() != 1 ||
      pipeline.stages[1].kind != FunctionalNodeKind::Reduce ||
      pipeline.stages[1].arguments.size() != 2)
    throw std::runtime_error(
        "internal error: unchecked FileIO chunk pipeline shape; expected "
        "exactly 'chunks(n) |> map(f) |> reduce(initial, combine)'");
  const string& map_callable = pipeline.stages[0].arguments.front();
  const string& initial_expression = pipeline.stages[1].arguments[0];
  const string& combine_callable = pipeline.stages[1].arguments[1];

  auto accumulator_type = generated_expr_type(initial_expression, types);
  if (!accumulator_type)
    throw std::runtime_error(
        "internal error: unchecked FileIO chunk pipeline initial accumulator type");
  auto mapped_type = generated_callable_result(map_callable, {"string"}, types);
  if (!mapped_type)
    throw std::runtime_error(
        "internal error: unchecked FileIO chunk pipeline map result type");

  size_t id = fileio_chunk_temp_++;
  const string offset = "__moss_chunk_offset_" + std::to_string(id);
  const string size_var = "__moss_chunk_size_" + std::to_string(id);
  const string acc = "__moss_chunk_acc_" + std::to_string(id);
  const string bytes = "__moss_chunk_bytes_" + std::to_string(id);
  const string mapped = "__moss_chunk_mapped_" + std::to_string(id);

  std::ostringstream out;
  out << "{\n";
  out << "        // Moss backend: FileIO CHUNK PIPELINE -- sequential reference\n";
  out << "        // semantics (no Agent-C branch runtime integrated yet)\n";
  out << "        let " << size_var << ": i64 = "
      << expr(size_expression, domain, locals, types) << ";\n";
  out << "        let mut " << offset << ": i64 = 0;\n";
  out << "        let mut " << acc << " = "
      << expr(initial_expression, domain, locals, types) << ";\n";
  out << "        loop {\n";
  out << "            let " << bytes << " = "
      << expr(fileio_receiver, domain, locals, types) << ".__moss_read("
      << offset << ", " << size_var << ");\n";
  out << "            if " << bytes << ".is_empty() { break; }\n";
  out << "            let __moss_chunk_len_" << id << " = " << bytes
      << ".len() as i64;\n";
  out << "            let " << mapped << " = "
      << render_functional_callable(
             map_callable, {FunctionalValue{bytes, "string", false}}, domain,
             locals, types)
      << ";\n";
  out << "            " << acc << " = "
      << render_functional_callable(
             combine_callable,
             {FunctionalValue{acc, *accumulator_type, false},
              FunctionalValue{mapped, *mapped_type, false}},
             domain, locals, types)
      << ";\n";
  out << "            " << offset << " += __moss_chunk_len_" << id << ";\n";
  out << "            if __moss_chunk_len_" << id << " < " << size_var
      << " { break; }\n";
  out << "        }\n";
  out << "        " << acc << "\n";
  out << "    }";
  return out.str();
}
