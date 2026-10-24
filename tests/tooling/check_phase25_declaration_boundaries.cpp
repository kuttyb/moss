// Exercise the actual compiler entry points, including ABI interface emission.
// Renaming only the CLI entry lets this translation unit inject corrupt checked
// declarations without adding an invalid Moss syntax or a production test hook.
#define main moss_cli_entry_for_declaration_test
#include "../../src/moss.cpp"
#undef main

using namespace moss;

static Program declaration_program(int kind, bool erased, bool missing) {
  Program program;
  program.type_context = std::make_shared<TypeContext>();
  const auto type = Type::primitive("int");
  Function function;
  function.name = "sample";
  function.semantic_callable_id =
      "callable-v1:module-v1:sample@1::main:fn:sample";
  if (kind == 0) {
    Param parameter("argument", type);
    if (erased) parameter.type.clear();
    function.params.push_back(std::move(parameter));
    program.functions.push_back(std::move(function));
  } else if (kind == 1) {
    ObjectType object;
    object.name = "Box";
    Method method;
    method.owner = object.name;
    method.name = "Read";
    method.bind_return_type(type);
    if (erased) {
      if (missing) method.return_type.reset();
      else method.return_type = "";
    }
    object.methods.push_back(std::move(method));
    program.objects.push_back(std::move(object));
  } else if (kind == 2) {
    Trait trait;
    trait.name = "Readable";
    TraitMethod method;
    method.name = "Read";
    method.bind_return_type(type);
    if (erased) {
      if (missing) method.return_type.reset();
      else method.return_type = "";
    }
    trait.methods.push_back(std::move(method));
    program.traits.push_back(std::move(trait));
  } else if (kind == 3) {
    Domain domain;
    domain.name = "Store";
    Handler handler;
    handler.name = "Read";
    handler.bind_reply_type(type);
    if (erased) {
      if (missing) handler.reply_type.reset();
      else handler.reply_type = "";
    }
    domain.handlers.push_back(std::move(handler));
    program.domains.push_back(std::move(domain));
  } else {
    function.bind_return_type(type);
    if (erased) {
      if (missing) function.return_type.reset();
      else function.return_type = "";
    }
    program.functions.push_back(std::move(function));
  }
  return program;
}

static void expect_projection_failure(const std::function<void()>& action) {
  try {
    action();
  } catch (const std::logic_error& error) {
    const string message = error.what();
    if (message.find("projection") != string::npos &&
        message.find("contradicts") != string::npos) return;
    throw std::runtime_error("unrelated rejection: " + message);
  }
  throw std::runtime_error("erased bound projection crossed compiler boundary");
}

int main() {
  // Unbound declarations still represent legitimate open source annotations.
  Param{}.assert_consistent();
  Method{}.assert_consistent();
  TraitMethod{}.assert_consistent();
  Handler{}.assert_consistent();
  Function{}.assert_consistent();
  for (int kind = 0; kind < 5; ++kind) {
    auto valid = declaration_program(kind, false, false);
    valid.assert_declaration_type_consistency();
    for (int missing = 0; missing < (kind ? 2 : 1); ++missing) {
      auto program = declaration_program(kind, true, missing != 0);
      // Program traversal covers nested method, trait, handler and parameter
      // records, rather than checking only one hand-selected declaration.
      expect_projection_failure([&] {
        program.assert_declaration_type_consistency();
      });
      expect_projection_failure([&] {
        (void)project_typed_semantics_v1(program);
      });
      expect_projection_failure([&] {
        const ProjectManifest manifest;
        const BackendToolchainIdentity backend;
        (void)write_module_interfaces(manifest, program,
                                     "tmp/declaration-boundary-unused", backend, {});
      });
      expect_projection_failure([&] {
        auto input = program;
        (void)moss::check_program(input);
      });
    }
  }
  std::cout << "PASS erased bound projections rejected for all five declaration kinds"
            << " at Program, checker, typed semantic and ABI boundaries\n";
}
