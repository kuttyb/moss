#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include "ast.hpp"
#include "checked_body_ir.hpp"
#include "debug_map.hpp"
#include "diagnostics.hpp"
#include "functional_optimizer.hpp"
#include "functional_optimizer_support.hpp"
#include "semantic_checker_support.hpp"
#include "native_generator.hpp"
#include "native_generator_support.hpp"
#include "type_placeholders.hpp"
#include "native_cache_identity.hpp"
#include "parser_support.hpp"
#include "synchronization.hpp"
#include "synchronization_lowering.hpp"
#include "source_free_abi_v9.hpp"
#include "source_free_abi_v10.hpp"
#include "abi_v10_records.hpp"
#include "handler_runtime.hpp"
#include "fileio_runtime.hpp"
#include "float_format_runtime.hpp"
#include "display_runtime.hpp"
#include "executor_runtime.hpp"
#include "char_string_ordering_runtime.hpp"
#include "numeric_runtime.hpp"
#include "compiler_stage_timer.hpp"

using std::string;
using std::vector;

namespace moss {



// Generator-private post-rewrite lowering metadata. The checker-owned parsed
// functional stages remain in functional_source_pipeline.hpp.
struct LoweredFunctionalStage {
  FunctionalNodeKind kind = FunctionalNodeKind::Source;
  std::vector<ParsedFunctionalStage> operations;
};
struct LoweredFunctionalPipeline {
  std::string source;
  std::vector<LoweredFunctionalStage> stages;
};

struct FunctionalExpressionHandlesScope {
  const vector<est::Handle>*& slot;
  const vector<est::Handle>* prior;
  FunctionalExpressionHandlesScope(
      const vector<est::Handle>*& target,
      const vector<est::Handle>* current)
      : slot(target), prior(target) { slot = current; }
  ~FunctionalExpressionHandlesScope() { slot = prior; }
};

class Generator {
 public:
  Generator(const Program& p, const OptimizationPlan&,
            bool debug_build = false,
            ProgramGenerationMode mode = ProgramGenerationMode::Application,
            vector<string> rust_dependencies = {},
            const Program* resolution_program = nullptr,
            bool project_module_artifact = false,
            bool emit_static_specializations = true,
            bool public_specializations = false)
      : p_(p),
        type_scope_(p.type_context.get()),
        debug_build_(debug_build), mode_(mode),
        rust_dependencies_(std::move(rust_dependencies)),
        emit_static_specializations_(emit_static_specializations),
        public_specializations_(public_specializations),
        project_module_artifact_(project_module_artifact) {
    for (const auto& d : p.domains) domains_[d.name] = &d;
    const Program& semantic_program = resolution_program ? *resolution_program : p;
    semantic_program_ = &semantic_program;
    std::set<string> used_domain_names;
    for (const auto& domain : semantic_program.domains) used_domain_names.insert(domain.name);
    for (const auto& specialization : semantic_program.domain_specializations) {
      if (!specialization.materialized_layout) continue;
      auto source = std::find_if(
          semantic_program.domains.begin(), semantic_program.domains.end(),
          [&](const Domain& domain) {
            return domain.name == specialization.source_domain;
          });
      if (source == semantic_program.domains.end()) continue;
      Domain specialized = *source;
      string backend_name = specialization.source_domain + "__" +
          specialization.instance;
      if (used_domain_names.count(backend_name)) {
        backend_name += "__specialized_" +
            stable_hash(specialization.source_domain + "\n" +
                        specialization.instance).substr(0, 12);
        size_t suffix = 0;
        string candidate = backend_name;
        while (used_domain_names.count(candidate))
          candidate = backend_name + "_" + std::to_string(++suffix);
        backend_name = std::move(candidate);
      }
      specialized.name = backend_name;
      used_domain_names.insert(specialized.name);
      for (auto& field : specialized.state) {
        auto type = specialization.state_types.find(field.name);
        if (type != specialization.state_types.end()) field.type = type->second;
      }
      for (auto& handler : specialized.handlers) {
        auto parameters = specialization.handler_parameter_types.find(handler.name);
        if (parameters != specialization.handler_parameter_types.end()) {
          for (size_t index = 0;
               index < handler.params.size() && index < parameters->second.size();
               ++index)
            if (!parameters->second[index].empty())
              handler.params[index].set_type(parameters->second[index]);
        }
        auto reply = specialization.handler_reply_types.find(handler.name);
        if (reply != specialization.handler_reply_types.end())
          handler.set_reply_type(reply->second);
      }
      specialized_domains_.push_back(std::move(specialized));
      specialization_sources_[specialized_domains_.back().name] = specialization.source_domain;
      specialization_names_[specialization.source_domain + "\n" +
                            specialization.instance] =
          specialized_domains_.back().name;
    }
    for (const auto& d : specialized_domains_) {
      domains_[d.name] = &d;
    }
    for (const auto& o : p.objects) objects_[o.name] = &o;
    for (const auto& e : p.enums) enums_[e.name] = &e;
    for (const auto& f : p.functions) {
      functions_[f.name] = &f;
      if (!f.semantic_callable_id.empty())
        callable_id_to_function_[f.semantic_callable_id] = &f;
    }
    if (resolution_program) {
      for (const auto& d : resolution_program->domains)
        domains_.emplace(d.name, &d);
      for (const auto& o : resolution_program->objects)
        objects_.emplace(o.name, &o);
      for (const auto& e : resolution_program->enums)
        enums_.emplace(e.name, &e);
      for (const auto& f : resolution_program->functions) {
        functions_.emplace(f.name, &f);
        if (!f.semantic_callable_id.empty())
          callable_id_to_function_.emplace(f.semantic_callable_id, &f);
      }
    }
  }

  string generate() {
    CompilerStageTimer timer("rust_generation");
    if (!p_.concrete_domain_graph.instances.empty() && !p_.concrete_domain_graph.closed)
      throw std::runtime_error("internal error: lowering requires a closed ConcreteDomainGraph");
    validate_synchronization_lowering(*semantic_program_);
    std::ostringstream o;
    o << "// Generated by Moss v0.1. Do not edit by hand.\n";
    o << "// Synchronous domain calls enter compiler-planned handler-level 2PL.\n";
    for (const auto& domain : p_.domains) o << "// Moss backend plan: " << domain.name << " = Handler2PL.\n";
    if (staged_abi_v10())
      for (const auto& operation : p_.key_operation_specialization_ids) {
        const auto separator = operation.first.rfind('|');
        synchronization_require(separator != string::npos &&
            p_.key_proof_ids_by_type.count(operation.first.substr(separator + 1)),
            "key operation specialization is missing its proof");
        o << "// Moss key operation specialization: " << operation.first
          << " proof="
          << p_.key_proof_ids_by_type.at(operation.first.substr(separator + 1))
          << " id=" << operation.second << "\n";
      }
    o << "#![allow(non_snake_case)]\n#![allow(non_camel_case_types)]\n#![allow(dead_code)]\n";
    o << "#![allow(unused_imports)]\n#![allow(unused_mut)]\n#![allow(unused_variables)]\n#![allow(unused_assignments)]\n\n";
    for (const auto& dependency : rust_dependencies_) {
      string crate = dependency == "__moss_specializations__"
          ? "moss_specializations" : tooling_name("moss_" + dependency);
      o << "extern crate " << crate << ";\n";
      // Moss's namespace pass has already qualified imported names.  Bringing
      // the dependency's public Rust surface into this crate keeps those
      // generated expressions direct calls to the materialized dependency
      // symbol; no dependency implementation is copied here.
      o << "use " << crate << "::*;\n";
    }
    o << "fn __moss_require_send<T: Send>() {}\n\n";
    o << "#[inline(always)]\n"
         "fn __moss_shl(value: i64, shift: i64) -> i64 {\n"
         "    if shift < 0 || shift >= 64 { 0 } else { value.wrapping_shl(shift as u32) }\n"
         "}\n\n"
         "#[inline(always)]\n"
         "fn __moss_shr(value: i64, shift: i64) -> i64 {\n"
         "    if shift < 0 || shift >= 64 { if value < 0 { -1 } else { 0 } } else { value.wrapping_shr(shift as u32) }\n"
         "}\n\n";

    o << handler_runtime_rust();
    o << float_format_runtime_rust();
    if (staged_abi_v10() && indexed_strings_staged()) {
      // A provider and its source-free consumers must name the same Rust
      // MossString type. The opt-in v10 path imports Team S's exact indexed
      // implementation from the compiler-owned shared runtime crate.
      o << char_ordering_runtime_rust();
      o << "pub use moss_runtime::string::{MossStr, MossString, "
           "__moss_string_length, __moss_string_char_at, "
           "__moss_string_slice, __moss_string_find};\n";
    } else {
      o << char_string_ordering_runtime_rust(indexed_strings_staged());
    }
    o << display_runtime_rust();
    if (program_uses_numeric_builtins()) o << numeric_runtime_rust();
    if (program_uses_fileio(p_)) {
      o << fileio_runtime_rust();
    }
    if (!project_module_artifact_) {
      o << fileio_root_runtime_rust();
    } else if (p_.main) {
      // A module with main may also be exported as an rlib. Only its executable
      // build owns process-wide FileIO symbols; linked library builds borrow
      // the selected executable's root runtime.
      o << "#[cfg(moss_process_root)]\nmod __moss_fileio_root_runtime {\nuse super::*;\n";
      o << fileio_root_runtime_rust();
      o << "}\n#[cfg(moss_process_root)]\npub use __moss_fileio_root_runtime::*;\n";
    }
    // Executor runtime ownership follows C's executable/provider role. The
    // Agent B FileIO root runtime remains emitted only in the process root.
    const ExecutorRuntimeRole executor_role =
        !project_module_artifact_
            ? (p_.explicit_module && !p_.main.has_value()
                ? ExecutorRuntimeRole::SelectedByExecutableBuild
                : ExecutorRuntimeRole::ProcessRoot)
            : semantic_program_->main.has_value()
                ? (p_.main.has_value()
                    ? ExecutorRuntimeRole::SelectedByExecutableBuild
                    : ExecutorRuntimeRole::Provider)
                : ExecutorRuntimeRole::SelectedByExecutableBuild;
    o << executor_runtime_rust(executor_role);
    for (const auto& e : p_.enums) gen_enum(o, e);
    gen_standard_error_external_bridges(o);
    gen_phase21_raised_type(o);
    gen_phase21_external_raised_bridges(o);
    for (const auto& t : p_.objects) gen_object(o, t);
    std::map<string, const ObjectType*> view_objects(objects_.begin(), objects_.end());
    for (const auto& entry : view_objects) {
      if (entry.second->fields.empty()) continue;
      bool owns = std::any_of(p_.objects.begin(), p_.objects.end(), [&](const auto& object) { return object.name == entry.first; });
      if (!owns && p_.explicit_module) {
        auto sep = entry.first.find("__");
        if (sep != string::npos) {
          string mod_name = entry.first.substr(0, sep);
          if (std::find(rust_dependencies_.begin(), rust_dependencies_.end(), mod_name) == rust_dependencies_.end()) {
            continue;
          }
        } else {
          continue;
        }
      }
      bool is_generic = std::any_of(entry.second->fields.begin(), entry.second->fields.end(),
          [](const Field& f) { return f.inferred || f.type.empty(); });
      if (!is_generic) gen_object_access(o, *entry.second, owns);
    }
    for (const auto& f : p_.functions) gen_function(o, f);
    auto body_owner = [&](const Domain& d) {
      return !p_.explicit_module || starts_with(d.name, p_.module_name + "__");
    };
    for (const auto& d : p_.domains) if (body_owner(d)) gen_domain_body_abi(o, d);
    for (const auto& d : specialized_domains_)
      if (owns_specialization(d) && body_owner(d)) gen_domain_body_abi(o, d);
    if (!p_.explicit_module || p_.main) {
      // The final application owns every physical layout, including source-free
      // provider domains. Provider bodies expose only semantic borrowed access.
      for (const auto& d : p_.domains) gen_ref_decl(o, d);
      for (const auto& d : specialized_domains_) if (owns_specialization(d)) gen_ref_decl(o, d);
      for (const auto& d : p_.domains) gen_nominal_handle(o, d);
      for (const auto& d : p_.domains) {
        gen_domain(o, d);
        gen_route_contract(o, d, d.name + "Ref");
        if (has_domain_specializations(d.name)) gen_route_contract(o, d, d.name + "Handle");
        if (d.exported) gen_exported_domain_bridge(o, d);
      }
      for (const auto& d : specialized_domains_) if (owns_specialization(d)) {
        gen_domain(o, d);
        // Exact specialized routes use their nominal source contract where
        // parameter/reply types are compatible (existing handle adapter).
        if (d.exported) gen_exported_domain_bridge(o, d);
      }
    }
    if (mode_ == ProgramGenerationMode::Tests)
      gen_test_harness(o);
    else if (mode_ == ProgramGenerationMode::Benchmarks)
      gen_benchmark_harness(o);
    else if (p_.main)
      gen_main(o, *p_.main);
    else
      o << "fn main() {}\n";
    return o.str();
  }

 private:
  const Program& p_;
  // Keeps this program's interned declarations bound while lowering.
  moss::TypeContext::Binding type_scope_;
  bool program_uses_numeric_builtins() const {
    const auto contains = [](const string& value) {
      for (const char* name : {"parse_int", "parse_float", "to_int"}) {
        if (numeric_builtin_base(value) == name) return true;
        for (size_t at = value.find(name); at != string::npos;
             at = value.find(name, at + 1)) {
          size_t end = at + std::char_traits<char>::length(name);
          while (end < value.size() &&
                 std::isspace(static_cast<unsigned char>(value[end]))) ++end;
          if (end < value.size() && value[end] == '(') return true;
        }
      }
      return false;
    };
    const auto body_uses = [&](const vector<Stmt>& body) {
      for (const auto& statement : body) {
        if (contains(statement.a) || contains(statement.b) ||
            contains(statement.text)) return true;
        for (const auto& argument : statement.args)
          if (contains(argument)) return true;
      }
      return false;
    };
    for (const auto& function : p_.functions)
      if (body_uses(function.body) ||
          (function.result_expression && contains(*function.result_expression)))
        return true;
    for (const auto& object : p_.objects)
      for (const auto& method : object.methods)
        if (body_uses(method.body) ||
            (method.result_expression && contains(*method.result_expression)))
          return true;
    for (const auto& domain : p_.domains)
      for (const auto& handler : domain.handlers)
        if (body_uses(handler.body)) return true;
    for (const auto& domain : specialized_domains_)
      for (const auto& handler : domain.handlers)
        if (body_uses(handler.body)) return true;
    if (p_.main && body_uses(p_.main->body)) return true;
    for (const auto& test : p_.tests)
      if (body_uses(test.body)) return true;
    for (const auto& benchmark : p_.benchmarks)
      if (body_uses(benchmark.body)) return true;
    return false;
  }
  const Program* semantic_program_ = nullptr;
  std::set<string> write_through_parameters_;
  std::set<string> borrowed_function_parameters_;
  string construction_binding_;
  bool view_domain_ = false;
  bool view_domain_access_ = false;
  bool view_method_ = false;
  std::set<string> view_parameters_;
  // Non-Copy incoming message parameters use a borrowed internal ABI.  This
  // is physical lowering metadata only: the checked Moss fact remains an
  // immutable value snapshot.  Keeping the set while rendering a handler
  // prevents a forwarded String/Vector/etc. from becoming &&T.
  std::set<string> borrowed_message_parameters_;
  mutable bool mutable_projection_ = false;
  bool debug_build_ = false;
  ProgramGenerationMode mode_ = ProgramGenerationMode::Application;
  vector<string> rust_dependencies_;
  bool emit_static_specializations_ = true;
  bool public_specializations_ = false;
  // Only Margo's per-module projection is reused as both an rlib and the
  // selected executable. Direct `moss -o` Rust is a standalone process root.
  bool project_module_artifact_ = false;
  bool benchmark_body_ = false;
  bool exported_domain_bridge_body_ = false;
  std::unordered_map<string, const Domain*> domains_;
  vector<Domain> specialized_domains_;
  std::unordered_map<string,string> specialization_sources_;
  std::unordered_map<string,string> specialization_names_;
  std::unordered_map<string, const ObjectType*> objects_;
  std::unordered_map<string, const EnumType*> enums_;
  std::unordered_map<string, const Function*> functions_;
  std::unordered_map<string, const Function*> callable_id_to_function_;

  const Function* find_function_by_callable_id(const string& id) const {
    if (id.empty()) return nullptr;
    auto it = callable_id_to_function_.find(id);
    if (it != callable_id_to_function_.end()) return it->second;
    for (const auto& [name, fn] : functions_) {
      if (fn && fn->semantic_callable_id == id) return fn;
    }
    return nullptr;
  }

  static bool is_compile_time_callable(const string& type) {
    return callable_identity(type);
  }
  struct DomainInstanceBinding {
    // This is the semantic specialization key carried by a source binding;
    // it is intentionally independent of the generated Rust type name.
    string source_domain;
    string instance;
  };
  std::unordered_map<string, DomainInstanceBinding> domain_instance_bindings_;
  std::set<string> ambiguous_domain_instance_bindings_;
  size_t assertion_temp_ = 0;
  mutable size_t call_argument_temp_ = 0;
  mutable std::optional<uint32_t> codegen_nested_argument_occurrence_;
  mutable size_t executor_invoke_temp_ = 0;
  mutable bool root_ingress_statement_ = false;
  mutable size_t fileio_chunk_temp_ = 0;
  bool phase21_result_context_ = false;
  std::optional<bool> phase21_return_result_context_;
  string phase21_selected_error_;
  string phase21_selected_carrier_;
  string phase21_try_label_;
  size_t phase21_try_temp_ = 0;
  const Handler* phase21_capture_handler_ = nullptr;
  const std::set<string>* phase21_capture_locals_ = nullptr;
  const RaiseSet* phase21_capture_alternatives_ = nullptr;
  std::set<string> phase21_option_slots_;
  const Stmt* phase21_codegen_statement_ = nullptr;
  const vector<est::Handle>* active_functional_expression_handles_ = nullptr;
  string codegen_body_context_;
  string codegen_active_specialization_key_;
  const FunctionSpecialization* codegen_result_specialization_ = nullptr;
  string codegen_result_expression_;

  static bool handler_has_failure_captures(const Handler& handler) {
    if (handler.provider_abi)
      for (const auto& arm : handler.provider_abi->failure_arms)
        if (!arm.captures.empty()) return true;
    for (const auto& arm : handler.failure_arms)
      if (!arm.captures.empty()) return true;
    return false;
  }

  string handler_capture_carrier_type(const Handler& handler) const {
    return handler.provider_abi
        ? handler.provider_abi->raised_carrier_rust_type
        : handler.phase21_capture_carrier_type;
  }

  string phase21_capture_carrier(const string& error,
                                 const RaiseSet* selected = nullptr) const {
    if (!phase21_capture_handler_ || !phase21_capture_locals_ ||
        !(selected ? selected : phase21_capture_alternatives_))
      throw std::runtime_error(
          "internal error: missing on_fail capture lowering context");
    const Handler& handler = *phase21_capture_handler_;
    const string carrier = handler_capture_carrier_type(handler);
    const string frame = carrier.substr(0, carrier.find("__MossRaisedCarrier_")) +
        "__MossFailureFrame_" +
        carrier.substr(carrier.find("__MossRaisedCarrier_") + 20);
    std::map<string,string> capture_types;
    for (const auto& arm : handler.failure_arms)
      for (const auto& capture : arm.captures)
        capture_types.emplace(capture.name, capture.type);
    std::ostringstream out;
    out << "#[allow(unreachable_patterns)] match " << error << " { ";
    const RaiseSet& possible = *(selected ? selected :
                                phase21_capture_alternatives_);
    for (const auto& identity : possible) {
      const string variant = phase21_rust_variant(identity);
      out << "__MossRaised::" << variant << "(__moss_payload) => "
          << carrier << "(" << frame << " { error: __MossRaised::"
          << variant << "(__moss_payload)";
      for (const auto& capture : capture_types) {
        bool selected = false;
        for (const auto& arm : handler.failure_arms)
          if (arm.exceptional_alternatives.count(identity))
            selected |= std::any_of(arm.captures.begin(), arm.captures.end(),
                [&](const FailureArm::Capture& item) {
                  return item.name == capture.first;
                });
        if (selected && !phase21_capture_locals_->count(capture.first))
          throw std::runtime_error(
              "internal error: on_fail capture is unavailable at native raise site");
        out << ", " << rust_identifier(capture.first) << ": "
            << (selected ? "Some(" +
                (phase21_option_slots_.count(capture.first)
                    ? "__moss_recovery_slot_" + capture.first +
                        ".take().unwrap_or_else(|| std::process::abort())"
                    : rust_identifier(capture.first)) + ")"
                         : "None");
      }
      out << " }), ";
    }
    out << "_ => std::process::abort() }";
    return out.str();
  }

  string phase21_propagate(const string& fallible) const {
    if (phase21_try_label_.empty()) {
      if (phase21_capture_handler_)
        return "match " + fallible + " { Ok(__moss_value) => "
            "__moss_value, Err(__moss_error) => return Err(" +
            phase21_capture_carrier("__moss_error.into()") + ") }";
      return "(" + fallible + ")?";
    }
    return "match " + fallible + " { Ok(__moss_value) => __moss_value, "
        "Err(__moss_error) => break '" + phase21_try_label_ +
        " Err(" + (phase21_capture_handler_
            ? phase21_capture_carrier("__moss_error.into()")
            : "__moss_error.into()") + ") }";
  }

  string phase21_error_exit(const string& error) const {
    if (phase21_try_label_.empty())
      return "return Err(" +
          (phase21_capture_handler_ ? phase21_capture_carrier(error) : error) +
          ")";
    return "break '" + phase21_try_label_ + " Err(" +
        (phase21_capture_handler_ ? phase21_capture_carrier(error) : error) +
        ")";
  }

  static string phase21_rust_variant(const RaisedIdentity& identity) {
    return "Raised_" + stable_hash(identity.canonical()).substr(0, 16);
  }

  string phase21_rust_error_type(const RaisedIdentity& identity) const {
    string type = canonical_type_name(identity.type);
    if (identity.kind == RaisedIdentityKind::EnumVariant &&
        !enums_.count(type)) {
      for (size_t separator = type.find("::"); separator != string::npos;
           separator = type.find("::"))
        type.replace(separator, 2, "__");
    }
    return rust_type(type);
  }

  RaiseSet phase21_program_raise_set() const {
    RaiseSet result;
    const auto merge = [&](const ObservableEffects& effects) {
      result.insert(effects.raise_set.begin(), effects.raise_set.end());
    };
    const auto statements = [&](const vector<Stmt>& body) {
      for (const auto& statement : body)
        result.insert(statement.exceptional_alternatives.begin(),
                      statement.exceptional_alternatives.end());
    };
    for (const auto& function : p_.functions) {
      merge(function.observable_effects);
      for (const auto& specialization : function.specializations)
        merge(specialization.observable_effects);
      statements(function.body);
    }
    for (const auto& object : p_.objects)
      for (const auto& method : object.methods) {
        merge(method.observable_effects);
        statements(method.body);
      }
    for (const auto& domain : p_.domains)
      for (const auto& handler : domain.handlers) {
        merge(handler.observable_effects);
        statements(handler.body);
        for (const auto& arm : handler.failure_arms)
          statements(arm.body);
      }
    if (p_.main) statements(p_.main->body);
    for (const auto& test : p_.tests) statements(test.body);
    return result;
  }

  void gen_phase21_raised_type(std::ostringstream& o) const {
    const RaiseSet raised = phase21_program_raise_set();
    if (raised.empty()) return;
    o << "#[derive(Clone, Debug)]\n";
    o << "#[allow(private_interfaces)]\n";
    o << "pub enum __MossRaised {\n";
    for (const auto& identity : raised) {
      o << "    " << phase21_rust_variant(identity) << "(";
      o << phase21_rust_error_type(identity);
      o << "),\n";
    }
    o << "}\n\n";
    for (const auto& domain : p_.domains) {
      auto module = phase21_external_module_for_symbol(domain.name);
      if (!module) continue;
      for (const auto& handler : domain.handlers)
        if (handler.provider_abi &&
            handler_has_failure_captures(handler))
          o << "use " << tooling_name("moss_" + *module) << "::"
            << handler.provider_abi->raised_carrier_rust_type << ";\n";
    }
    if (program_uses_fileio(p_)) {
      o << "#[allow(dead_code)]\n"
        << "fn __moss_raise_fileio(error: MossFileError) -> __MossRaised {\n"
        << "    match error {\n";
      for (const char* name : {"NotFound", "PermissionDenied",
                               "NotRegularFile", "InUse", "Full", "IO"}) {
        const RaisedIdentity identity =
            RaisedIdentity::enum_variant("std__FileError", name);
        o << "        MossFileError::" << name << " => ";
        if (raised.count(identity))
          o << "__MossRaised::" << phase21_rust_variant(identity)
            << "(std__FileError::" << name << "),\n";
        else
          o << "std::process::abort(),\n";
      }
      o << "    }\n}\n\n";
    }
  }

  std::optional<string> phase21_external_module_for_symbol(
      const string& symbol) const {
    for (const auto& module : p_.external_modules)
      if (starts_with(symbol, module + "__")) return module;
    for (const auto& module : rust_dependencies_)
      if (module != "__moss_specializations__" &&
          starts_with(symbol, module + "__"))
        return module;
    return std::nullopt;
  }

  string phase21_external_raised_type(const string& module) const {
    return tooling_name("moss_" + module) + "::__MossRaised";
  }

  void gen_standard_error_external_bridges(std::ostringstream& o) const {
    for (const auto& module : rust_dependencies_) {
      if (module == "__moss_specializations__") continue;
      const string crate = tooling_name("moss_" + module);
      for (const char* short_name :
           {"FileError", "ParseError", "ConversionError"}) {
        const string local = standard_error_identity(short_name);
        const string external = crate + "::" + local;
        const auto cases = standard_error_variants(local);
        o << "impl From<" << external << "> for " << local << " {\n"
          << "    fn from(value: " << external << ") -> Self { match value {\n";
        for (const auto& name : cases)
          o << "        " << external << "::" << name << " => "
            << local << "::" << name << ",\n";
        o << "    } }\n}\n";
        o << "impl From<" << local << "> for " << external << " {\n"
          << "    fn from(value: " << local << ") -> Self { match value {\n";
        for (const auto& name : cases)
          o << "        " << local << "::" << name << " => "
            << external << "::" << name << ",\n";
        o << "    } }\n}\n";
      }
    }
  }

  void gen_phase21_external_raised_bridges(std::ostringstream& o) const {
    const RaiseSet raised = phase21_program_raise_set();
    if (raised.empty()) return;
    std::map<string, RaiseSet> by_module;
    for (const auto& function : semantic_program_->functions)
      if (auto module = phase21_external_module_for_symbol(function.name))
        by_module[*module].insert(function.observable_effects.raise_set.begin(),
                                  function.observable_effects.raise_set.end());
    for (const auto& object : semantic_program_->objects)
      if (auto module = phase21_external_module_for_symbol(object.name))
        for (const auto& method : object.methods)
          by_module[*module].insert(method.observable_effects.raise_set.begin(),
                                    method.observable_effects.raise_set.end());
    for (const auto& domain : semantic_program_->domains)
      if (auto module = phase21_external_module_for_symbol(domain.name))
        for (const auto& handler : domain.handlers)
          by_module[*module].insert(handler.observable_effects.raise_set.begin(),
                                    handler.observable_effects.raise_set.end());
    for (const auto& entry : by_module) {
      RaiseSet reachable;
      for (const auto& identity : entry.second)
        if (raised.count(identity)) reachable.insert(identity);
      if (reachable.empty()) continue;
      const string provider = phase21_external_raised_type(entry.first);
      auto bridge_payload = [&](const RaisedIdentity& identity,
                                bool from_provider) {
        const auto variants = standard_error_variants(identity.type);
        if (variants.empty()) return string("payload");
        const string external = tooling_name("moss_" + entry.first) +
            "::" + identity.type;
        const string source = from_provider ? external : identity.type;
        const string target = from_provider ? identity.type : external;
        string conversion = "match payload { ";
        for (const auto& variant : variants)
          conversion += source + "::" + variant + " => " + target +
              "::" + variant + ", ";
        return conversion + "}";
      };
      o << "impl From<" << provider << "> for __MossRaised {\n"
        << "    fn from(value: " << provider << ") -> Self {\n"
        << "        #[allow(unreachable_patterns)]\n"
        << "        match value {\n";
      for (const auto& identity : reachable) {
        const string variant = phase21_rust_variant(identity);
        o << "            " << provider << "::" << variant
          << "(payload) => __MossRaised::" << variant << "("
          << bridge_payload(identity, true) << "),\n";
      }
      o << "            _ => std::process::abort(),\n"
        << "        }\n    }\n}\n";
      o << "impl From<__MossRaised> for " << provider << " {\n"
        << "    fn from(value: __MossRaised) -> Self {\n"
        << "        #[allow(unreachable_patterns)]\n"
        << "        match value {\n";
      for (const auto& identity : reachable) {
        const string variant = phase21_rust_variant(identity);
        o << "            __MossRaised::" << variant << "(payload) => "
          << provider << "::" << variant << "("
          << bridge_payload(identity, false) << "),\n";
      }
      o << "            _ => std::process::abort(),\n"
        << "        }\n    }\n}\n\n";
    }
  }

  bool owns_specialization(const Domain& specialized) const {
    const string& source = specialization_sources_.at(specialized.name);
    return std::any_of(p_.domains.begin(), p_.domains.end(),
        [&](const Domain& domain) { return domain.name == source; });
  }

  bool has_domain_specializations(const string& domain) const {
    return std::any_of(
        p_.domain_specializations.begin(), p_.domain_specializations.end(),
        [&](const DomainSpecialization& specialization) {
          return specialization.materialized_layout && specialization.source_domain == domain;
        });
  }

  bool is_domain_specialization_instance(const string& source_domain,
                                         const string& instance) const {
    return std::any_of(
        p_.domain_specializations.begin(), p_.domain_specializations.end(),
        [&](const DomainSpecialization& specialization) {
          return specialization.materialized_layout && specialization.source_domain == source_domain &&
              specialization.instance == instance;
        });
  }

  string nominalized_generated_argument_type(
      const string& expression, const string& inferred) const {
    string binding = trim(expression);
    auto identity = domain_instance_bindings_.find(binding);
    if (tree_plain_identifier(binding) && identity != domain_instance_bindings_.end() &&
        is_domain_specialization_instance(identity->second.source_domain,
                                          identity->second.instance))
      return identity->second.source_domain;
    return inferred;
  }

  void remember_domain_instance_binding(const string& binding,
                                        const string& source_domain,
                                        const string& instance) {
    if (ambiguous_domain_instance_bindings_.count(binding)) return;
    auto found = domain_instance_bindings_.find(binding);
    if (found == domain_instance_bindings_.end()) {
      domain_instance_bindings_[binding] = {source_domain, instance};
      return;
    }
    if (found->second.source_domain != source_domain ||
        found->second.instance != instance) {
      domain_instance_bindings_.erase(found);
      ambiguous_domain_instance_bindings_.insert(binding);
    }
  }

  void forget_domain_instance_binding(const string& binding) {
    domain_instance_bindings_.erase(binding);
    ambiguous_domain_instance_bindings_.erase(binding);
  }

  static string comment_text(string text) {
    string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\r' || text[i] == '\n') {
        out.push_back(' ');
      } else if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
        out += "* /";
        ++i;
      } else {
        out.push_back(text[i]);
      }
    }
    return out;
  }

  static string handler_signature(const Handler& h) {
    if (!h.header.empty()) return h.header;
    std::ostringstream out;
    out << "on " << h.name << "(";
    for (size_t i = 0; i < h.params.size(); ++i) {
      if (i) out << ", ";
      out << h.params[i].name << ": " << h.params[i].type;
    }
    out << ")";
    if (h.reply_type) out << " -> " << *h.reply_type;
    return out.str();
  }

  static string state_field_signature(const Field& f) {
    if (!f.header.empty()) return f.header;
    string out = "var " + f.name + ": " + f.type;
    if (!f.init.empty()) out += " = " + f.init;
    return out;
  }

  static void source_comment(std::ostringstream& o, int spaces, int line,
                             const string& source) {
    if (line <= 0) return;
    o << string(spaces, ' ') << "// Moss line " << line;
    if (!source.empty()) o << ": " << comment_text(source);
    o << "\n";
  }

  static void backend_comment(std::ostringstream& o, int spaces, const string& text) {
    o << string(spaces, ' ') << "// Moss backend: " << comment_text(text) << "\n";
  }

  static void tooling_begin(std::ostringstream& o, int spaces,
                            const string& kind, const string& semantic_identity,
                            const string& generated_symbol,
                            const string& native_symbol = "") {
    o << string(spaces, ' ') << "// Moss tooling begin|" << kind << "|"
      << semantic_identity << "|" << generated_symbol << "|" << native_symbol
      << "\n";
  }

  static void tooling_end(std::ostringstream& o, int spaces,
                          const string& semantic_identity) {
    o << string(spaces, ' ') << "// Moss tooling end|" << semantic_identity
      << "\n";
  }

  void debug_symbol_attributes(std::ostringstream& o, int spaces,
                               const string& native_symbol,
                               bool can_export = true) const {
    if (debug_build_) o << string(spaces, ' ') << "#[inline(never)]\n";
    if (can_export)
      o << string(spaces, ' ') << "#[export_name = \"" << native_symbol
        << "\"]\n";
  }

  static string function_semantic_identity(
      const Function& function,
      const FunctionSpecialization* specialization = nullptr) {
    string identity = functional_function_context(function, specialization) +
        "@" + std::to_string(function.line);
    if (specialization)
      identity += "#" + specialization->stable_type_key;
    return identity;
  }

  static string method_semantic_identity(const ObjectType& object,
                                         const Method& method) {
    return functional_method_context(object, method) + "@" +
        std::to_string(method.line);
  }

  static string handler_semantic_identity(const Domain& domain,
                                          const Handler& handler) {
    return functional_handler_context(domain, handler) + "@" +
        std::to_string(handler.line);
  }

  // Mirrors Checker::echo_display_class for an already-checked Vector type.
  static bool native_echo_vector_displayable(const string& t) {
    string x = canonical_type_name(t);
    if (x == "int" || x == "float" || x == "bool" || x == "char" || x == "string" ||
        x == "vector" || internal_semantic_placeholder(x))
      return true;
    if (starts_with(x, "vector[") && ends_with(x, "]"))
      return native_echo_vector_displayable(trim(x.substr(7, x.size() - 8)));
    return false;
  }

  // The statically selected display-runtime formatter for one Vector element
  // type: a helper function for a scalar, or a closure that formats a nested
  // Vector with its own element formatter. nullopt when the type is open.
  static std::optional<string> native_echo_element(const string& t) {
    string x = canonical_type_name(t);
    if (x == "int") return string("__moss_display_int");
    if (x == "float") return string("__moss_display_float");
    if (x == "bool") return string("__moss_display_bool");
    if (x == "char") return string("__moss_display_char");
    if (x == "string") return string("__moss_display_string");
    if (starts_with(x, "vector[") && ends_with(x, "]")) {
      auto inner = native_echo_element(trim(x.substr(7, x.size() - 8)));
      if (!inner) return std::nullopt;
      return "|o: &mut String, v: &std::vec::Vec<_>| __moss_display_vector_into(o, v, &" +
             *inner + ")";
    }
    return std::nullopt;
  }

  bool shared_nominal_map_key(const string& key) const {
    const string concrete = canonical_type_name(key);
    return staged_abi_v10() &&
        p_.key_proof_ids_by_type.count(concrete) &&
        (objects_.count(concrete) || is_nominal_instance_type(concrete));
  }

  bool shared_protocol_map_key(const string& key) const {
    const string concrete = canonical_type_name(key);
    return shared_nominal_map_key(concrete) ||
        (staged_abi_v10() && concrete == "float" &&
         p_.key_proof_ids_by_type.count(concrete));
  }

  std::optional<string> shared_protocol_map_key_from_type(
      const string& type) const {
    auto elements = map_key_value_types(canonical_type_name(type));
    if (elements && shared_protocol_map_key(elements->first))
      return elements->first;
    return std::nullopt;
  }

  string rust_type(const string& t) const {
    string x = canonical_type_name(t);
    if (x == "int") return "i64";
    if (x == "float") return "f64";
    if (x == "bool") return "bool";
    if (x == "char") return "char";
    if (x == "string") return "MossStr";
    // Phase 20: runtime-facing semantic names reserved in
    // docs/ROOT_RUNTIME_ABI.md ("Runtime-facing semantic types"). Agent
    // D does not define these Rust types; Agent B's crate does.
    if (x == "FileIO") return "FileIO";
    if (x == "Range") return "Range";
    if (x == "RangeBatch") return "RangeBatch";
    if (x == "vector") return "std::vec::Vec<T>";
    if (x == "map") return "std::collections::HashMap<K, V>";
    if (x == "queue") return "std::collections::VecDeque<T>";
    if (x == "unit") return "()";
    if (domains_.count(x))
      return has_domain_specializations(x) ? x + "Handle" : x + "Ref";
    if (is_nominal_instance_type(x)) {
      string tname = nominal_template_name(x);
      auto subs = nominal_field_substitutions(x);
      string res = tname + "<";
      for (size_t i = 0; i < subs.size(); ++i) {
        if (i > 0) res += ", ";
        res += rust_type(subs[i].second);
      }
      res += ">";
      return res;
    }
    if (objects_.count(x) || enums_.count(x)) return x;
    if (starts_with(x, "seq[") && ends_with(x, "]"))
      return "std::vec::Vec<" + rust_type(x.substr(4, x.size()-5)) + ">";
    if (starts_with(x, "vector[") && ends_with(x, "]")) return "std::vec::Vec<" + rust_type(x.substr(7, x.size()-8)) + ">";
    if (starts_with(x, "queue[") && ends_with(x, "]")) return "std::collections::VecDeque<" + rust_type(x.substr(6, x.size()-7)) + ">";
    if (staged_abi_v10() && starts_with(x, "set[") && ends_with(x, "]"))
      return "moss_runtime::MossSet<" + rust_type(x.substr(4, x.size()-5)) + ">";
    if (starts_with(x, "map[") && ends_with(x, "]")) {
      auto parsed = parsed_type_arguments(x);
      if (!parsed || parsed->size() != 2) return x;
      const auto& ps = *parsed;
      if (shared_protocol_map_key(ps[0]))
        return "moss_runtime::MossMap<" + rust_type(ps[0]) + ", " +
               rust_type(ps[1]) + ">";
      return "std::collections::HashMap<" + rust_type(ps[0]) + ", " + rust_type(ps[1]) + ">";
    }
    if (starts_with(x, "option[") && ends_with(x, "]"))
      return "Option<" + rust_type(x.substr(7, x.size()-8)) + ">";
    if (starts_with(x, "table[") && ends_with(x, "]")) {
      auto parsed = parsed_type_arguments(x);
      if (!parsed || parsed->size() != 2) return x;
      const auto& ps = *parsed;
      return "std::collections::HashMap<" + rust_type(ps[0]) + ", " + rust_type(ps[1]) + ">";
    }
    return x;
  }

  std::set<string> constraint_ops(const Function& f, const string& subject) const {
    std::set<string> out;
    for (const auto& c : f.constraints) if (c.subject == subject) {
      if (c.kind == ConstraintKind::Operator) out.insert(c.detail);
      if (c.kind == ConstraintKind::Indexable) out.insert("[]");
    }
    return out;
  }

  string default_value(const string& type) const {
    string t = canonical_type_name(type);
    if (t == "int") return "0";
    if (t == "float") return "0.0";
    if (t == "bool") return "false";
    if (t == "string") return "MossStr::new()";
    if (starts_with(t, "seq[")) return "std::vec::Vec::new()";
    if (starts_with(t, "table[")) return "std::collections::HashMap::new()";
    if (starts_with(t, "option[")) return "None";
    auto object = objects_.find(t);
    if (object != objects_.end()) {
      string result = rust_type(t) + " { ";
      for (const auto& field : object->second->fields)
        result += field.name + ": " + default_value(field.type) + ", ";
      return result + "}";
    }
    return "Default::default()";
  }

  bool copy_type(const string& type) const {
    return copy_type_name(canonical_type_name(type));
  }

  std::optional<est::ExpressionView> expression_view_from_est(
      std::string_view expression,
      const vector<est::Handle>* handles = nullptr) const {
    const auto fingerprint = est::fingerprint(string(expression));
    const auto* selected = handles ? handles
        : active_functional_expression_handles_;
    if (selected) {
      for (const auto& handle : *selected) {
        if (handle.source_fingerprint != fingerprint ||
            handle.arena_index >= p_.expression_arenas.size() ||
            !p_.expression_arenas[handle.arena_index]) continue;
        const auto& arena = *p_.expression_arenas[handle.arena_index];
        const auto span = arena.span(handle.expr);
        // A fingerprint can match an identical expression nested in a larger
        // source slot. Its node spans are relative to that parent buffer, so
        // pairing it with the child text breaks EST projections (notably
        // arithmetic in a Root message argument). Reparse unless this handle
        // is rooted at the complete supplied expression.
        if (span.source_begin != 0 || span.source_end != expression.size())
          continue;
        return est::view(arena, handle.expr, expression);
      }
    }
    return est::parse_view(expression);
  }

  bool tree_plain_identifier(std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax) return false;
    string name;
    return est::project_identifier(*syntax, name);
  }

  bool tree_empty_constructor_named(std::string_view expression,
                                    std::string_view name) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::empty_constructor_named(*syntax, name);
  }

  bool tree_empty_vector_literal(std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::empty_vector_literal(*syntax);
  }

  std::optional<string> tree_identifier_name(
      std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax) return std::nullopt;
    string name;
    return est::project_identifier(*syntax, name)
        ? std::optional<string>(std::move(name)) : std::nullopt;
  }

  bool tree_expressions_equal(std::string_view left,
                              std::string_view right) const {
    auto left_tree = expression_view_from_est(left);
    auto right_tree = expression_view_from_est(right);
    return left_tree && right_tree &&
        est::structurally_equal(*left_tree, *right_tree);
  }

  bool tree_empty_string_literal(std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax || !syntax->arena || syntax->root >= syntax->arena->size())
      return false;
    const auto* literal = std::get_if<est::Literal>(
        &syntax->arena->at(syntax->root));
    return literal && literal->spelling == "\"\"";
  }

  std::optional<vector<string>> tree_member_path(
      std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax || !syntax->arena || syntax->root >= syntax->arena->size())
      return std::nullopt;
    vector<string> reverse;
    est::ExprId current = syntax->root;
    while (current < syntax->arena->size()) {
      const auto& node = syntax->arena->at(current);
      if (const auto* member = std::get_if<est::Member>(&node)) {
        reverse.push_back(member->name);
        current = member->base;
      } else if (const auto* identifier =
                     std::get_if<est::Identifier>(&node)) {
        reverse.push_back(identifier->name);
        std::reverse(reverse.begin(), reverse.end());
        return reverse;
      } else {
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<vector<string>> tree_tuple_elements(
      std::string_view expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax ? est::project_tuple_elements(*syntax) : std::nullopt;
  }

  bool tree_parse_simple_call(const string& expression, string& callee,
                              vector<string>& arguments) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_simple_call(*syntax, callee, arguments);
  }

  bool tree_parse_member_call(const string& expression, string& receiver,
                              string& member, vector<string>& arguments,
                              bool* outputs_written = nullptr) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax) {
      if (outputs_written) *outputs_written = false;
      return false;
    }
    return est::project_member_call(*syntax, receiver, member, arguments,
                                    outputs_written);
  }

  bool tree_parse_member_access(const string& expression, string& base,
                               string& member) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_member_access(*syntax, base, member);
  }

  bool tree_parse_unary(const string& expression, string& operation,
                        string& operand) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_unary(*syntax, operation, operand);
  }

  bool tree_parse_message(const string& expression, string& target,
                          string& handler, vector<string>& arguments) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_message(
        *syntax, target, handler, arguments);
  }

  bool tree_parse_fileio_open(const string& expression,
                              vector<string>& arguments) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_fileio_open(*syntax, arguments);
  }

  bool tree_parse_fileio_chunks_source(const string& expression,
                                       string& receiver,
                                       string& size_expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_fileio_chunks_source(
        *syntax, receiver, size_expression);
  }

  bool tree_parse_index(const string& expression, string& base,
                        string& index) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_index(*syntax, base, index);
  }

  bool tree_parse_named_argument(const string& expression, string& name,
                                 string& value) const {
    auto syntax = est::parse_named_argument_view(expression);
    return syntax && est::project_named_argument(*syntax, name, value);
  }

  std::optional<vector<string>> tree_parse_vector_literal(
      const string& expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax ? est::project_vector_literal(*syntax) : std::nullopt;
  }

  bool tree_parse_enum_case_expression(const string& expression,
                                       string& enum_name, string& case_name,
                                       vector<string>& fields,
                                       bool& called) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_enum_case(*syntax, enum_name, case_name,
                                            fields, &called);
  }

  bool tree_parse_bound_method_callable(const string& expression,
                                        string& receiver,
                                        string& method) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::project_bound_method(*syntax, receiver, method);
  }

  string tree_strip_expression_grouping(const string& expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax ? est::strip_grouping(*syntax) : expression;
  }

  string tree_normalize_pipeline(const string& expression) const {
    auto syntax = expression_view_from_est(expression);
    return syntax ? est::normalize_pipeline_tree(*syntax) : expression;
  }

  bool tree_expression_mentions_identifier(
      const string& expression, const string& identifier) const {
    auto syntax = expression_view_from_est(expression);
    return syntax && est::expression_mentions(*syntax, identifier);
  }

  std::optional<vector<string>> parsed_type_arguments(
      const string& type) const {
    auto parsed = Type::parse_canonical(type);
    if (!parsed) return std::nullopt;
    vector<string> result;
    result.reserve((*parsed)->type_args.size());
    for (const auto& argument : (*parsed)->type_args)
      result.push_back(argument->to_canonical_string());
    return result;
  }

  bool borrowable_type(const string& type) const {
    string t = canonical_type_name(type);
    if (t.empty() || contains_internal_placeholder(t) || copy_type(t) || domains_.count(t)) return false;
    return true;
  }

  ConcreteParameterABI function_parameter_abi(
      const Function& function, size_t index, const string& concrete_type,
      const FunctionSpecialization* specialization = nullptr) const {
    Effect effect = specialization && index < specialization->parameter_effects.size()
        ? specialization->parameter_effects[index]
        : specialized_iterator_parameter_effect(function, index, concrete_type, objects_);
    if (specialization) return concrete_parameter_abi(effect, concrete_type);
    // Preserve unspecialized/explicitly typed ABI, including primitive WRITE.
    bool borrow = effect == Effect::Write ||
        (effect != Effect::Consume && borrowable_type(concrete_type));
    auto ops = constraint_ops(function, function.params[index].name);
    if (effect == Effect::Read && function.params[index].type.empty() &&
        !ops.empty() && !ops.count("[]")) borrow = false;
    return {effect, !borrow ? ParameterPassingMode::Value :
        effect == Effect::Write ? ParameterPassingMode::MutableBorrow :
                                 ParameterPassingMode::SharedBorrow};
  }

  Effect function_effect(const Function& function, size_t index,
                         const string& concrete_type = {},
                         const FunctionSpecialization* specialization = nullptr) const {
    return function_parameter_abi(function, index, concrete_type, specialization).effect;
  }

  static Effect method_effect(const Method& method, size_t index) {
    return index < method.parameter_effects.size()
        ? method.parameter_effects[index] : Effect::Read;
  }

  const FunctionSpecialization* call_specialization(
      const Function& function, const vector<string>& arguments,
      const std::unordered_map<string,string>* types) const {
    if (codegen_result_specialization_ &&
        !codegen_result_specialization_->result_call_key.empty()) {
      string direct_callee;
      vector<string> direct_arguments;
      if (tree_parse_simple_call(trim(codegen_result_expression_), direct_callee,
                            direct_arguments) &&
          direct_callee == function.name && direct_arguments == arguments) {
        auto exact = std::find_if(function.specializations.begin(),
                                  function.specializations.end(),
                                  [&](const FunctionSpecialization& candidate) {
          return candidate.stable_type_key ==
              codegen_result_specialization_->result_call_key;
        });
        if (exact == function.specializations.end())
          throw std::runtime_error(
              "checked returned-call key is absent from native projection");
        return &*exact;
      }
    }
    if (phase21_codegen_statement_) {
      const Stmt& statement = *phase21_codegen_statement_;
      string nested_expression = function.name + "(";
      for (size_t index = 0; index < arguments.size(); ++index) {
        if (index) nested_expression += ", ";
        nested_expression += arguments[index];
      }
      nested_expression += ")";
      auto nested_context = statement.nested_open_call_keys_by_context.find(
          codegen_active_specialization_key_);
      if (nested_context == statement.nested_open_call_keys_by_context.end())
        nested_context = statement.nested_open_call_keys_by_context.find(
            codegen_body_context_);
      if (nested_context != statement.nested_open_call_keys_by_context.end()) {
        auto nested_key = nested_context->second.find(nested_expression);
        if (nested_key != nested_context->second.end()) {
          const auto& keys = nested_key->second;
          const uint32_t occurrence =
              codegen_nested_argument_occurrence_.value_or(0);
          if (occurrence >= keys.size() || keys[occurrence].empty())
            throw std::runtime_error(
                "checked nested open-result occurrence has no exact key");
          auto exact = std::find_if(function.specializations.begin(),
                                    function.specializations.end(),
                                    [&](const FunctionSpecialization& candidate) {
            return candidate.stable_type_key == keys[occurrence];
          });
          if (exact == function.specializations.end())
            throw std::runtime_error(
                "checked nested open-result key is absent from native projection");
          return &*exact;
        }
      }
      auto selected = statement.open_call_keys_by_context.find(
          codegen_active_specialization_key_);
      if (selected == statement.open_call_keys_by_context.end())
        selected = statement.open_call_keys_by_context.find(
            codegen_body_context_);
      if (selected == statement.open_call_keys_by_context.end())
        selected = statement.open_call_keys_by_context.find("");
      string direct_callee;
      vector<string> direct_arguments;
      const string& call_expression = statement.kind == Stmt::Kind::Return
          ? statement.a : statement.b;
      if (selected != statement.open_call_keys_by_context.end() &&
          tree_parse_simple_call(trim(call_expression), direct_callee,
                            direct_arguments) &&
          direct_callee == function.name &&
          direct_arguments == arguments) {
        auto exact = std::find_if(function.specializations.begin(),
                                  function.specializations.end(),
                                  [&](const FunctionSpecialization& candidate) {
          return candidate.stable_type_key == selected->second;
        });
        if (exact == function.specializations.end())
          throw std::runtime_error(
              "checked open-result specialization key is absent from native projection");
        return &*exact;
      }
    }
    vector<string> concrete_types;
    for (const auto& argument : arguments)
      concrete_types.push_back(generated_expr_type(argument, types).value_or(""));
    return concrete_specialization(function, concrete_types);
  }

  string nominal_domain_handle_argument(
      const string& expression, const string& type, const Domain* d,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      size_t functional_pipeline_id = 0) const {
    string rendered = expr(expression, d, locals, types, functional_pipeline_id);
    string nominal = canonical_type_name(type);
    string binding = trim(expression);
    auto identity = domain_instance_bindings_.find(binding);
    if (types && tree_plain_identifier(binding) && domains_.count(nominal) &&
        identity != domain_instance_bindings_.end() &&
        identity->second.source_domain == nominal &&
        is_domain_specialization_instance(nominal, identity->second.instance)) {
      rendered = nominal + "Handle::from(" + rendered + ")";
    }
    return rendered;
  }

  // key-protocol-v1 scalar operations lowered to the exact Moss operation
  // for the statically known type; no Rust trait or adapter is involved.
  static string scalar_protocol_rust(const string& op, const string& type,
                                     const string& a, const string& b = "") {
    const string t = canonical_type_name(type);
    if (op == "less") {
      if (t == "float") return "moss_float_less(" + a + ", " + b + ")";
      return "(&(" + a + ") < &(" + b + "))";
    }
    if (op == "isequal") {
      if (t == "float") return "moss_float_isequal(" + a + ", " + b + ")";
      return "(&(" + a + ") == &(" + b + "))";
    }
    if (t == "float") return "moss_float_hash(" + a + ")";
    if (t == "int") return "(" + a + ")";
    if (t == "bool") return "(if " + a + " { 1i64 } else { 0i64 })";
    if (t == "char") return "((" + a + ") as u32 as i64)";
    return "moss_string_hash(&(" + a + "))";
  }

  string shared_map_hash(const string& key_type, const string& key) const {
    return shared_nominal_map_key(key_type) ? "(" + key + ").hash()"
        : scalar_protocol_rust("hash", key_type, key);
  }

  string shared_map_equal(const string& key_type, const string& a,
                          const string& b) const {
    return shared_nominal_map_key(key_type) ? "(" + a + ").isequal(" + b + ")"
        : scalar_protocol_rust("isequal", key_type, "*" + a, "*" + b);
  }

  // Rust comparator for a key-protocol-v1 total-order scalar. Float uses
  // the Julia-like total order; the other scalars' Rust Ord already matches
  // Moss `less` (Bool false < true, String by UTF-8 bytes = code points).
  static string total_order_compare_rust(const string& key_type,
                                         const string& left, const string& right) {
    if (canonical_type_name(key_type) == "float")
      return "moss_float_total_cmp(" + left + ", " + right + ")";
    return "(" + left + ").cmp(&(" + right + "))";
  }

  // Lowers sort(v), sort(v, by: key), and sort(v, by: Reverse(key)) to a
  // stable in-place Rust sort. The checker has proven the key pure, so each
  // key is lowered as an ordinary specialized Moss call.
  string sort_call_rust(const vector<string>& args, const Domain* d,
                        const std::set<string>& locals,
                        const std::unordered_map<string,string>* types) const {
    string target = write_call_place(args[0], d, locals, types);
    auto vector_type = generated_expr_type(args[0], types);
    string canonical = vector_type ? canonical_type_name(*vector_type) : "";
    string element = starts_with(canonical, "vector[")
        ? canonical_type_name(canonical.substr(7, canonical.size() - 8)) : "";
    if (args.size() == 1)
      return "(" + target + ").sort_by(|__moss_a, __moss_b| " +
             total_order_compare_rust(element, "*__moss_a", "*__moss_b") + ")";
    string label, key;
    tree_parse_named_argument(args[1], label, key);
    bool reverse = false;
    string reverse_callee;
    vector<string> reverse_args;
    if (tree_parse_simple_call(key, reverse_callee, reverse_args) &&
        reverse_callee == "Reverse" && reverse_args.size() == 1) {
      reverse = true;
      key = trim(reverse_args.front());
    }
    const bool copy = copy_type(element);
    auto key_locals = locals;
    key_locals.insert("__moss_sort_a");
    key_locals.insert("__moss_sort_b");
    std::unordered_map<string,string> key_types = types ? *types
        : std::unordered_map<string,string>{};
    key_types["__moss_sort_a"] = element;
    key_types["__moss_sort_b"] = element;
    string key_a = expr(key + "(__moss_sort_a)", d, key_locals, &key_types);
    string key_b = expr(key + "(__moss_sort_b)", d, key_locals, &key_types);
    string key_type = generated_expr_type(key + "(__moss_sort_a)", &key_types)
        .value_or("");
    string compare = reverse
        ? total_order_compare_rust(key_type, "__moss_kb", "__moss_ka")
        : total_order_compare_rust(key_type, "__moss_ka", "__moss_kb");
    // A key that consumes its argument receives an owned copy; the vector
    // element itself is never moved out during the sort.
    auto key_function = functions_.find(key);
    const bool consumes = !copy && key_function != functions_.end() &&
        sort_key_effect(*key_function->second, element) == Effect::Consume;
    string bind = copy ? "*__moss_a" : consumes ? "(*__moss_a).clone()" : "__moss_a";
    string bind_b = copy ? "*__moss_b" : consumes ? "(*__moss_b).clone()" : "__moss_b";
    return "(" + target + ").sort_by(|__moss_a, __moss_b| { "
           "let __moss_sort_a = " + bind + "; let __moss_sort_b = " + bind_b + "; "
           "let __moss_ka = " + key_a + "; let __moss_kb = " + key_b + "; " +
           compare + " })";
  }

  string write_call_place(const string& argument, const Domain* d,
                          const std::set<string>& locals,
                          const std::unordered_map<string,string>* types) const {
    string value = tree_strip_expression_grouping(trim(argument));
    // Borrowed object views need their mutable accessors. For paths that
    // cross a container, recursively lower the base as a place, never as an
    // owned read expression (index reads may clone non-Copy elements).
    if (view_place(value, d, locals, types))
      return place_expr(value, d, locals, types);
    string base, index;
    if (!tree_parse_index(value, base, index)) {
      string field;
      if (tree_parse_member_access(value, base, field)) {
        return "(" + write_call_place(base, d, locals, types) +
            ")." + rust_identifier(field);
      }
      return place_expr(value, d, locals, types);
    }
    string storage = write_call_place(base, d, locals, types);
    string key = expr(index, d, locals, types);
    auto type = generated_expr_type(base, types);
    if (type && shared_protocol_map_key_from_type(*type)) {
      const string key_type = *shared_protocol_map_key_from_type(*type);
      return "*({ let __moss_key = " + key + "; (" + storage +
          ").get_mut_with(" + shared_map_hash(key_type, "__moss_key") +
          ", &__moss_key, |a, b| " + shared_map_equal(key_type, "a", "b") +
          ").expect(\"Moss missing map key\") })";
    }
    if (type && (starts_with(canonical_type_name(*type), "map[") || canonical_type_name(*type) == "map"))
      return "*(" + storage + ").get_mut(&(" + key + ")).expect(\"Moss missing map key\")";
    if (type && (canonical_type_name(*type) == "RangeBatch" || canonical_type_name(*type) == "Range"))
      return "(" + storage + ").get(" + key + ")";
    return "(" + storage + ")[(" + key + ") as usize]";
  }

  string read_call_place(const string& argument, const Domain* d,
                         const std::set<string>& locals,
                         const std::unordered_map<string,string>* types) const {
    string base, index;
    if (!tree_parse_index(trim(argument), base, index)) return expr(argument, d, locals, types);
    string storage = read_call_place(base, d, locals, types);
    string key = expr(index, d, locals, types);
    auto type = generated_expr_type(base, types);
    if (type && shared_protocol_map_key_from_type(*type)) {
      const string key_type = *shared_protocol_map_key_from_type(*type);
      return "*({ let __moss_key = " + key + "; (" + storage +
          ").get_with(" + shared_map_hash(key_type, "__moss_key") +
          ", &__moss_key, |a, b| " + shared_map_equal(key_type, "a", "b") +
          ").expect(\"Moss missing map key\") })";
    }
    if (type && (starts_with(canonical_type_name(*type), "map[") || canonical_type_name(*type) == "map"))
      return "*(" + storage + ").get(&(" + key + ")).expect(\"Moss missing map key\")";
    if (type && (canonical_type_name(*type) == "RangeBatch" || canonical_type_name(*type) == "Range"))
      return "(" + storage + ").get(" + key + ")";
    return "(" + storage + ")[(" + key + ") as usize]";
  }

  string function_call_argument(const Function& function, size_t index,
                                const string& argument, const Domain* d,
                                const std::set<string>& locals,
                                const std::unordered_map<string,string>* types,
                                size_t functional_pipeline_id = 0,
                                const FunctionSpecialization* specialization = nullptr) const {
    if (index >= function.params.size())
      return nominal_domain_handle_argument(argument, "", d, locals, types,
                                            functional_pipeline_id);
    string actual_type = generated_expr_type(argument, types).value_or("");
    auto abi = function_parameter_abi(function, index, actual_type, specialization);
    Effect effect = abi.effect;
    if (effect == Effect::Consume &&
        phase21_option_slots_.count(trim(argument)))
      return "__moss_recovery_slot_" + trim(argument) +
          ".take().unwrap_or_else(|| std::process::abort())";
    string rendered = nominal_domain_handle_argument(
        argument, function.params[index].type, d, locals, types,
        functional_pipeline_id);
    if (abi.passing == ParameterPassingMode::Value) return rendered;
    // Non-object payloads arrive through the internal handler ABI as `&T`.
    // Preserve that borrow when handing it to a READ helper. Object payloads
    // retain the extra reference below because view helpers take
    // `&impl MossAccess_T`.
    if (effect == Effect::Read && borrowed_message_parameter(argument) &&
        !view_object_type(actual_type))
      return rendered;
    if (effect == Effect::Write) return "&mut (" + write_call_place(argument, d, locals, types) + ")";
    return "&(" + read_call_place(argument, d, locals, types) + ")";
  }

  string method_call_argument(const Method& method, size_t index,
                              const string& argument, const Domain* d,
                              const std::set<string>& locals,
                              const std::unordered_map<string,string>* types,
                              size_t functional_pipeline_id = 0) const {
    if (index >= method.params.size())
      return nominal_domain_handle_argument(argument, "", d, locals, types,
                                            functional_pipeline_id);
    Effect effect = method_effect(method, index);
    if (effect == Effect::Consume &&
        phase21_option_slots_.count(trim(argument)))
      return "__moss_recovery_slot_" + trim(argument) +
          ".take().unwrap_or_else(|| std::process::abort())";
    string rendered = nominal_domain_handle_argument(
        argument, method.params[index].type, d, locals, types,
        functional_pipeline_id);
    string type = method.params[index].type;
    if (effect == Effect::Consume || (effect != Effect::Write && !borrowable_type(type))) return rendered;
    if (effect == Effect::Read && borrowed_message_parameter(argument) &&
        !view_object_type(generated_expr_type(argument, types).value_or("")))
      return rendered;
    if (effect == Effect::Write) return "&mut (" + write_call_place(argument, d, locals, types) + ")";
    return "&(" + read_call_place(argument, d, locals, types) + ")";
  }

  struct GeneratedBinaryExpression {
    string left;
    string op;
    string right;
  };

  static bool generated_integer_literal(const string& expression) {
    string value = trim(expression);
    size_t start = !value.empty() && (value.front() == '+' || value.front() == '-')
        ? 1 : 0;
    return start < value.size() &&
        std::all_of(value.begin() + static_cast<std::ptrdiff_t>(start), value.end(),
                    [](char ch) {
                      return std::isdigit(static_cast<unsigned char>(ch));
                    });
  }

  static string generated_integer_literal_as_float(string expression) {
    // An Int literal in a Float context converts exactly as `as f64` does.
    auto decoded = decode_numeric_literal_expression(expression);
    if (!decoded || decoded->kind != NumericLiteral::Kind::Int)
      throw std::runtime_error("internal error: numeric literal '" + expression +
                               "' was not validated by the lexer");
    DecodedNumber as_float;
    as_float.kind = NumericLiteral::Kind::Float;
    as_float.floating = static_cast<double>(decoded->integer);
    return rust_numeric_literal(as_float);
  }

  std::optional<GeneratedBinaryExpression> tree_generated_binary(
      const string& expression, const vector<string>& operators) const {
    auto syntax = expression_view_from_est(expression);
    if (!syntax) return std::nullopt;
    vector<string> tree_operators;
    tree_operators.reserve(operators.size());
    for (const auto& op : operators) tree_operators.push_back(trim(op));
    std::pair<string,string> operands;
    string operation;
    if (!est::project_binary(*syntax, tree_operators, operands, operation))
      return std::nullopt;
    return GeneratedBinaryExpression{std::move(operands.first),
                                     std::move(operation),
                                     std::move(operands.second)};
  }

  static std::optional<string> generated_functional_element_type(
      const string& type) {
    const auto term = parsed_type(type);
    if (applied_builtin(term, {"vector", "seq"}) && term->type_args.size() == 1)
      return term->type_args[0]->to_canonical_string();
    return std::nullopt;
  }

  string resolved_callable_identity(
      string callable, const std::unordered_map<string,string>* types) const {
    callable = trim(std::move(callable));
    if (types) {
      auto local = types->find(callable);
      if (local != types->end() && callable_identity(local->second)) {
        if (starts_with(local->second, "callable-v1:"))
          return local->second;
        return local->second.substr(9);
      }
    }
    return callable;
  }

  static bool generated_expression_uses(const string& expression,
                                        const string& name) {
    bool in_string = false, escaped = false;
    for (size_t index = 0; index < expression.size();) {
      char ch = expression[index];
      if (in_string) {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == '"') in_string = false;
        ++index;
        continue;
      }
      if (ch == '"') { in_string = true; ++index; continue; }
      if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')) {
        ++index;
        continue;
      }
      size_t end = index + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) ||
              expression[end] == '_'))
        ++end;
      if (expression.compare(index, end - index, name) == 0) return true;
      index = end;
    }
    return false;
  }

  std::optional<string> generated_callable_result(
      const string& callable, const vector<string>& input_types,
      const std::unordered_map<string,string>* types) const {
    if (generated_expression_uses(callable, "_")) {
      if (input_types.size() != 1) return std::nullopt;
      std::unordered_map<string,string> placeholder_types = types
          ? *types : std::unordered_map<string,string>{};
      placeholder_types["_"] = input_types.front();
      return generated_expr_type(callable, &placeholder_types);
    }
    string bound_receiver, bound_method;
    if (tree_parse_bound_method_callable(callable, bound_receiver, bound_method)) {
      auto receiver_type = generated_expr_type(bound_receiver, types);
      const Method* method = receiver_type
          ? resolve_object_method(objects_, canonical_type_name(*receiver_type),
                                  bound_method, input_types, false, nullptr)
          : nullptr;
      if (method && method->return_type)
        return canonical_type_name(*method->return_type);
      return std::nullopt;
    }
    string identity = resolved_callable_identity(callable, types);
    const Function* fn = nullptr;
    if (starts_with(identity, "callable-v1:")) {
      fn = find_function_by_callable_id(identity);
    } else {
      auto function = functions_.find(identity);
      if (function != functions_.end()) fn = function->second;
    }
    if (!fn) return std::nullopt;
    if (fn->static_dispatch) {
      if (const auto* specialization =
              functional_specialization(*fn, input_types))
        return specialization->return_type;
      return std::nullopt;
    }
    if (fn->return_type &&
        !contains_internal_placeholder(*fn->return_type))
      return canonical_type_name(*fn->return_type);
    return std::nullopt;
  }

  std::optional<ParsedFunctionalPipeline> functional_pipeline_from_est(
      std::string_view expression,
      const vector<est::Handle>* handles = nullptr) const {
    if (expression.find("|>") == std::string_view::npos)
      return std::nullopt;
    const auto resolve_stage = [](std::string_view name)
        -> std::optional<FunctionalNodeKind> {
      for (const auto& operation : functional_operations())
        if (name == operation.name) return operation.kind;
      return std::nullopt;
    };
    const auto fingerprint = est::fingerprint(string(expression));
    auto convert = [&](std::uint32_t arena_index, est::ExprId root)
        -> std::optional<ParsedFunctionalPipeline> {
      if (arena_index >= p_.expression_arenas.size() ||
          !p_.expression_arenas[arena_index]) return std::nullopt;
      return est::functional_pipeline(*p_.expression_arenas[arena_index], root,
                                      expression, resolve_stage);
    };
    const auto* selected_handles = handles ? handles
        : active_functional_expression_handles_;
    if (selected_handles) {
      for (const auto& handle : *selected_handles)
        if (handle.source_fingerprint == fingerprint)
          if (auto parsed = convert(handle.arena_index, handle.expr))
            return parsed;
    }

    // Optimizer-created and nested expression occurrences may not retain a
    // root handle. Parse those expressions into EST and consume the Pipeline
    // node, keeping the emitter off the legacy pipeline splitter.
    est::Arena temporary;
    try {
      const auto root = est::parse_expression(temporary, expression);
      return est::functional_pipeline(temporary, root, expression,
                                      resolve_stage);
    } catch (const std::invalid_argument&) {
      return std::nullopt;
    }
  }

  std::optional<string> generated_functional_pipeline_type(
      const string& expression,
      const std::unordered_map<string,string>* types) const {
    auto pipeline = functional_pipeline_from_est(expression);
    if (!pipeline) return std::nullopt;
    auto source_type = generated_expr_type(pipeline->source, types);
    if (!source_type) return std::nullopt;
    auto element = generated_functional_element_type(*source_type);
    if (!element) return std::nullopt;
    string current = *element;
    string output = "vector[" + current + "]";
    for (const auto& stage : pipeline->stages) {
      switch (stage.kind) {
        case FunctionalNodeKind::Map: {
          if (stage.arguments.size() != 1) return std::nullopt;
          auto result = generated_callable_result(stage.arguments.front(),
                                                  {current}, types);
          if (!result) return std::nullopt;
          current = *result;
          output = "vector[" + current + "]";
          break;
        }
        case FunctionalNodeKind::Filter:
          output = "vector[" + current + "]";
          break;
        case FunctionalNodeKind::Reduce:
          if (stage.arguments.size() != 2) return std::nullopt;
          output = generated_expr_type(stage.arguments.front(), types).value_or("");
          break;
        case FunctionalNodeKind::Sum:
          output = current;
          break;
        case FunctionalNodeKind::Count:
          output = "int";
          break;
        case FunctionalNodeKind::Any:
        case FunctionalNodeKind::All:
          output = "bool";
          break;
        case FunctionalNodeKind::Source:
          return std::nullopt;
      }
    }
    return output.empty() ? std::nullopt : std::optional<string>(output);
  }

  std::optional<string> generated_expr_type(
      const string& expression,
      const std::unordered_map<string,string>* types) const {
    string original = tree_strip_expression_grouping(expression);
    {
      string callee;
      vector<string> arguments;
      if (tree_parse_simple_call(original, callee, arguments) && callee == "replace" &&
          arguments.size() == 2)
        return generated_expr_type(arguments[0], types);
      if (tree_parse_simple_call(original, callee, arguments) && !functions_.count(callee)) {
        if (callee == "Char" && arguments.size() == 1) return string("char");
        if ((callee == "less" || callee == "isequal") && arguments.size() == 2)
          return string("bool");
        if (callee == "hash" && arguments.size() == 1) return string("int");
      }
    }
    string message_target, message_handler;
    vector<string> message_arguments;
    if (types && tree_parse_message(original, message_target, message_handler,
                                    message_arguments)) {
      string receiver = message_target;
      string handler = message_handler;
      vector<string> arguments = std::move(message_arguments);
      if (tree_plain_identifier(receiver)) {
        auto type = types->find(receiver);
        if (type != types->end()) {
          auto domain = domains_.find(type->second);
          if (domain != domains_.end())
            if (const Handler* target = find_handler(*domain->second, handler))
              return target->reply_type.value_or("unit");
        }
      }
      return std::nullopt;
    }
    if (auto pipeline = generated_functional_pipeline_type(original, types))
      return pipeline;
    string value = tree_strip_expression_grouping(tree_normalize_pipeline(std::move(original)));
    {
      string enum_name, case_name;
      vector<string> fields;
      bool called = false;
      if (tree_parse_enum_case_expression(value, enum_name, case_name, fields, called) &&
          enums_.count(enum_name))
        return enum_name;
    }
    if (value == "true" || value == "false") return string("bool");
    if (is_single_char_literal(value)) return string("char");
    if (value == "None") return string("_none");
    if (is_single_string_literal(value))
      return string("string");
    if (types) {
      auto local = types->find(value);
      if (local != types->end() && !local->second.empty() &&
          !contains_internal_placeholder(local->second))
        return canonical_type_name(local->second);
    }
    string unary_operation, unary_operand;
    if (tree_parse_unary(value, unary_operation, unary_operand) &&
        unary_operation == "not") {
      auto operand = generated_expr_type(unary_operand, types);
      return operand && canonical_type_name(*operand) == "bool"
          ? std::optional<string>("bool") : std::nullopt;
    }
    if (tree_plain_identifier(value) && functions_.count(value))
      return "callable:" + value;
    if (auto typed_vector = typed_empty_vector_constructor(value)) return *typed_vector;
    if (generated_integer_literal(value))
      return string("int");
    if (decimal_float_literal(value)) return string("float");
    if (auto elements = tree_parse_vector_literal(value)) {
      if (elements->empty()) return string("vector");
      string element_type;
      for (const auto& element : *elements) {
        auto current = generated_expr_type(element, types);
        if (!current) return std::nullopt;
        string concrete = canonical_type_name(*current);
        if (element_type.empty()) element_type = concrete;
        else if (element_type != concrete) return std::nullopt;
      }
      return "vector[" + element_type + "]";
    }

    string receiver, method_name;
    vector<string> method_args;
    if (tree_parse_member_call(value, receiver, method_name, method_args)) {
      if (staged_abi_v10() && tree_empty_constructor_named(receiver, "Map") &&
          method_name == "get" && method_args.size() == 2)
        return generated_expr_type(method_args[1], types);
      string canon_recv = canonical_type_name(receiver);
      if (canon_recv == "int" || canon_recv == "float" || canon_recv == "char" ||
          canon_recv == "string" || canon_recv == "bool") {
        if ((method_name == "less" || method_name == "isequal") && method_args.size() == 2)
          return string("bool");
        if (method_name == "hash" && method_args.size() == 1)
          return string("int");
        if (method_name == "to_string" && method_args.size() == 1)
          return string("string");
        if (canon_recv == "char" && method_name == "codepoint" && method_args.size() == 1)
          return string("int");
      }
      auto receiver_type = generated_expr_type(receiver, types);
      if (receiver_type) {
        string concrete_receiver = canonical_type_name(*receiver_type);
        // Phase 20: Executor configuration/start chain and FileIO operations;
        // mirrors the checker's inferred_expr_type handling.
        if (concrete_receiver == "executor" &&
            (executor_config_methods().count(method_name) || method_name == "start"))
          return string("executor");
        if (concrete_receiver == "FileIO") {
          if (method_name == "read" && method_args.size() == 2) return string("Range");
          if (method_name == "read" && method_args.size() == 1) return string("RangeBatch");
          if (method_name == "chunks" && method_args.size() == 1) return string("seq[Range]");
          if ((method_name == "write" && method_args.size() == 2) ||
              (method_name == "sync" && method_args.size() <= 1) ||
              (method_name == "close" && method_args.empty()))
            return string("unit");
        }
        if (concrete_receiver == "Range" && method_name == "length" && method_args.empty())
          return string("int");
        if (concrete_receiver == "Range" && method_name == "slice" && method_args.size() == 2)
          return string("Range");
        if (auto result = builtin_operation_result(concrete_receiver, method_name,
                                                    method_args.size()))
          return result;
        vector<string> argument_types;
        for (const auto& argument : method_args)
          argument_types.push_back(nominalized_generated_argument_type(
              argument, generated_expr_type(argument, types).value_or("")));
        auto match = resolve_object_method(objects_, *receiver_type, method_name,
                                           argument_types, false, nullptr);
        if (match && match->return_type)
          return canonical_type_name(*match->return_type);
      }
    }

    string callee;
    vector<string> call_args;
    if (tree_parse_simple_call(value, callee, call_args)) {
      if (objects_.count(callee)) {
        const ObjectType* obj = objects_.at(callee);
        bool has_generic_field = std::any_of(obj->fields.begin(), obj->fields.end(),
            [](const Field& f) { return f.inferred || f.type.empty(); });
        if (!has_generic_field) return callee;
        std::map<string, string> supplied_types;
        for (const auto& arg : call_args) {
          string fn, fv;
          if (tree_parse_named_argument(arg, fn, fv)) {
            auto ft = generated_expr_type(fv, types);
            if (ft) supplied_types[fn] = canonical_type_name(*ft);
          }
        }
        string instance_type = callee + "[";
        bool first = true;
        for (const auto& f : obj->fields) {
          if (!f.inferred && !f.type.empty()) continue;
          if (!first) instance_type += ",";
          first = false;
          auto it = supplied_types.find(f.name);
          string ft = (it != supplied_types.end()) ? it->second : (f.type.empty() ? "_" : canonical_type_name(f.type));
          instance_type += f.name + "=" + ft;
        }
        instance_type += "]";
        return instance_type;
      }
      if (callee == "sqrt" && call_args.size() == 1) {
        auto argument = generated_expr_type(call_args.front(), types);
        return argument && canonical_type_name(*argument) == "float"
            ? std::optional<string>("float") : std::nullopt;
      }
      if (callee == "Some" && call_args.size() == 1) {
        auto value_type = generated_expr_type(call_args.front(), types);
        return value_type ? "option[" + *value_type + "]" : "option[_]";
      }
      if (callee == "range" && (call_args.size() == 2 || call_args.size() == 3))
        return "range[int]";
      string callee_id = resolved_callable_identity(callee, types);
      const Function* function_target = nullptr;
      if (starts_with(callee_id, "callable-v1:")) {
        function_target = find_function_by_callable_id(callee_id);
      } else {
        auto function = functions_.find(callee_id);
        if (function != functions_.end()) function_target = function->second;
      }
      if (function_target && function_target->return_type) {
        vector<string> argument_types;
        for (const auto& argument : call_args)
          argument_types.push_back(generated_expr_type(argument, types).value_or(""));
        if (function_target->static_dispatch) {
          const auto* specialization = call_specialization(
              *function_target, call_args, types);
          if (specialization)
            return specialization->return_type;
          return std::nullopt;
        }
        string result = canonical_type_name(*function_target->return_type);
        if (open_generic(result)) {
          string parameter = result.substr(9);
          for (size_t index = 0; index < function_target->params.size(); ++index)
            if (function_target->params[index].name == parameter &&
                index < argument_types.size() && !argument_types[index].empty())
              return argument_types[index];
          return std::nullopt;
        }
        if (!contains_internal_placeholder(result)) return result;
      }
    }

    string member_base, member_name;
    if (tree_parse_member_access(value, member_base, member_name)) {
      auto base = generated_expr_type(member_base, types);
      if (base) {
        string tname = nominal_template_name(*base);
        auto object = objects_.find(tname);
        if (object != objects_.end()) {
          string field_name = member_name;
          if (auto sub_type = nominal_field_type(*base, field_name)) {
            return canonical_type_name(*sub_type);
          }
          for (const auto& field : object->second->fields)
            if (field.name == field_name)
              return canonical_type_name(field.type);
        }
      }
    }
    string index_base, index_expr;
    if (tree_parse_index(value, index_base, index_expr)) {
      auto base_type = generated_expr_type(index_base, types);
      if (base_type) {
        string concrete = canonical_type_name(*base_type);
        if (concrete == "RangeBatch") return string("Range");
        if (auto element = indexed_element(parsed_type(concrete)))
          return element;
      }
    }
    if (tree_generated_binary(value, {" or "}) ||
        tree_generated_binary(value, {" xor "}) ||
        tree_generated_binary(value, {" and "}) ||
        tree_generated_binary(value, {"==", "!=", "<=", ">=", "<", ">"}))
      return string("bool");
    for (const auto& ops : vector<vector<string>>{{"|"}, {"^"}, {"&"}, {"<<", ">>"}}) {
      if (auto binary = tree_generated_binary(value, ops)) {
        auto left = generated_expr_type(binary->left, types);
        auto right = generated_expr_type(binary->right, types);
        if (left && right &&
            canonical_type_name(*left) == "int" &&
            canonical_type_name(*right) == "int")
          return string("int");
      }
    }
    for (const auto& operators : vector<vector<string>>{{"+", "-"}, {"*", "/", "%"}}) {
      auto binary = tree_generated_binary(value, operators);
      if (!binary) continue;
      auto left = generated_expr_type(binary->left, types);
      auto right = generated_expr_type(binary->right, types);
      if (left && right) {
        if (*left == "string" && *right == "string" && binary->op == "+")
          return string("string");
        if ((*left == "int" || *left == "float") &&
            (*right == "int" || *right == "float"))
          return (*left == "float" || *right == "float") ? "float" : "int";
      }
    }
    if (tree_parse_unary(value, unary_operation, unary_operand) &&
        (unary_operation == "-" || unary_operation == "+")) {
      auto operand = generated_expr_type(unary_operand, types);
      if (operand && (canonical_type_name(*operand) == "int" ||
                      canonical_type_name(*operand) == "float"))
        return operand;
    }
    if (tree_parse_unary(value, unary_operation, unary_operand) &&
        unary_operation == "~") {
      auto operand = generated_expr_type(unary_operand, types);
      if (operand && canonical_type_name(*operand) == "int")
        return string("int");
    }
    return std::nullopt;
  }

  static string generated_type_list(const vector<string>& types) {
    std::ostringstream out;
    for (size_t index = 0; index < types.size(); ++index) {
      if (index) out << ", ";
      out << (types[index].empty() ? "unresolved" : types[index]);
    }
    return out.str();
  }

  bool is_pure_state_read_expression(const string& expression) const {
    string value = trim(expression);
    // This prelude is solely a Rust evaluation-order repair.  Do not move a
    // source call or message across an earlier WRITE argument.
    string target, handler;
    vector<string> message_arguments;
    if (tree_parse_message(value, target, handler, message_arguments)) return false;
    string callee, receiver, method;
    vector<string> arguments;
    return !tree_parse_simple_call(value, callee, arguments) &&
        !tree_parse_member_call(value, receiver, method, arguments);
  }

  string render_known_function_call(
      const Function& function, const string& name, const vector<string>& arguments,
      const Domain* domain, const std::set<string>& locals,
      const std::unordered_map<string,string>* types) const {
    const auto* specialization = call_specialization(function, arguments, types);
    const auto external_module = function.static_dispatch ? std::nullopt :
        phase21_external_module_for_symbol(name);
    const bool cross_crate = external_module.has_value();
    const bool standard_write_bridge = cross_crate &&
        std::any_of(function.params.begin(), function.params.end(),
            [&](const Param& parameter) {
              const size_t index = static_cast<size_t>(
                  &parameter - function.params.data());
              return !standard_error_variants(canonical_type_name(
                  parameter.type)).empty() &&
                  function_parameter_abi(function, index, parameter.type,
                      specialization).effect == Effect::Write;
            });
    std::ostringstream bridge_before, bridge_after;
    vector<string> temporaries(arguments.size());
    if (domain) {
      for (size_t write_index = 0;
           write_index < arguments.size() && write_index < function.params.size();
           ++write_index) {
        if (function_effect(function, write_index,
                generated_expr_type(arguments[write_index], types).value_or(""), specialization)
            != Effect::Write) continue;
        string written = trim(arguments[write_index]);
        bool writes_direct_field = !locals.count(written) &&
            std::any_of(domain->state.begin(), domain->state.end(),
                        [&](const Field& field) { return field.name == written; });
        if (!writes_direct_field) continue;
        for (size_t read_index = write_index + 1;
             read_index < arguments.size() && read_index < function.params.size();
             ++read_index) {
          if (!is_pure_state_read_expression(arguments[read_index]))
            continue;
          auto argument_type = generated_expr_type(arguments[read_index], types);
          if (!argument_type || !copy_type(*argument_type))
            continue;
          bool reads_sibling = false;
          for (const auto& field : domain->state) {
            if (field.name != written &&
                tree_expression_mentions_identifier(arguments[read_index], field.name)) {
              reads_sibling = true;
              break;
            }
          }
          if (reads_sibling)
            temporaries[read_index] = "__moss_call_argument_" +
                std::to_string(call_argument_temp_++);
        }
      }
    }

    std::ostringstream call;
    call << viewed_function_name(name, arguments, domain, locals, types) << "(";
    auto nested_occurrence = [&](size_t index) {
      uint32_t occurrence = 0;
      for (size_t prior = 0; prior < index; ++prior)
        if (tree_expressions_equal(arguments[prior], arguments[index])) ++occurrence;
      return occurrence;
    };
    struct NestedArgumentScope {
      std::optional<uint32_t>& slot;
      std::optional<uint32_t> previous;
      NestedArgumentScope(std::optional<uint32_t>& target, uint32_t occurrence)
          : slot(target), previous(target) { slot = occurrence; }
      ~NestedArgumentScope() { slot = previous; }
    };
    size_t emitted_arguments = 0;
    for (size_t index = 0; index < arguments.size(); ++index) {
      NestedArgumentScope nested_scope(codegen_nested_argument_occurrence_,
                                       nested_occurrence(index));
      bool compile_time_callable = is_compile_time_callable(
          generated_expr_type(arguments[index], types).value_or(""));
      if (compile_time_callable) continue;
      if (emitted_arguments++) call << ", ";
      {
        string rendered = temporaries[index].empty()
            ? function_call_argument(function, index, arguments[index], domain,
                                     locals, types, 0, specialization)
            : temporaries[index];
        if (cross_crate && index < function.params.size() &&
            !standard_error_variants(
                canonical_type_name(function.params[index].type)).empty()) {
          const auto abi = function_parameter_abi(function, index,
              generated_expr_type(arguments[index], types).value_or(""),
              specialization);
          const string external_type = tooling_name("moss_" +
              *external_module) + "::" +
              canonical_type_name(function.params[index].type);
          if (abi.effect == Effect::Write) {
            const string prefix = "__moss_standard_write_" +
                std::to_string(call_argument_temp_++);
            const string local_ref = prefix + "_local";
            const string provider_value = prefix + "_provider";
            bridge_before << "let " << local_ref << " = " << rendered
              << "; let mut " << provider_value << " = " << external_type
              << "::from((*" << local_ref << ").clone()); ";
            bridge_after << "*" << local_ref << " = "
              << canonical_type_name(function.params[index].type)
              << "::from(" << provider_value << "); ";
            rendered = "&mut " + provider_value;
          } else
            rendered = abi.passing == ParameterPassingMode::Value
                ? external_type + "::from(" + rendered + ")"
                : "&(" + external_type +
                    "::from((* (" + rendered + ")).clone()))";
        }
        if (standard_write_bridge &&
            !(cross_crate && index < function.params.size() &&
              !standard_error_variants(canonical_type_name(
                  function.params[index].type)).empty() &&
              function_parameter_abi(function, index,
                  generated_expr_type(arguments[index], types).value_or(""),
                  specialization).effect == Effect::Write)) {
          const string staged = "__moss_call_argument_" +
              std::to_string(call_argument_temp_++);
          bridge_before << "let " << staged << " = " << rendered << "; ";
          rendered = staged;
        }
        call << rendered;
      }
    }
    call << ")";
    if (!bridge_before.str().empty()) {
      const string native_call = call.str();
      call.str("");
      call.clear();
      call << "{ " << bridge_before.str() << "let __moss_bridge_result = "
        << native_call << "; " << bridge_after.str()
        << "__moss_bridge_result }";
    }
    bool needs_prelude = std::any_of(
        temporaries.begin(), temporaries.end(),
        [](const string& temporary) { return !temporary.empty(); });
    const bool raises = !(specialization ? specialization->observable_effects
                                       : function.observable_effects).raise_set.empty();
    const string result_type = specialization ? specialization->return_type :
        function.return_type.value_or("");
    const bool convert_result = cross_crate &&
        !standard_error_variants(canonical_type_name(result_type)).empty();
    if (convert_result) {
      const string rendered = call.str();
      call.str("");
      call.clear();
      call << (raises ? "(" + rendered + ").map(" + result_type + "::from)"
                      : result_type + "::from(" + rendered + ")");
    }
    if (raises) {
      if (!phase21_result_context_)
        throw std::runtime_error(
            "internal error: checked raising function call lacks a Phase 21 outcome context");
      const string propagated = phase21_propagate(call.str());
      call.str("");
      call.clear();
      call << propagated;
    }
    if (!needs_prelude) return call.str();

    std::ostringstream lowered;
    lowered << "{ ";
    for (size_t index = 0; index < arguments.size(); ++index)
      if (!temporaries[index].empty()) {
        NestedArgumentScope nested_scope(codegen_nested_argument_occurrence_,
                                         nested_occurrence(index));
        lowered << "let " << temporaries[index] << " = "
                << expr(arguments[index], domain, locals, types) << "; ";
      }
    lowered << call.str() << " }";
    return lowered.str();
  }

  string emitted_function_name(
      const string& name, const vector<string>& arguments,
      const std::unordered_map<string,string>* types) const {
    const Function* fn = nullptr;
    if (starts_with(name, "callable-v1:")) {
      fn = find_function_by_callable_id(name);
    } else {
      auto function = functions_.find(name);
      if (function != functions_.end()) fn = function->second;
    }
    if (!fn || !fn->static_dispatch)
      return fn ? fn->name : name;
    const auto* specialization = call_specialization(*fn, arguments, types);
    if (!specialization)
      throw std::runtime_error("missing static specialization for function '" + (fn ? fn->name : name) +
                               "' at its checked call site");
    return specialization->generated_name;
  }

  struct FunctionalValue {
    string expression;
    string type;
    bool reference = false;
  };

  const FunctionalPipeline* planned_functional_pipeline(
      size_t functional_pipeline_id) const {
    if (functional_pipeline_id == 0) return nullptr;
    auto planned = std::find_if(
        p_.functional_pipelines.begin(), p_.functional_pipelines.end(),
        [&](const FunctionalPipeline& candidate) {
          return candidate.transient_id == functional_pipeline_id;
        });
    if (planned == p_.functional_pipelines.end())
      throw std::runtime_error(
          "internal error: checked functional_pipeline_id has no IR plan");
    return &*planned;
  }

  const FunctionalTraversalGroup* functional_traversal_group(
      size_t group_id) const {
    if (!group_id) return nullptr;
    auto found = std::find_if(
        p_.functional_traversal_groups.begin(),
        p_.functional_traversal_groups.end(),
        [&](const FunctionalTraversalGroup& group) {
          return group.transient_id == group_id;
        });
    if (found == p_.functional_traversal_groups.end())
      throw std::runtime_error(
          "internal error: functional traversal group has no IR plan");
    return &*found;
  }

  static LoweredFunctionalPipeline lower_functional_semantic_plan(
      const ParsedFunctionalPipeline& parsed,
      const FunctionalPipeline& planned) {
    LoweredFunctionalPipeline lowered;
    lowered.source = parsed.source;
    for (const auto& semantic_step : planned.semantic_steps) {
      if (semantic_step.kind == FunctionalNodeKind::Source ||
          semantic_step.source_node_indices.empty())
        throw std::runtime_error(
            "internal error: invalid functional semantic step");
      LoweredFunctionalStage stage;
      stage.kind = semantic_step.kind;
      for (size_t node_index : semantic_step.source_node_indices) {
        if (node_index == 0 || node_index > parsed.stages.size() ||
            node_index >= planned.nodes.size())
          throw std::runtime_error(
              "internal error: functional semantic step has stale source node");
        const ParsedFunctionalStage& operation =
            parsed.stages[node_index - 1];
        if (operation.kind != semantic_step.kind ||
            planned.nodes[node_index].kind != semantic_step.kind)
          throw std::runtime_error(
              "internal error: functional semantic rewrite changed operator kind");
        stage.operations.push_back(operation);
      }
      lowered.stages.push_back(std::move(stage));
    }
    return lowered;
  }

  static const ParsedFunctionalStage& terminal_operation(
      const LoweredFunctionalPipeline& pipeline) {
    if (pipeline.stages.empty() ||
        pipeline.stages.back().operations.size() != 1 ||
        !functional_terminal_kind(pipeline.stages.back().kind))
      throw std::runtime_error(
          "internal error: lowered functional pipeline has no terminal");
    return pipeline.stages.back().operations.front();
  }

  string render_functional_callable(
      const string& callable, const vector<FunctionalValue>& arguments,
      const Domain* domain, const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      bool propagate_raises = true,
      bool checked_placeholder_result = false) const {
    if (generated_expression_uses(callable, "_")) {
      if (arguments.size() != 1)
        throw std::runtime_error("internal error: placeholder functional arity");
      string replaced = replace_unqualified_word(callable, "_",
                                                 arguments.front().expression);
      std::unordered_map<string,string> callback_types = types
          ? *types : std::unordered_map<string,string>{};
      callback_types[arguments.front().expression] = arguments.front().type;
      auto callback_locals = locals;
      callback_locals.insert(arguments.front().expression);
      // A checked chunk mapper runs in its own Rust callback result. Its
      // expression must propagate into that result, then the surrounding
      // chunk lowering decides whether to commit the indexed error. In
      // particular, a lexical try label or Root capture exit outside this
      // callback is not reachable from inside the generated closure.
      if (checked_placeholder_result && !propagate_raises) {
        Generator callback = *this;
        callback.phase21_try_label_.clear();
        callback.phase21_capture_handler_ = nullptr;
        string rendered = callback.expr(replaced, domain, callback_locals,
                                        &callback_types);
        return "(|| { let __moss_callback_value = " + rendered +
            "; Ok::<_, __MossRaised>(__moss_callback_value) })()";
      }
      return expr(replaced, domain, callback_locals, &callback_types);
    }

    string bound_receiver, bound_method;
    if (tree_parse_bound_method_callable(callable, bound_receiver, bound_method)) {
      auto receiver_type = generated_expr_type(bound_receiver, types);
      vector<string> argument_types;
      for (const auto& argument : arguments)
        argument_types.push_back(argument.type);
      const Method* method = receiver_type
          ? resolve_object_method(objects_, canonical_type_name(*receiver_type),
                                  bound_method, argument_types, false, nullptr)
          : nullptr;
      if (!method)
        throw std::runtime_error(
            "missing statically resolved functional bound method '" +
            trim(callable) + "'");
      std::ostringstream rendered;
      rendered << expr(bound_receiver, domain, locals, types) << "."
               << bound_method << "(";
      for (size_t index = 0; index < arguments.size(); ++index) {
        if (index) rendered << ", ";
        const auto& argument = arguments[index];
        Effect effect = method_effect(*method, index);
        bool borrow = effect != Effect::Consume &&
            (effect == Effect::Write || borrowable_type(argument.type));
        if (domains_.count(canonical_type_name(argument.type))) {
          if (argument.reference)
            rendered << "(*" << argument.expression << ").clone()";
          else
            rendered << argument.expression;
        } else if (borrow && !argument.reference)
          rendered << (effect == Effect::Write ? "&mut (" : "&(")
                   << argument.expression << ")";
        else
          rendered << argument.expression;
      }
      rendered << ")";
      if (!method->observable_effects.raise_set.empty()) {
        if (!phase21_result_context_)
          throw std::runtime_error(
              "checked raising pipeline method lacks a Phase 21 outcome context");
        return propagate_raises ? phase21_propagate(rendered.str())
                                : rendered.str();
      }
      return rendered.str();
    }

    string identity = resolved_callable_identity(callable, types);
    const Function* function_target = nullptr;
    if (starts_with(identity, "callable-v1:")) {
      function_target = find_function_by_callable_id(identity);
    } else {
      auto function = functions_.find(identity);
      if (function != functions_.end()) function_target = function->second;
    }
    if (!function_target)
      throw std::runtime_error("missing statically resolved functional callable '" +
                               identity + "'");
    string emitted = function_target->name;
    vector<string> argument_types;
    for (const auto& argument : arguments) argument_types.push_back(argument.type);
    const FunctionSpecialization* concrete = nullptr;
    if (function_target->static_dispatch) {
      // Functional callbacks register the key with no receiver or open-result
      // binding. Select that exact checked key even when a separate ordinary
      // call of the same callable and arguments has a bound result key.
      concrete = functional_specialization(*function_target, argument_types);
      if (!concrete)
        throw std::runtime_error("missing functional specialization for '" +
                                 identity + "'");
      emitted = concrete->generated_name;
    } else concrete = concrete_specialization(*function_target, argument_types);
    std::ostringstream rendered;
    rendered << emitted << "(";
    for (size_t index = 0; index < arguments.size(); ++index) {
      if (index) rendered << ", ";
      const auto& argument = arguments[index];
      auto abi = function_parameter_abi(*function_target, index, argument.type, concrete);
      Effect effect = abi.effect;
      bool borrow = abi.passing != ParameterPassingMode::Value;
      if (domains_.count(canonical_type_name(argument.type))) {
        if (argument.reference)
          rendered << "(*" << argument.expression << ").clone()";
        else
          rendered << argument.expression;
      } else if (borrow && !argument.reference)
        rendered << (effect == Effect::Write ? "&mut (" : "&(")
                 << argument.expression << ")";
      else
        rendered << argument.expression;
    }
    rendered << ")";
    const bool raises = !(concrete ? concrete->observable_effects
                                  : function_target->observable_effects)
                             .raise_set.empty();
    if (raises) {
      if (!phase21_result_context_)
        throw std::runtime_error(
            "checked raising pipeline callable lacks a Phase 21 outcome context");
      return propagate_raises ? phase21_propagate(rendered.str())
                              : rendered.str();
    }
    return rendered.str();
  }

  static string functional_zero(const string& type) {
    return canonical_type_name(type) == "float" ? "0.0_f64" : "0_i64";
  }

  string functional_terminal_initializer(
      const ParsedFunctionalStage& terminal, const string& element_type,
      const Domain* domain, const std::set<string>& locals,
      const std::unordered_map<string,string>* types) const {
    switch (terminal.kind) {
      case FunctionalNodeKind::Reduce:
        return expr(terminal.arguments.front(), domain, locals, types);
      case FunctionalNodeKind::Sum:
        return functional_zero(element_type);
      case FunctionalNodeKind::Count:
        return "0_i64";
      case FunctionalNodeKind::Any:
        return "false";
      case FunctionalNodeKind::All:
        return "true";
      case FunctionalNodeKind::Source:
      case FunctionalNodeKind::Map:
      case FunctionalNodeKind::Filter:
        break;
    }
    throw std::runtime_error(
        "internal error: non-terminal functional traversal consumer");
  }

  void gen_shared_functional_traversal(
      std::ostringstream& out, const FunctionalTraversalGroup& group,
      const vector<Stmt>& statements, size_t statement_index,
      const Domain* domain, std::set<string>& locals,
      std::unordered_map<string,string>& types, int spaces,
      const string& context) const {
    if (group.consumers.size() < 2 ||
        statement_index + group.consumers.size() > statements.size())
      throw std::runtime_error(
          "internal error: invalid shared functional traversal region");

    vector<const FunctionalPipeline*> plans;
    vector<LoweredFunctionalPipeline> pipelines;
    vector<string> final_element_types;
    plans.reserve(group.consumers.size());
    pipelines.reserve(group.consumers.size());
    final_element_types.reserve(group.consumers.size());
    for (size_t index = 0; index < group.consumers.size(); ++index) {
      const auto& consumer = group.consumers[index];
      const Stmt& statement = statements[statement_index + index];
      size_t exact_id = statement_functional_pipeline_id(statement, context, 0);
      const auto result_binding = tree_identifier_name(statement.a);
      if (exact_id != consumer.pipeline_id || !result_binding ||
          *result_binding != consumer.result_binding)
        throw std::runtime_error(
            "internal error: stale shared functional traversal region");
      const FunctionalPipeline* plan = planned_functional_pipeline(exact_id);
      auto parsed = functional_pipeline_from_est(
          statement.b, &statement.expression_handles);
      if (!plan || !parsed || parsed->stages.empty() ||
          !functional_terminal_kind(parsed->stages.back().kind))
        throw std::runtime_error(
            "internal error: invalid shared functional consumer");
      string element_type = generated_functional_element_type(
          group.source_type).value_or("");
      if (element_type.empty())
        throw std::runtime_error(
            "internal error: untyped shared functional source");
      LoweredFunctionalPipeline lowered =
          lower_functional_semantic_plan(*parsed, *plan);
      if (lowered.stages.empty() ||
          !functional_terminal_kind(lowered.stages.back().kind))
        throw std::runtime_error(
            "internal error: rewritten shared functional consumer lost terminal");
      for (const auto& stage : lowered.stages) {
        if (stage.kind != FunctionalNodeKind::Map) continue;
        for (const auto& operation : stage.operations) {
          auto result = generated_callable_result(
              operation.arguments.front(), {element_type}, &types);
          if (!result)
            throw std::runtime_error(
                "internal error: untyped shared functional map");
          element_type = *result;
        }
      }
      plans.push_back(plan);
      pipelines.push_back(std::move(lowered));
      final_element_types.push_back(std::move(element_type));
    }

    for (size_t index = 1; index < group.consumers.size(); ++index)
      source_comment(out, spaces, statements[statement_index + index].line,
                     statements[statement_index + index].text);
    backend_comment(
        out, spaces,
        "SHARED FUNCTIONAL SOURCE TRAVERSAL (" +
            std::to_string(group.consumers.size()) +
            " terminal consumers); one dataflow DAG loop");
    out << string(spaces, ' ') << "let (";
    for (size_t index = 0; index < group.consumers.size(); ++index) {
      if (index) out << ", ";
      if (group.consumers[index].mutable_binding) out << "mut ";
      out << group.consumers[index].result_binding;
    }
    out << ") = {\n";
    string inner(spaces + 4, ' ');
    string deep(spaces + 8, ' ');
    out << inner << "// Moss dataflow group %" << group.transient_id
        << ", semantic " << group.semantic_identity << "; provenance";
    for (const auto& origin : group.provenance) out << " " << origin;
    out << "\n";
    out << inner << "let __moss_shared_source_" << group.transient_id
        << " = &(" << expr(group.source_expression, domain, locals, &types)
        << ");\n";

    for (size_t index = 0; index < group.consumers.size(); ++index) {
      const auto& terminal = terminal_operation(pipelines[index]);
      out << inner << "let mut __moss_shared_result_" << group.transient_id
          << "_" << index << " = ";
      if (plans[index]->count_uses_exact_length)
        out << "__moss_shared_source_" << group.transient_id
            << ".len() as i64";
      else
        out << functional_terminal_initializer(
            terminal, final_element_types[index], domain, locals, &types);
      out << ";\n";
    }

    bool has_traversal = std::any_of(
        plans.begin(), plans.end(), [](const FunctionalPipeline* plan) {
          return !plan->count_uses_exact_length;
        });
    auto source_element = generated_functional_element_type(group.source_type);
    if (!source_element)
      throw std::runtime_error(
          "internal error: untyped shared functional source element");
    if (has_traversal) {
      out << inner << "for __moss_shared_item_ref_" << group.transient_id
          << " in __moss_shared_source_" << group.transient_id
          << ".iter() {\n";
      for (size_t consumer_index = 0;
           consumer_index < group.consumers.size(); ++consumer_index) {
        if (plans[consumer_index]->count_uses_exact_length) continue;
        const auto& pipeline = pipelines[consumer_index];
        const auto& terminal = terminal_operation(pipeline);
        string result = "__moss_shared_result_" +
            std::to_string(group.transient_id) + "_" +
            std::to_string(consumer_index);
        // A completed any/all branch may stop receiving elements only when
        // the semantic plan proves every skipped callback non-observable,
        // non-failing, and non-divergent. Otherwise the shared loop continues
        // invoking that branch exactly as eager execution would.
        bool guarded_any = terminal.kind == FunctionalNodeKind::Any &&
            plans[consumer_index]->short_circuit_terminal;
        bool guarded_all = terminal.kind == FunctionalNodeKind::All &&
            plans[consumer_index]->short_circuit_terminal;
        if (guarded_any || guarded_all)
          out << deep << "if " << (guarded_any ? "!" : "") << result
              << " {\n";
        string block_indent(spaces + (guarded_any || guarded_all ? 16 : 12),
                            ' ');
        string statement_indent(spaces +
            (guarded_any || guarded_all ? 20 : 16), ' ');
        string label = "'__moss_consumer_" +
            std::to_string(group.transient_id) + "_" +
            std::to_string(consumer_index);
        out << block_indent << label << ": {\n";
        string current_expression = "__moss_shared_value_" +
            std::to_string(group.transient_id) + "_" +
            std::to_string(consumer_index) + "_0";
        bool source_copy = copy_type(*source_element);
        out << statement_indent << "let " << current_expression << " = "
            << (source_copy ? "*" : "") << "__moss_shared_item_ref_"
            << group.transient_id << ";\n";
        FunctionalValue current{current_expression, *source_element,
                                !source_copy};
        size_t map_number = 0;
        for (const auto& stage : pipeline.stages) {
          switch (stage.kind) {
            case FunctionalNodeKind::Map: {
              for (const auto& operation : stage.operations) {
                string next = "__moss_shared_value_" +
                    std::to_string(group.transient_id) + "_" +
                    std::to_string(consumer_index) + "_" +
                    std::to_string(++map_number);
                out << statement_indent << "let " << next << " = "
                    << render_functional_callable(
                           operation.arguments.front(), {current}, domain,
                           locals, &types)
                    << ";\n";
                auto mapped_type = generated_callable_result(
                    operation.arguments.front(), {current.type}, &types);
                if (!mapped_type)
                  throw std::runtime_error(
                      "internal error: untyped shared functional map result");
                current = {next, *mapped_type, false};
              }
              break;
            }
            case FunctionalNodeKind::Filter: {
              out << statement_indent << "if !(";
              for (size_t predicate = 0;
                   predicate < stage.operations.size(); ++predicate) {
                if (predicate) out << " && ";
                out << "("
                    << render_functional_callable(
                           stage.operations[predicate].arguments.front(),
                           {current}, domain, locals, &types)
                    << ")";
              }
              out << ") { break " << label << "; }\n";
              break;
            }
            case FunctionalNodeKind::Reduce: {
              const auto& operation = stage.operations.front();
              auto accumulator_type = generated_expr_type(
                  operation.arguments.front(), &types).value_or(current.type);
              out << statement_indent << result << " = "
                  << render_functional_callable(
                         operation.arguments[1],
                         {{result, accumulator_type, false}, current},
                         domain, locals, &types)
                  << ";\n";
              break;
            }
            case FunctionalNodeKind::Sum:
              if (canonical_type_name(current.type) == "int")
                out << statement_indent << result << " = " << result
                    << ".wrapping_add(" << current.expression << ");\n";
              else
                out << statement_indent << result << " += "
                    << current.expression << ";\n";
              break;
            case FunctionalNodeKind::Count:
              out << statement_indent << result << " = " << result
                  << ".wrapping_add(1_i64);\n";
              break;
            case FunctionalNodeKind::Any: {
              const auto& operation = stage.operations.front();
              string predicate = operation.arguments.empty()
                  ? current.expression
                  : render_functional_callable(
                        operation.arguments.front(), {current}, domain,
                        locals, &types);
              out << statement_indent << "if " << predicate << " { "
                  << result << " = true; }\n";
              break;
            }
            case FunctionalNodeKind::All: {
              const auto& operation = stage.operations.front();
              string predicate = operation.arguments.empty()
                  ? current.expression
                  : render_functional_callable(
                        operation.arguments.front(), {current}, domain,
                        locals, &types);
              out << statement_indent << "if !(" << predicate << ") { "
                  << result << " = false; }\n";
              break;
            }
            case FunctionalNodeKind::Source:
              break;
          }
        }
        out << block_indent << "}\n";
        if (guarded_any || guarded_all) out << deep << "}\n";
      }
      out << inner << "}\n";
    }
    out << inner << "(";
    for (size_t index = 0; index < group.consumers.size(); ++index) {
      if (index) out << ", ";
      out << "__moss_shared_result_" << group.transient_id << "_" << index;
    }
    out << ")\n" << string(spaces, ' ') << "};\n";

    for (size_t index = 0; index < group.consumers.size(); ++index) {
      const auto& consumer = group.consumers[index];
      locals.insert(consumer.result_binding);
      types[consumer.result_binding] = plans[index]->output_type;
    }
  }

  string gen_eager_functional_pipeline(
      const ParsedFunctionalPipeline& pipeline, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      const FunctionalPipeline* planned) const {
    auto source_type = generated_expr_type(pipeline.source, types);
    auto element = source_type
        ? generated_functional_element_type(*source_type) : std::nullopt;
    if (!element)
      throw std::runtime_error("internal error: untyped functional source");
    string current_type = *element;
    string current_collection = "__moss_pipeline_source";
    std::ostringstream out;
    out << "{\n        // Moss backend: EAGER FUNCTIONAL PIPELINE reference semantics";
    if (planned) {
      out << " (plan %" << planned->transient_id
          << ", semantic " << planned->semantic_identity << ")";
    }
    out << "\n";
    if (planned && !planned->nodes.empty())
      out << "        // Moss functional origin|"
          << planned->nodes.front().semantic_identity << "\n";
    out << "        let __moss_pipeline_source = &("
        << expr(pipeline.source, domain, locals, types) << ");\n";

    size_t stage_number = 0;
    for (size_t pipeline_stage = 0; pipeline_stage < pipeline.stages.size();
         ++pipeline_stage) {
      const auto& stage = pipeline.stages[pipeline_stage];
      if (stage.kind != FunctionalNodeKind::Map &&
          stage.kind != FunctionalNodeKind::Filter)
        continue;
      if (planned && pipeline_stage + 1 < planned->nodes.size())
        out << "        // Moss functional origin|"
            << planned->nodes[pipeline_stage + 1].semantic_identity << "\n";
      string output = "__moss_stage_" + std::to_string(stage_number);
      string item_ref = "__moss_item_ref_" + std::to_string(stage_number);
      string value = "__moss_value_" + std::to_string(stage_number);
      bool item_copy = copy_type(current_type);
      out << "        let mut " << output << " = std::vec::Vec::new();\n"
          << "        for " << item_ref << " in " << current_collection
          << ".iter() {\n"
          << "            let " << value << " = "
          << (item_copy ? "*" : "") << item_ref << ";\n";
      FunctionalValue input{value, current_type, !item_copy};
      if (stage.kind == FunctionalNodeKind::Map) {
        string mapped = render_functional_callable(
            stage.arguments.front(), {input}, domain, locals, types);
        out << "            " << output << ".push(" << mapped << ");\n";
        auto result = generated_callable_result(stage.arguments.front(),
                                                {current_type}, types);
        if (!result)
          throw std::runtime_error("internal error: untyped eager map result");
        current_type = *result;
      } else {
        string predicate = render_functional_callable(
            stage.arguments.front(), {input}, domain, locals, types);
        out << "            if " << predicate << " { " << output << ".push("
            << value << "); }\n";
      }
      out << "        }\n";
      current_collection = output;
      ++stage_number;
    }

    const auto& terminal = pipeline.stages.back();
    bool has_terminal = terminal.kind == FunctionalNodeKind::Reduce ||
        terminal.kind == FunctionalNodeKind::Sum ||
        terminal.kind == FunctionalNodeKind::Count ||
        terminal.kind == FunctionalNodeKind::Any ||
        terminal.kind == FunctionalNodeKind::All;
    if (!has_terminal) {
      out << "        " << current_collection << "\n    }";
      return out.str();
    }

    string accumulator = "__moss_result";
    if (planned && pipeline.stages.size() < planned->nodes.size())
      out << "        // Moss functional origin|"
          << planned->nodes[pipeline.stages.size()].semantic_identity << "\n";
    if (terminal.kind == FunctionalNodeKind::Reduce)
      out << "        let mut " << accumulator << " = "
          << expr(terminal.arguments.front(), domain, locals, types) << ";\n";
    else if (terminal.kind == FunctionalNodeKind::Sum)
      out << "        let mut " << accumulator << " = "
          << functional_zero(current_type) << ";\n";
    else if (terminal.kind == FunctionalNodeKind::Count)
      out << "        let mut " << accumulator << " = 0_i64;\n";
    else
      out << "        let mut " << accumulator << " = "
          << (terminal.kind == FunctionalNodeKind::All ? "true" : "false")
          << ";\n";

    string item_ref = "__moss_terminal_ref";
    string value = "__moss_terminal_value";
    bool item_copy = copy_type(current_type);
    out << "        for " << item_ref << " in " << current_collection
        << ".iter() {\n"
        << "            let " << value << " = "
        << (item_copy ? "*" : "") << item_ref << ";\n";
    FunctionalValue input{value, current_type, !item_copy};
    if (terminal.kind == FunctionalNodeKind::Reduce) {
      auto accumulator_type = generated_expr_type(terminal.arguments.front(), types)
          .value_or(current_type);
      out << "            " << accumulator << " = "
          << render_functional_callable(
                 terminal.arguments[1],
                 {{accumulator, accumulator_type, false}, input},
                 domain, locals, types)
          << ";\n";
    } else if (terminal.kind == FunctionalNodeKind::Sum) {
      if (canonical_type_name(current_type) == "int")
        out << "            " << accumulator << " = " << accumulator
            << ".wrapping_add(" << value << ");\n";
      else
        out << "            " << accumulator << " += " << value << ";\n";
    } else if (terminal.kind == FunctionalNodeKind::Count) {
      out << "            " << accumulator << " = " << accumulator
          << ".wrapping_add(1_i64);\n";
    } else {
      string predicate = terminal.arguments.empty()
          ? value
          : render_functional_callable(terminal.arguments.front(), {input},
                                       domain, locals, types);
      if (terminal.kind == FunctionalNodeKind::Any)
        out << "            if " << predicate << " { " << accumulator
            << " = true; }\n";
      else
        out << "            if !(" << predicate << ") { " << accumulator
            << " = false; }\n";
    }
    out << "        }\n        " << accumulator << "\n    }";
    return out.str();
  }

  string gen_fused_functional_pipeline(
      const LoweredFunctionalPipeline& pipeline, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      const FunctionalPipeline* planned) const {
    auto source_type = generated_expr_type(pipeline.source, types);
    auto element = source_type
        ? generated_functional_element_type(*source_type) : std::nullopt;
    if (!element)
      throw std::runtime_error("internal error: untyped fused functional source");
    string current_type = *element;
    string final_element_type = current_type;
    for (const auto& stage : pipeline.stages) {
      if (stage.kind != FunctionalNodeKind::Map) continue;
      for (const auto& operation : stage.operations) {
        auto result = generated_callable_result(
            operation.arguments.front(), {final_element_type}, types);
        if (!result)
          throw std::runtime_error("internal error: untyped fused map result");
        final_element_type = *result;
      }
    }
    FunctionalNodeKind last_kind = pipeline.stages.empty()
        ? FunctionalNodeKind::Source : pipeline.stages.back().kind;
    bool terminal = functional_terminal_kind(last_kind);
    const ParsedFunctionalStage* last = terminal
        ? &terminal_operation(pipeline) : nullptr;
    std::ostringstream out;
    out << "{\n        // Moss backend: FUSED FUNCTIONAL PIPELINE; one explicit loop, intermediates eliminated";
    if (planned) {
      out << "; plan %" << planned->transient_id << ", semantic "
          << planned->semantic_identity << "; provenance";
      for (const auto& origin : planned->lowered_provenance)
        out << " " << origin;
    }
    out << "\n"
        << "        let __moss_pipeline_source = &("
        << expr(pipeline.source, domain, locals, types) << ");\n";
    if (!terminal)
      out << "        let mut __moss_result = std::vec::Vec::new();\n";
    else if (last_kind == FunctionalNodeKind::Reduce)
      out << "        let mut __moss_result = "
          << expr(last->arguments.front(), domain, locals, types) << ";\n";
    else if (last_kind == FunctionalNodeKind::Sum)
      out << "        let mut __moss_result = "
          << functional_zero(final_element_type) << ";\n";
    else if (last_kind == FunctionalNodeKind::Count)
      out << "        let mut __moss_result = 0_i64;\n";
    else
      out << "        let mut __moss_result = "
          << (last_kind == FunctionalNodeKind::All ? "true" : "false")
          << ";\n";
    bool source_copy = copy_type(current_type);
    out << "        for __moss_item_ref in __moss_pipeline_source.iter() {\n"
        << "            let __moss_value_0 = "
        << (source_copy ? "*" : "") << "__moss_item_ref;\n";
    FunctionalValue current{"__moss_value_0", current_type, !source_copy};
    size_t map_number = 0;
    for (const auto& stage : pipeline.stages) {
      switch (stage.kind) {
        case FunctionalNodeKind::Map: {
          for (const auto& operation : stage.operations) {
            string next = "__moss_value_" + std::to_string(++map_number);
            out << "            let " << next << " = "
                << render_functional_callable(
                       operation.arguments.front(), {current}, domain,
                       locals, types)
                << ";\n";
            auto result = generated_callable_result(
                operation.arguments.front(), {current.type}, types);
            if (!result)
              throw std::runtime_error(
                  "internal error: untyped fused map result");
            current = {next, *result, false};
          }
          break;
        }
        case FunctionalNodeKind::Filter: {
          out << "            if !(";
          for (size_t predicate = 0;
               predicate < stage.operations.size(); ++predicate) {
            if (predicate) out << " && ";
            out << "("
                << render_functional_callable(
                       stage.operations[predicate].arguments.front(),
                       {current}, domain, locals, types)
                << ")";
          }
          out << ") { continue; }\n";
          break;
        }
        case FunctionalNodeKind::Reduce: {
          const auto& operation = stage.operations.front();
          auto accumulator_type = generated_expr_type(
              operation.arguments.front(), types).value_or(current.type);
          out << "            __moss_result = "
              << render_functional_callable(
                     operation.arguments[1],
                     {{"__moss_result", accumulator_type, false}, current},
                     domain, locals, types)
              << ";\n";
          break;
        }
        case FunctionalNodeKind::Sum:
          if (canonical_type_name(current.type) == "int")
            out << "            __moss_result = __moss_result.wrapping_add("
                << current.expression << ");\n";
          else
            out << "            __moss_result += " << current.expression << ";\n";
          break;
        case FunctionalNodeKind::Count:
          out << "            __moss_result = __moss_result.wrapping_add(1_i64);\n";
          break;
        case FunctionalNodeKind::Any: {
          const auto& operation = stage.operations.front();
          string predicate = operation.arguments.empty()
              ? current.expression
              : render_functional_callable(
                    operation.arguments.front(), {current}, domain, locals,
                    types);
          out << "            if " << predicate
              << " { __moss_result = true;";
          if (planned && planned->short_circuit_terminal) out << " break;";
          out << " }\n";
          break;
        }
        case FunctionalNodeKind::All: {
          const auto& operation = stage.operations.front();
          string predicate = operation.arguments.empty()
              ? current.expression
              : render_functional_callable(
                    operation.arguments.front(), {current}, domain, locals,
                    types);
          out << "            if !(" << predicate
              << ") { __moss_result = false;";
          if (planned && planned->short_circuit_terminal) out << " break;";
          out << " }\n";
          break;
        }
        case FunctionalNodeKind::Source:
          break;
      }
    }
    if (!terminal)
      out << "            __moss_result.push(" << current.expression << ");\n";
    out << "        }\n        __moss_result\n    }";
    return out.str();
  }

  std::optional<string> functional_expr(
      const string& expression, const Domain* domain,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      size_t functional_pipeline_id) const {
    auto pipeline = functional_pipeline_from_est(expression);
    if (!pipeline) return std::nullopt;
    if (auto chunk_lowering = gen_fileio_chunk_pipeline(
            *pipeline, domain, locals, types, functional_pipeline_id))
      return chunk_lowering;
    auto source_type = generated_expr_type(pipeline->source, types);
    if (!source_type || !generated_functional_element_type(*source_type))
      return std::nullopt;
    const FunctionalPipeline* planned =
        planned_functional_pipeline(functional_pipeline_id);
    // A pipeline nested in an ordinary expression does not have a standalone
    // statement plan.  It still has fully checked semantics, so use the
    // baseline eager lowering rather than requiring an unrelated outer
    // expression to masquerade as a pipeline statement.
    if (!planned)
      return gen_eager_functional_pipeline(*pipeline, domain, locals, types,
                                           nullptr);
    LoweredFunctionalPipeline lowered =
        lower_functional_semantic_plan(*pipeline, *planned);
    if (planned->virtual_upstream_pipeline_id) {
      const FunctionalPipeline* upstream = planned_functional_pipeline(
          planned->virtual_upstream_pipeline_id);
      auto upstream_parsed = upstream
          ? functional_pipeline_from_est(upstream->expression) : std::nullopt;
      if (!upstream_parsed || upstream_parsed->stages.empty() ||
          functional_terminal_kind(upstream_parsed->stages.back().kind))
        throw std::runtime_error(
            "internal error: invalid virtual functional upstream plan");
      LoweredFunctionalPipeline upstream_lowered =
          lower_functional_semantic_plan(*upstream_parsed, *upstream);
      vector<LoweredFunctionalStage> stages =
          std::move(upstream_lowered.stages);
      stages.insert(stages.end(), lowered.stages.begin(), lowered.stages.end());
      lowered.source = upstream_parsed->source;
      lowered.stages = std::move(stages);
    }
    if (planned->count_uses_exact_length) {
      std::ostringstream out;
      if (planned->count_uses_known_size)
        out << "{\n        // Moss backend: COUNT -> KNOWN SIZE; inert literal work eliminated";
      else
        out << "{\n        // Moss backend: COUNT -> EXACT LENGTH; mapped outputs are dead";
      out << "; plan %" << planned->transient_id << ", semantic "
          << planned->semantic_identity << "; provenance";
      for (const auto& origin : planned->lowered_provenance)
        out << " " << origin;
      if (planned->count_uses_known_size)
        out << "\n        " << planned->known_source_size << "_i64\n    }";
      else
        out << "\n"
            << "        let __moss_pipeline_source = &("
            << expr(lowered.source, domain, locals, types) << ");\n"
            << "        __moss_pipeline_source.len() as i64\n    }";
      return out.str();
    }
    if (planned && planned->fused)
      return gen_fused_functional_pipeline(lowered, domain, locals, types,
                                           planned);
    return gen_eager_functional_pipeline(*pipeline, domain, locals, types,
                                         planned);
  }

  string expr(string e, const Domain* d, const std::set<string>& locals,
              const std::unordered_map<string,string>* types = nullptr,
              size_t functional_pipeline_id = 0,
              const vector<size_t>* message_argument_plans = nullptr) const {
    // Moss parentheses are grouping syntax, not an observable expression node.
    // Remove only complete balanced outer groups before lowering. Nested groups
    // still protect precedence until their recursive expr() call reaches them.
    e = tree_strip_expression_grouping(std::move(e));
    if (phase21_option_slots_.count(e))
      return "__moss_recovery_slot_" + e +
          (phase21_codegen_statement_ &&
           phase21_codegen_statement_->ownership_consumed_bindings.count(e)
              ? ".take()" : ".as_ref()") +
          ".unwrap_or_else(|| std::process::abort())";
    if (auto place = view_place(e, d, locals, types)) return *place;
    if (write_through_parameters_.count(e) && types && types->count(e) && copy_type(types->at(e)))
      return "*" + e;
    // `message` is a Moss expression as well as a statement.  Assignment
    // statements have a dedicated lowering path, but expression positions
    // (for example `echo message d.Get()`) must retain the same synchronous
    // completion and reply semantics through synchronized handler entry.
    string message_target, message_handler;
    vector<string> message_arguments;
    if (tree_parse_message(e, message_target, message_handler,
                           message_arguments)) {
      string receiver = message_target;
      string handler = message_handler;
      vector<string> arguments = std::move(message_arguments);
      if (!types || !tree_plain_identifier(receiver))
        throw std::runtime_error("internal error: malformed message expression");
      auto receiver_type = types->find(receiver);
      if (receiver_type == types->end())
        throw std::runtime_error("internal error: unresolved message receiver");
      auto domain_it = domains_.find(canonical_type_name(receiver_type->second));
      if (domain_it == domains_.end())
        throw std::runtime_error("internal error: message expression receiver is not a domain");
      const Domain& target_domain = *domain_it->second;
      const Handler* target_handler = find_handler(target_domain, handler);
      if (!target_handler)
        throw std::runtime_error("internal error: unresolved message expression handler");
      if (root_ingress_statement_)
        return root_ingress_message(receiver, handler, arguments, target_domain,
                                    *target_handler, d, locals, types,
                                    message_argument_plans);
      const bool raises = !target_handler->observable_effects.raise_set.empty();
      if (raises && !phase21_result_context_)
        throw std::runtime_error(
            "internal error: checked raising message lacks a Phase 21 outcome context");
      if (target_domain.exported && !exported_domain_bridge_body_) {
        std::ostringstream call;
        call << expr(receiver, d, locals, types) << ".__moss_message_" << handler << "(";
        for (size_t index = 0; index < arguments.size(); ++index) {
          if (index) call << ", ";
          call << message_arg(arguments[index], target_handler->params.at(index).type,
                              d, locals, types, message_argument_plans ? message_argument_plans->at(index) : 0);
        }
        call << ")";
        return raises ? phase21_propagate(call.str()) : call.str();
      }
      std::ostringstream rendered;
      rendered << expr(receiver, d, locals, types) << "." << handler << "_shared(";
      for (size_t index = 0; index < arguments.size(); ++index) {
        if (index) rendered << ", ";
        rendered << shared_message_arg(arguments[index], target_handler->params.at(index).type,
                                       d, locals, types,
                                       message_argument_plans ? message_argument_plans->at(index) : 0);
      }
      rendered << ")";
      if (raises) {
        const string propagated = phase21_propagate(rendered.str());
        rendered.str("");
        rendered.clear();
        rendered << propagated;
      }
      if (target_handler->reply_type)
        rendered << ".unwrap_or_else(|| std::process::abort())";
      return rendered.str();
    }
    if (auto functional = functional_expr(
            e, d, locals, types, functional_pipeline_id))
      return *functional;
    e = tree_normalize_pipeline(std::move(e));
    // Minimal surface rewrites.
    if (e == "true" || e == "false") return e;
    if (e == "None") return "None";
    // Numeric literals are emitted from the shared decoder's value as a
    // canonical suffixed Rust literal (`16_i64`, `2.5_f64`), never from Moss
    // spelling.
    if (generated_integer_literal(e) || decimal_float_literal(e)) {
      if (auto rust = rust_numeric_literal(e)) return *rust;
      throw std::runtime_error("internal error: numeric literal '" + e +
                               "' was not validated by the lexer");
    }
    if (auto typed_vector = typed_empty_vector_constructor(e)) {
      string element = trim(typed_vector->substr(7, typed_vector->size() - 8));
      return "std::vec::Vec::<" + rust_type(element) + ">::new()";
    }
    auto expression_shape = expression_view_from_est(e);
    if (expression_shape && est::empty_constructor_named(*expression_shape, "Map")) return staged_abi_v10()
        ? "std::default::Default::default()"
        : "std::collections::HashMap::new()";
    if (expression_shape && staged_abi_v10() && est::empty_constructor_named(*expression_shape, "Set"))
      return "moss_runtime::MossSet::new()";
    if (expression_shape && est::empty_constructor_named(*expression_shape, "Queue"))
      return "std::collections::VecDeque::new()";
    {
      string callee;
      vector<string> arguments;
      if (tree_parse_simple_call(e, callee, arguments) && callee == "replace" &&
          arguments.size() == 2)
        return "{ let __moss_replacement = " +
            expr(arguments[1], d, locals, types) + "; std::mem::replace(&mut " +
            write_call_place(arguments[0], d, locals, types) +
            ", __moss_replacement) }";
    }
    {
      string enum_name, case_name;
      vector<string> fields;
      bool called = false;
      if (tree_parse_enum_case_expression(e, enum_name, case_name,
                                          fields, called) &&
          enums_.count(enum_name)) {
        std::ostringstream rendered;
        rendered << enum_name << "::" << rust_identifier(case_name);
        if (!fields.empty()) {
          rendered << " { ";
          for (size_t index = 0; index < fields.size(); ++index) {
            string name, value;
            if (!tree_parse_named_argument(fields[index], name, value))
              throw std::runtime_error("unchecked enum constructor field");
            if (index) rendered << ", ";
            rendered << rust_identifier(name) << ": "
                     << expr(value, d, locals, types);
          }
          rendered << " }";
        }
        return rendered.str();
      }
    }

    for (const auto& boolean_operator :
         vector<std::pair<string, string>>{{" or ", "||"},
                                                {" xor ", "^"},
                                                {" and ", "&&"}}) {
      if (auto binary = tree_generated_binary(e, {boolean_operator.first}))
        return "(" + expr(binary->left, d, locals, types) + ") " +
            boolean_operator.second + " (" +
            expr(binary->right, d, locals, types) + ")";
    }
    string unary_operation, unary_operand;
    if (tree_parse_unary(e, unary_operation, unary_operand) &&
        unary_operation == "not")
      return "!(" + expr(unary_operand, d, locals, types) + ")";
    if (auto comparison =
            tree_generated_binary(e, {"==", "!=", "<=", ">=", "<", ">"})) {
      string left = expr(comparison->left, d, locals, types);
      string right = expr(comparison->right, d, locals, types);
      left = value_from_borrowed_message_parameter(comparison->left, left, types);
      right = value_from_borrowed_message_parameter(comparison->right, right, types);
      auto left_type = generated_expr_type(comparison->left, types);
      auto right_type = generated_expr_type(comparison->right, types);
      if (left_type && canonical_type_name(*left_type) == "string") {
        left = "(" + left + ").as_str()";
      }
      if (right_type && canonical_type_name(*right_type) == "string") {
        right = "(" + right + ").as_str()";
      }
      if (left_type && canonical_type_name(*left_type) == "float" &&
          right_type && canonical_type_name(*right_type) == "int") {
        if (generated_integer_literal(comparison->right))
          right = generated_integer_literal_as_float(comparison->right);
        else
          right = "((" + right + ") as f64)";
      }
      if (right_type && canonical_type_name(*right_type) == "float" &&
          left_type && canonical_type_name(*left_type) == "int") {
        if (generated_integer_literal(comparison->left))
          left = generated_integer_literal_as_float(comparison->left);
        else
          left = "((" + left + ") as f64)";
      }
      return "(" + left + ") " + comparison->op + " (" + right + ")";
    }
    if (auto binary = tree_generated_binary(e, {"|"})) {
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
      return "(" + left + ") | (" + right + ")";
    }
    if (auto binary = tree_generated_binary(e, {"^"})) {
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
      return "(" + left + ") ^ (" + right + ")";
    }
    if (auto binary = tree_generated_binary(e, {"&"})) {
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
      return "(" + left + ") & (" + right + ")";
    }
    if (auto binary = tree_generated_binary(e, {"<<", ">>"})) {
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
      string fn_name = binary->op == "<<" ? "__moss_shl" : "__moss_shr";
      return fn_name + "(" + left + ", " + right + ")";
    }
    for (const auto& operators : vector<vector<string>>{{"+", "-"}, {"*", "/", "%"}}) {
      auto binary = tree_generated_binary(e, operators);
      if (!binary) continue;
      string left = expr(binary->left, d, locals, types);
      string right = expr(binary->right, d, locals, types);
      left = value_from_borrowed_message_parameter(binary->left, left, types);
      right = value_from_borrowed_message_parameter(binary->right, right, types);
      auto left_type = generated_expr_type(binary->left, types);
      auto right_type = generated_expr_type(binary->right, types);
      bool string_concatenation = binary->op == "+" &&
          left_type && right_type &&
          canonical_type_name(*left_type) == "string" &&
          canonical_type_name(*right_type) == "string";
      if (string_concatenation && tree_empty_string_literal(binary->right))
        return "(" + left + ").clone()";
      if (string_concatenation && tree_empty_string_literal(binary->left))
        return "(" + right + ").clone()";
      if (string_concatenation) {
        vector<string> operands;
        std::function<void(string)> collect = [&](string source) {
          source = tree_strip_expression_grouping(std::move(source));
          auto nested = tree_generated_binary(source, {"+"});
          auto nested_left = nested
              ? generated_expr_type(nested->left, types) : std::nullopt;
          auto nested_right = nested
              ? generated_expr_type(nested->right, types) : std::nullopt;
          if (nested && nested_left && nested_right &&
              canonical_type_name(*nested_left) == "string" &&
              canonical_type_name(*nested_right) == "string") {
            collect(nested->left);
            collect(nested->right);
          } else {
            operands.push_back(std::move(source));
          }
        };
        collect(e);
        std::ostringstream joined;
        joined << "{ ";
        for (size_t index = 0; index < operands.size(); ++index) {
          const string operand = trim(operands[index]);
          const bool literal = operand.size() >= 2 &&
              operand.front() == '"' && operand.back() == '"';
          joined << "let __moss_concat_" << index << " = ";
          if (literal) joined << operand << "; ";
          else
            joined << "&(" << expr(operands[index], d, locals, types)
                   << "); ";
        }
        joined << "let mut __moss_concat_result = MossStr::with_capacity(";
        for (size_t index = 0; index < operands.size(); ++index) {
          if (index) joined << ".saturating_add(";
          joined << "__moss_concat_" << index << ".len()";
        }
        for (size_t index = 1; index < operands.size(); ++index) joined << ")";
        joined << "); ";
        for (size_t index = 0; index < operands.size(); ++index)
          joined << "__moss_concat_result.push_str(__moss_concat_" << index
                 << (trim(operands[index]).size() >= 2 &&
                     trim(operands[index]).front() == '"' &&
                     trim(operands[index]).back() == '"' ? "); " : ".as_str()); ");
        joined << "__moss_concat_result }";
        return joined.str();
      }
      bool integer_operation = left_type && right_type &&
          canonical_type_name(*left_type) == "int" &&
          canonical_type_name(*right_type) == "int";
      if (integer_operation) {
        string method = binary->op == "+" ? "wrapping_add"
            : binary->op == "-" ? "wrapping_sub"
            : binary->op == "*" ? "wrapping_mul"
            : binary->op == "/" ? "wrapping_div" : "wrapping_rem";
        return "(" + left + ")." + method + "(" + right + ")";
      }
      if (left_type && canonical_type_name(*left_type) == "float" &&
          right_type && canonical_type_name(*right_type) == "int") {
        if (generated_integer_literal(binary->right))
          right = generated_integer_literal_as_float(binary->right);
        else
          right = "((" + right + ") as f64)";
      }
      if (right_type && canonical_type_name(*right_type) == "float" &&
          left_type && canonical_type_name(*left_type) == "int") {
        if (generated_integer_literal(binary->left))
          left = generated_integer_literal_as_float(binary->left);
        else
          left = "((" + left + ") as f64)";
      }
      return "(" + left + ") " + binary->op + " (" + right + ")";
    }
    // Moss Int arithmetic wraps; Rust's `-x` panics on Int min in debug.
    if (tree_parse_unary(e, unary_operation, unary_operand) &&
        unary_operation == "-") {
      const string inner = unary_operand;
      if (!inner.empty() && std::isdigit(static_cast<unsigned char>(inner.front())) &&
          generated_integer_literal(inner))
        if (auto rust = rust_numeric_literal("-" + inner)) return *rust;
      auto operand_type = generated_expr_type(inner, types);
      if (operand_type && canonical_type_name(*operand_type) == "int") {
        string operand = expr(inner, d, locals, types);
        operand = value_from_borrowed_message_parameter(inner, operand, types);
        return "(" + operand + ").wrapping_neg()";
      }
      // Float: negate the whole postfix operand (P25-S-ARCH-022), so `-v[0]`
      // is `-(v[0])`, never `(-v)[0]`.
      if (operand_type && canonical_type_name(*operand_type) == "float" &&
          !std::isdigit(static_cast<unsigned char>(inner.front())) && inner.front() != '.' &&
          inner.front() != '(') {
        string operand = expr(inner, d, locals, types);
        operand = value_from_borrowed_message_parameter(inner, operand, types);
        return "-(" + operand + ")";
      }
    }
    if (tree_parse_unary(e, unary_operation, unary_operand) &&
        unary_operation == "~") {
      string operand = expr(unary_operand, d, locals, types);
      operand = value_from_borrowed_message_parameter(unary_operand, operand, types);
      return "!(" + operand + ")";
    }
    // Only classify a quoted source form as a single literal after top-level
    // operators have had a chance to split it. Expressions such as
    // "a" + "b" and "a" == "b" also begin and end with quote characters.
    if (is_single_string_literal(e))
      return "MossStr::from(" + e + ")";
    if (is_single_char_literal(e))
      return e;
    if (auto parts = tree_parse_vector_literal(e)) {
      std::ostringstream r; r << "vec![";
      for (size_t i=0;i<parts->size();++i) {
        if (i) r << ", ";
        string value = expr(parts->at(i), d, locals, types);
        auto value_type = generated_expr_type(parts->at(i), types);
        if (value_type && domains_.count(canonical_type_name(*value_type)))
          value = "(" + value + ").clone()";
        r << value;
      }
      r << "]"; return r.str();
    }
    string ib, ii;
    if (tree_parse_index(e, ib, ii)) {
      auto base_type = generated_expr_type(ib, types);
      if (base_type && shared_protocol_map_key_from_type(*base_type)) {
        const string key_type = *shared_protocol_map_key_from_type(*base_type);
        const string key = expr(ii, d, locals, types);
        const string borrowed = borrowed_function_parameters_.count(trim(ii))
            ? "__moss_key" : "&__moss_key";
        return "{ let __moss_key = " + key + "; (" +
            expr(ib, d, locals, types) +
            ").get_with(" + shared_map_hash(key_type, "__moss_key") +
            ", " + borrowed + ", |a, b| " +
            shared_map_equal(key_type, "a", "b") +
            ").expect(\"Moss missing map key\").clone() }";
      }
      if (base_type && canonical_type_name(*base_type) == "RangeBatch")
        return "(" + expr(ib, d, locals, types) + ").get(" +
            expr(ii, d, locals, types) + ")";
      if (base_type && canonical_type_name(*base_type) == "Range")
        return "(" + expr(ib, d, locals, types) + ").get(" +
            expr(ii, d, locals, types) + ")";
      bool string_index = ii.size() >= 2 && ii.front() == '"' && ii.back() == '"';
      string ir = string_index ? ii : expr(ii, d, locals, types);
      bool map_index = base_type &&
          (canonical_type_name(*base_type) == "map" ||
           starts_with(canonical_type_name(*base_type), "map["));
      if (!string_index && !map_index) ir = "(" + ir + ") as usize";
      else if (map_index && !string_index) {
        // HashMap<String, _> accepts `str` as a borrowed lookup key. This
        // works for both an owned String and an already-borrowed &String,
        // whereas another `&` would emit an invalid &&String lookup.
        auto key_type = generated_expr_type(ii, types);
        if (key_type && canonical_type_name(*key_type) == "string")
          ir = "(" + ir + ").as_str()";
        else if (!borrowed_function_parameters_.count(trim(ii)))
          ir = "&(" + ir + ")";
      }
      string indexed = "(" + expr(ib, d, locals, types) + ")[" + ir + "]";
      return borrowed_view_expression(ib, d, locals, types) ? indexed : "(" + indexed + ").clone()";
    }

    string projection_base, projection_field;
    if (tree_parse_member_access(e, projection_base, projection_field)) {
      if (tree_plain_identifier(projection_field)) {
        auto projection_type = generated_expr_type(projection_base, types);
        if (projection_type) {
          auto object = objects_.find(nominal_template_name(*projection_type));
          if (object != objects_.end()) {
            auto field = std::find_if(object->second->fields.begin(),
                object->second->fields.end(), [&](const Field& candidate) {
                  return candidate.name == projection_field;
                });
            if (field != object->second->fields.end()) {
              string base_rendered = expr(projection_base, d, locals, types);
              return "(" + base_rendered + ")." + rust_identifier(projection_field);
            }
          }
        }
      }
    }

    string member_receiver, member_name;
    vector<string> member_arguments;
    if (tree_parse_member_call(e, member_receiver, member_name, member_arguments)) {
      string canon_recv = canonical_type_name(member_receiver);
      if (canon_recv == "int" || canon_recv == "float" || canon_recv == "char" || canon_recv == "string" || canon_recv == "bool") {
        if ((member_name == "less" || member_name == "isequal") &&
            member_arguments.size() == 2)
          return scalar_protocol_rust(member_name, canon_recv,
                                      expr(member_arguments[0], d, locals, types),
                                      expr(member_arguments[1], d, locals, types));
        if (member_name == "hash" && member_arguments.size() == 1)
          return scalar_protocol_rust("hash", canon_recv,
                                      expr(member_arguments[0], d, locals, types));
        if (member_name == "to_string" && member_arguments.size() == 1) {
          return "MossStr::from((" + expr(member_arguments[0], d, locals, types) + ").to_string())";
        }
        if (canon_recv == "char" && member_name == "codepoint" && member_arguments.size() == 1) {
          return "((" + expr(member_arguments[0], d, locals, types) + ") as u32 as i64)";
        }
      }
      string receiver_expression = method_receiver_place(member_receiver, member_name, member_arguments, d, locals, types);
      if (starts_with(receiver_expression, "*")) receiver_expression = "(" + receiver_expression + ")";
      auto receiver_type = generated_expr_type(member_receiver, types);
      if (staged_abi_v10() && tree_empty_constructor_named(member_receiver, "Map") &&
          member_name == "get" &&
          member_arguments.size() == 2) {
        auto key = generated_expr_type(member_arguments[0], types);
        auto value = generated_expr_type(member_arguments[1], types);
        if (key && value) {
          receiver_type = "map[" + canonical_type_name(*key) + "," +
              canonical_type_name(*value) + "]";
          receiver_expression = "<" + rust_type(*receiver_type) + ">::new()";
        }
      }
      if (receiver_type) {
        string concrete = canonical_type_name(*receiver_type);
        if (concrete == "FileIO" && member_name == "read" &&
            member_arguments.size() == 2) {
          string read = receiver_expression +
              (phase21_result_context_ ? ".read_checked(" : ".read(") +
              expr(member_arguments[0], d, locals, types) + ", " +
              expr(member_arguments[1], d, locals, types) + ")";
          return phase21_result_context_
              ? phase21_propagate(read + ".map_err(__moss_raise_fileio)")
              : read;
        }
        if (concrete == "FileIO" && member_name == "write" &&
            member_arguments.size() == 2) {
          string call = receiver_expression +
              (phase21_result_context_ ? ".write_checked(" : ".write(") +
              expr(member_arguments[0], d, locals, types) + ", &( " +
              expr(member_arguments[1], d, locals, types) + " ))";
          return phase21_result_context_
              ? phase21_propagate(call + ".map_err(__moss_raise_fileio)")
              : call;
        }
        if (concrete == "FileIO" && member_name == "open" &&
            member_arguments.size() == 2) {
          string call = receiver_expression +
              (phase21_result_context_ ? ".open_in_place_checked(&( "
                                       : ".open_in_place(&( ") +
              expr(member_arguments[0], d, locals, types) + " ), " +
              rust_string_literal(trim(member_arguments[1])) + ")";
          return phase21_result_context_
              ? phase21_propagate(call + ".map_err(__moss_raise_fileio)")
              : call;
        }
        if (concrete == "FileIO" && member_name == "sync" &&
            member_arguments.size() <= 1) {
          bool dataonly = !member_arguments.empty() &&
              tree_identifier_name(member_arguments[0]) ==
                  std::optional<string>("dataonly");
          string call = receiver_expression +
              (phase21_result_context_
                  ? ".sync_checked(" + string(dataonly ? "true" : "false") + ")"
                  : dataonly ? ".sync_dataonly()" : ".sync()");
          return phase21_result_context_
              ? phase21_propagate(call + ".map_err(__moss_raise_fileio)")
              : call;
        }
        if (concrete == "FileIO" && member_name == "close" &&
            member_arguments.empty()) {
          string call = receiver_expression +
              (phase21_result_context_ ? ".close_checked()" : ".close()");
          return phase21_result_context_
              ? phase21_propagate(call + ".map_err(__moss_raise_fileio)")
              : call;
        }
        // Agent A checks the one-argument read form as a bounded RangeBatch.
        // This branch only adapts that checked form to Agent B's read_batch
        // ABI; it deliberately does not repeat request legality or bound
        // analysis here.
        if (concrete == "FileIO" && member_name == "read" &&
            member_arguments.size() == 1) {
          const size_t batch_id = fileio_chunk_temp_++;
          string receiver_temp = "__moss_fileio_batch_receiver_" +
              std::to_string(batch_id);
          std::ostringstream lowered;
          lowered << "{ let " << receiver_temp << " = &("
                  << receiver_expression << "); ";
          string requests = tree_strip_expression_grouping(
              trim(member_arguments.front()));
          bool inline_requests = requests.size() >= 2 &&
              requests.front() == '[' && requests.back() == ']';
          if (!inline_requests) {
            auto request_type = generated_expr_type(requests, types);
            string concrete_request_type = request_type
                ? canonical_type_name(*request_type) : string();
            if (!starts_with(concrete_request_type, "vector[") ||
                !ends_with(concrete_request_type, "]"))
              throw std::runtime_error(
                  "internal error: checked FileIO batch requests lost their vector type");
            string record_type = trim(concrete_request_type.substr(
                7, concrete_request_type.size() - 8));
            auto record = objects_.find(canonical_type_name(record_type));
            if (record == objects_.end())
              throw std::runtime_error(
                  "internal error: checked FileIO batch request record is unavailable");
            auto offset_field = std::find_if(record->second->fields.begin(),
                record->second->fields.end(), [](const Field& field) {
                  return field.name == "offset";
                });
            auto size_field = std::find_if(record->second->fields.begin(),
                record->second->fields.end(), [](const Field& field) {
                  return field.name == "size";
                });
            if (offset_field == record->second->fields.end() ||
                size_field == record->second->fields.end())
              throw std::runtime_error(
                  "internal error: checked FileIO batch request fields are unavailable");
            string requests_temp = "__moss_fileio_batch_records_" +
                std::to_string(batch_id);
            string pairs_temp = "__moss_fileio_batch_pairs_" +
                std::to_string(batch_id);
            lowered << "let " << requests_temp << " = &("
                    << expr(requests, d, locals, types) << "); "
                    << "let " << pairs_temp << " = " << requests_temp
                    << ".iter().map(|__moss_request| (__moss_request."
                    << rust_identifier(offset_field->name)
                    << ", __moss_request."
                    << rust_identifier(size_field->name)
                    << ")).collect::<std::vec::Vec<_>>(); "
                    << receiver_temp
                    << (phase21_result_context_ ? ".read_batch_checked(&"
                                                : ".read_batch(&")
                    << pairs_temp << ") }";
            return phase21_result_context_
                ? phase21_propagate("(" + lowered.str() +
                    ").map_err(__moss_raise_fileio)")
                : lowered.str();
          }

          vector<string> entries = tree_parse_vector_literal(requests)
              .value_or(vector<string>{});
          vector<std::pair<string, string>> coordinate_temps;
          for (size_t entry_index = 0; entry_index < entries.size();
               ++entry_index) {
            string entry = trim(entries[entry_index]);
            vector<string> coordinates;
            if (auto tuple_coordinates = tree_tuple_elements(entry)) {
              coordinates = std::move(*tuple_coordinates);
            } else {
              string constructor;
              vector<string> fields;
              if (tree_parse_simple_call(entry, constructor, fields) &&
                  objects_.count(canonical_type_name(constructor))) {
                std::map<string, string> named;
                for (const auto& field : fields) {
                  string name, expression;
                  if (tree_parse_named_argument(field, name, expression))
                    named[name] = expression;
                }
                if (named.count("offset") && named.count("size") &&
                    fields.size() == 2)
                  coordinates = {named["offset"], named["size"]};
              }
            }
            if (coordinates.size() != 2)
              throw std::runtime_error(
                  "internal error: checked FileIO batch entry was not a request pair");
            string offset_temp = "__moss_fileio_batch_offset_" +
                std::to_string(batch_id) + "_" + std::to_string(entry_index);
            string size_temp = "__moss_fileio_batch_size_" +
                std::to_string(batch_id) + "_" + std::to_string(entry_index);
            lowered << "let " << offset_temp << " = "
                    << expr(coordinates[0], d, locals, types) << "; let "
                    << size_temp << " = "
                    << expr(coordinates[1], d, locals, types) << "; ";
            coordinate_temps.emplace_back(std::move(offset_temp),
                                          std::move(size_temp));
          }
          lowered << receiver_temp
                  << (phase21_result_context_ ? ".read_batch_checked(&["
                                              : ".read_batch(&[");
          for (size_t index = 0; index < coordinate_temps.size(); ++index) {
            if (index) lowered << ", ";
            lowered << "(" << coordinate_temps[index].first << ", "
                    << coordinate_temps[index].second << ")";
          }
          lowered << "]) }";
          return phase21_result_context_
              ? phase21_propagate("(" + lowered.str() +
                  ").map_err(__moss_raise_fileio)")
              : lowered.str();
        }
        // Phase 20: Moss `range.length()` lowers to Agent B's runtime
        // `Range::len() -> i64`; Agent B need not expose a `.length()` alias.
        if ((concrete == "Range" || concrete == "RangeBatch") && member_name == "length" && member_arguments.empty())
          return "(" + receiver_expression + ").len()";
        if (concrete == "Range" && member_name == "slice" && member_arguments.size() == 2)
          return "(" + receiver_expression + ").slice(" +
              expr(member_arguments[0], d, locals, types) + ", " +
              expr(member_arguments[1], d, locals, types) + ")";
        if (concrete == "string") {
          string source = "(" + receiver_expression + ")";
          if (member_name == "length" && member_arguments.empty())
            return "__moss_string_length(&" + source + ")";
          if (member_name == "chars" && member_arguments.empty())
            return source + ".chars().collect::<std::vec::Vec<char>>()";
          if (member_name == "char_at" && member_arguments.size() == 1)
            return "__moss_string_char_at(&(" + source + "), " + expr(member_arguments[0], d, locals, types) + ")";
          if ((member_name == "less" || member_name == "isequal") &&
              member_arguments.size() == 1)
            return scalar_protocol_rust(member_name, concrete, source,
                                        expr(member_arguments[0], d, locals, types));
          if (member_name == "hash" && member_arguments.empty())
            return scalar_protocol_rust("hash", concrete, source);
          if (member_name == "find" && member_arguments.size() == 1)
            return "{ let __moss_pattern = " + expr(member_arguments[0], d, locals, types) +
                "; __moss_string_find(&" + source + ", __moss_pattern.as_str()) }";
          if (member_name == "slice" && member_arguments.size() == 2)
            return "__moss_string_slice(&" + source + ", " +
                expr(member_arguments[0], d, locals, types) + ", " +
                expr(member_arguments[1], d, locals, types) + ")";
          if (member_name == "ascii_lower" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_ascii_lowercase())";
          if (member_name == "ascii_upper" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_ascii_uppercase())";
          if (member_name == "reserve" && member_arguments.size() == 1)
            return "{ let __moss_string = &mut (" +
                write_call_place(member_receiver, d, locals, types) +
                "); let __moss_additional = " +
                expr(member_arguments[0], d, locals, types) +
                "; if __moss_additional > 0 { __moss_string.reserve(__moss_additional as usize); } }";
          if (member_name == "append" && member_arguments.size() == 1)
            return "{ let __moss_string = &mut (" +
                write_call_place(member_receiver, d, locals, types) +
                "); let __moss_part = " + [&]() {
                  const string part = trim(member_arguments[0]);
                  return part.size() >= 2 && part.front() == '"' &&
                      part.back() == '"' ? part : "&(" +
                      expr(member_arguments[0], d, locals, types) + ")";
                }() + "; __moss_string.push_str(" + [&]() {
                  const string part = trim(member_arguments[0]);
                  return string("__moss_part") +
                      (part.size() >= 2 && part.front() == '"' &&
                       part.back() == '"' ? "" : ".as_str()");
                }() + "); }";
          if (member_name == "split" && member_arguments.size() == 1)
            return "{ let __moss_separator = " + expr(member_arguments[0], d, locals, types) +
                "; if __moss_separator.is_empty() { std::process::abort(); } " + source +
                ".split(__moss_separator.as_str()).map(MossStr::from).collect::<std::vec::Vec<MossStr>>() }";
          if (member_name == "join" && member_arguments.size() == 1)
            return "MossStr::from((" + expr(member_arguments[0], d, locals, types) + ").join(" + source + ".as_str()))";
        }
        if (concrete == "char") {
          string source = "(" + receiver_expression + ")";
          if (member_name == "to_string" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_string())";
          if (member_name == "codepoint" && member_arguments.empty())
            return "((" + source + ") as u32 as i64)";
          if ((member_name == "less" || member_name == "isequal") &&
              member_arguments.size() == 1)
            return scalar_protocol_rust(member_name, concrete, source,
                                        expr(member_arguments[0], d, locals, types));
          if (member_name == "hash" && member_arguments.empty())
            return scalar_protocol_rust("hash", concrete, source);
        }
        if (concrete == "int") {
          string source = "(" + receiver_expression + ")";
          if (member_name == "to_string" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_string())";
          if ((member_name == "less" || member_name == "isequal") &&
              member_arguments.size() == 1)
            return scalar_protocol_rust(member_name, concrete, source,
                                        expr(member_arguments[0], d, locals, types));
          if (member_name == "hash" && member_arguments.empty())
            return scalar_protocol_rust("hash", concrete, source);
        }
        if (concrete == "float") {
          string source = "(" + receiver_expression + ")";
          if (member_name == "to_string" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_string())";
          if ((member_name == "less" || member_name == "isequal") &&
              member_arguments.size() == 1)
            return scalar_protocol_rust(member_name, concrete, source,
                                        expr(member_arguments[0], d, locals, types));
          if (member_name == "hash" && member_arguments.empty())
            return scalar_protocol_rust("hash", concrete, source);
        }
        if (concrete == "bool") {
          string source = "(" + receiver_expression + ")";
          if (member_name == "to_string" && member_arguments.empty())
            return "MossStr::from(" + source + ".to_string())";
          if ((member_name == "less" || member_name == "isequal") &&
              member_arguments.size() == 1)
            return scalar_protocol_rust(member_name, concrete, source,
                                        expr(member_arguments[0], d, locals, types));
          if (member_name == "hash" && member_arguments.empty())
            return scalar_protocol_rust("hash", concrete, source);
        }
        if (staged_abi_v10() && starts_with(concrete, "set[") &&
            ends_with(concrete, "]")) {
          const string key_type = concrete.substr(4, concrete.size() - 5);
          const bool nominal = shared_nominal_map_key(key_type);
          const string equal = nominal ? "a.isequal(b)"
              : tree_strip_expression_grouping(
                    scalar_protocol_rust("isequal", key_type, "*a", "*b"));
          if (member_name == "values" && member_arguments.empty())
            return "(" + receiver_expression + ").keys()";
          if ((member_name == "insert" || member_name == "contains") &&
              member_arguments.size() == 1) {
            string key = expr(member_arguments[0], d, locals, types);
            if (nominal && is_object_view(member_arguments[0], d, locals, types))
              key = "(" + key + ").__moss_value()";
            const string borrowed = borrowed_function_parameters_.count(
                trim(member_arguments[0])) ? "__moss_key" : "&__moss_key";
            const string hash = nominal ? "__moss_key.hash()"
                : tree_strip_expression_grouping(
                      scalar_protocol_rust("hash", key_type, "__moss_key"));
            if (member_name == "insert")
              return "{ let __moss_key = " + key + "; let __moss_hash = " +
                  hash + "; (" + write_call_place(member_receiver, d, locals, types) +
                  ").insert_with(__moss_hash, __moss_key, |a, b| " + equal + "); }";
            return "{ let __moss_key = " + key + "; (" + receiver_expression +
                ").contains_with(" + hash + ", " + borrowed +
                ", |a, b| " + equal + ") }";
          }
        }
        if (auto map_types = map_key_value_types(concrete)) {
          const bool shared_key = shared_protocol_map_key(map_types->first);
          if (member_name == "delete" && member_arguments.size() == 3) {
            string key = expr(member_arguments[0], d, locals, types);
            string fallback = expr(member_arguments[1], d, locals, types);
            string flag = write_call_place(member_arguments[2], d, locals, types);
            if (shared_key) {
              const bool view_key = is_object_view(member_arguments[0], d, locals, types);
              if (view_key) key = "(" + key + ").__moss_value()";
              const string borrowed = !view_key && borrowed_function_parameters_.count(
                  trim(member_arguments[0])) ? "__moss_key" : "&__moss_key";
              return "{ let __moss_key = " + key +
                  "; let __moss_fallback = " + fallback +
                  "; let __moss_removed = (" + receiver_expression +
                  ").remove_with(" + shared_map_hash(map_types->first, "__moss_key") +
                  ", " + borrowed + ", |a, b| " +
                  shared_map_equal(map_types->first, "a", "b") +
                  "); let _ = &( " + flag +
                  " ); " + flag + " = __moss_removed.is_some(); " +
                  "__moss_removed.unwrap_or(__moss_fallback) }";
            }
            string lookup = map_types->first == "string" ? "__moss_key.as_str()" : "&__moss_key";
            return "{ let __moss_key = " + key + "; let __moss_fallback = " + fallback +
                "; let __moss_removed = (" + receiver_expression + ").remove(" + lookup +
                "); let _ = &(" + flag + "); " + flag + " = __moss_removed.is_some(); " +
                "__moss_removed.unwrap_or(__moss_fallback) }";
          }
          if (member_name == "get" && member_arguments.size() == 2) {
            string key = expr(member_arguments[0], d, locals, types);
            if (shared_key) {
              string fallback = expr(member_arguments[1], d, locals, types);
              const bool view_key = is_object_view(member_arguments[0], d, locals, types);
              if (view_key) key = "(" + key + ").__moss_value()";
              const string borrowed = !view_key && borrowed_function_parameters_.count(
                  trim(member_arguments[0])) ? "__moss_key" : "&__moss_key";
              return "{ let __moss_key = " + key +
                  "; (" + receiver_expression +
                  ").get_with(" + shared_map_hash(map_types->first, "__moss_key") +
                  ", " + borrowed + ", |a, b| " +
                  shared_map_equal(map_types->first, "a", "b") +
                  ").cloned().unwrap_or(" +
                  fallback + ") }";
            }
            if (map_types->first == "string")
              key = "(" + key + ").as_str()";
            else
              key = "&( " + key + " )";
            string fallback = expr(member_arguments[1], d, locals, types);
            if (!copy_type(map_types->second))
              fallback = "(" + fallback + ").clone()";
            return "(" + receiver_expression + ").get(" + key + ").cloned().unwrap_or(" + fallback + ")";
          }
          if (member_name == "keys" && member_arguments.empty()) {
            if (shared_key)
              return "(" + receiver_expression + ").keys()";
            if (map_types->first == "float")
              return "(" + receiver_expression + ").keys().map(|k| k.0).collect::<std::vec::Vec<_>>()";
            return "(" + receiver_expression + ").keys().cloned().collect::<std::vec::Vec<_>>()";
          }
          if (member_name == "values" && member_arguments.empty())
            return shared_key
                ? "(" + receiver_expression + ").values()"
                : "(" + receiver_expression + ").values().cloned().collect::<std::vec::Vec<_>>()";
        }
        if (collection_named(parsed_type(concrete), {"queue"}))
          member_name = member_name == "push" ? "push_back" : member_name == "pop" ? "pop_front" : member_name;
        if (auto method = resolve_object_method(objects_, concrete, member_name == "push_back" ? "push" : member_name == "pop_front" ? "pop" : member_name,
                                                [&]() {
                                                  vector<string> result;
                                                  for (const auto& argument : member_arguments)
                                                    result.push_back(generated_expr_type(argument, types).value_or(""));
                                                  return result;
                                                }(), true, nullptr)) {
          const auto external_method_module =
              phase21_external_module_for_symbol(concrete);
          // A consuming concrete method cannot be invoked through either a
          // field-only access view or an ordinary borrowed function parameter.
          // The former has no method surface at all; the latter must retain
          // Moss's READ helper contract. Materialize the checked object value
          // at this native boundary before selecting the owned method.
          if (method->receiver_effect == Effect::Consume) {
            if (is_object_view(member_receiver, d, locals, types))
              receiver_expression = "(" + receiver_expression + ").__moss_value()";
            else if (borrowed_function_parameters_.count(trim(member_receiver)))
              receiver_expression = "(*(" + receiver_expression + ")).clone()";
          }
          vector<string> argument_temporaries(member_arguments.size());
          if (method->receiver_effect != Effect::Read) {
            for (size_t index = 0; index < member_arguments.size(); ++index) {
              string nested_receiver, nested_method;
              vector<string> nested_arguments;
              if (tree_parse_member_call(member_arguments[index], nested_receiver,
                                    nested_method, nested_arguments) &&
                  tree_expressions_equal(nested_receiver, member_receiver))
                argument_temporaries[index] = "__moss_call_argument_" +
                    std::to_string(call_argument_temp_++);
            }
          }
          const bool standard_write_bridge = external_method_module &&
              std::any_of(method->params.begin(), method->params.end(),
                  [&](const Param& parameter) {
                    const size_t index = static_cast<size_t>(
                        &parameter - method->params.data());
                    return !standard_error_variants(canonical_type_name(
                        parameter.type)).empty() &&
                        method_effect(*method, index) == Effect::Write;
                  });
          std::ostringstream bridge_before, bridge_after, rendered;
          rendered << (standard_write_bridge ? "__moss_bridge_receiver" :
              receiver_expression) << "." << viewed_method_name(
                  member_receiver, member_name, member_arguments, d, locals,
                  types) << "(";
          for (size_t index = 0; index < member_arguments.size(); ++index) {
            if (index) rendered << ", ";
            string argument = method_call_argument(*method, index,
                argument_temporaries[index].empty() ? member_arguments[index]
                                                    : argument_temporaries[index],
                d, locals, types);
            bool standard_write_argument = false;
            if (external_method_module && index < method->params.size() &&
                !standard_error_variants(canonical_type_name(
                    method->params[index].type)).empty()) {
              const string external_type = tooling_name("moss_" +
                  *external_method_module) + "::" +
                  canonical_type_name(method->params[index].type);
              const Effect effect = method_effect(*method, index);
              if (effect == Effect::Write) {
                standard_write_argument = true;
                const string prefix = "__moss_standard_write_" +
                    std::to_string(call_argument_temp_++);
                const string local_ref = prefix + "_local";
                const string provider_value = prefix + "_provider";
                bridge_before << "let " << local_ref << " = " << argument
                  << "; let mut " << provider_value << " = " << external_type
                  << "::from((*" << local_ref << ").clone()); ";
                bridge_after << "*" << local_ref << " = "
                  << canonical_type_name(method->params[index].type)
                  << "::from(" << provider_value << "); ";
                argument = "&mut " + provider_value;
              } else
                argument = effect == Effect::Read
                    ? "&" + external_type + "::from((*(" + argument + ")).clone())"
                    : external_type + "::from(" + argument + ")";
            }
            if (standard_write_bridge && !standard_write_argument) {
              const string staged = "__moss_call_argument_" +
                  std::to_string(call_argument_temp_++);
              bridge_before << "let " << staged << " = " << argument << "; ";
              argument = staged;
            }
            rendered << argument;
          }
          rendered << ")";
          string result = rendered.str();
          if (standard_write_bridge) {
            string receiver_binding = method->receiver_effect == Effect::Consume
                ? receiver_expression
                : (method->receiver_effect == Effect::Write ? "&mut (" :
                    "&(") + receiver_expression + ")";
            result = "{ let __moss_bridge_receiver = " + receiver_binding +
                "; " + bridge_before.str() + "let __moss_bridge_result = " +
                result + "; " + bridge_after.str() + "__moss_bridge_result }";
          }
          const string return_type = method->return_type.value_or("");
          const bool standard_return = external_method_module &&
              !standard_error_variants(canonical_type_name(return_type)).empty();
          if (standard_return)
            result = method->observable_effects.raise_set.empty()
                ? return_type + "::from(" + result + ")"
                : "(" + result + ").map(" + return_type + "::from)";
          if (!method->observable_effects.raise_set.empty()) {
            if (!phase21_result_context_)
              throw std::runtime_error(
                  "internal error: checked raising method call lacks a Phase 21 outcome context");
            result = phase21_propagate(result);
          }
          if (std::any_of(argument_temporaries.begin(),
                          argument_temporaries.end(),
                          [](const string& value) { return !value.empty(); })) {
            std::ostringstream sequenced;
            sequenced << "{ ";
            for (size_t index = 0; index < member_arguments.size(); ++index)
              if (!argument_temporaries[index].empty())
                sequenced << "let " << argument_temporaries[index] << " = "
                          << expr(member_arguments[index], d, locals, types)
                          << "; ";
            sequenced << result << " }";
            result = sequenced.str();
          }
          if (collection_named(parsed_type(concrete), {"vector", "queue"}) &&
              (member_name == "pop" || member_name == "pop_front"))
            result += ".unwrap_or_else(|| std::process::abort())";
          return result;
        }
        if (collection_named(parsed_type(concrete), {"vector", "queue"}) &&
            (member_name == "pop" || member_name == "pop_front") &&
            member_arguments.empty())
          return receiver_expression + "." +
              viewed_method_name(member_receiver, member_name, member_arguments,
                                 d, locals, types) +
              "().unwrap_or_else(|| std::process::abort())";
      }
      // Trait/duck-typed calls are already statically specialized by the
      // checker; retain their ordinary Rust method spelling when the concrete
      // method is not available to this expression pass.
      std::ostringstream rendered;
      rendered << receiver_expression << "." << viewed_method_name(member_receiver, member_name, member_arguments, d, locals, types) << "(";
      for (size_t index = 0; index < member_arguments.size(); ++index) {
        if (index) rendered << ", ";
        rendered << expr(member_arguments[index], d, locals, types);
      }
      rendered << ")";
      return rendered.str();
    }

    string builtin;
    vector<string> builtin_args;
    if (tree_parse_simple_call(e, builtin, builtin_args)) {
      if (builtin == "Some" && builtin_args.size() == 1)
        return "Some(" + expr(builtin_args.front(), d, locals, types) + ")";
      if (builtin == "sqrt" && builtin_args.size() == 1)
        return "{ let __moss_sqrt_value: f64 = " +
            expr(builtin_args.front(), d, locals, types) +
            "; __moss_sqrt_value.sqrt() }";
      if ((builtin == "is_nan" || builtin == "is_finite") &&
          builtin_args.size() == 1)
        return "{ let __moss_float_value: f64 = " +
            expr(builtin_args.front(), d, locals, types) +
            "; __moss_float_value." + builtin + "() }";
      const std::string_view numeric_name = numeric_builtin_base(builtin);
      if (!numeric_name.empty() && builtin_args.size() == 1) {
        if (!phase21_result_context_)
          throw std::runtime_error("internal error: numeric raise lacks outcome context");
        const string value = expr(builtin_args.front(), d, locals, types);
        const string error_type = numeric_builtin_error_type(builtin);
        const string invalid = numeric_name == "to_int" ? "NonFinite" : "Invalid";
        const string overflow = "Overflow";
        const string first_variant = phase21_rust_variant(
            RaisedIdentity::enum_variant(error_type, invalid));
        const string second_variant = phase21_rust_variant(
            RaisedIdentity::enum_variant(error_type, overflow));
        string call = "__moss_" + string(numeric_name) + "(" + value +
            (numeric_name == "to_int" ? "" : ".as_str()") + ")";
        string mapping = call + ".map_err(|__moss_numeric_error| "
            "if __moss_numeric_error == 0 { __MossRaised::" + first_variant +
            "(" + error_type + "::" + invalid + ") } else { __MossRaised::" +
            second_variant + "(" + error_type + "::" + overflow + ") })";
        if (numeric_name == "parse_float")
          mapping = call + ".map_err(|_| __MossRaised::" +
              first_variant + "(" + error_type + "::" + invalid + "))";
        return phase21_propagate(mapping);
      }
      if (builtin == "sum" && builtin_args.size() == 1) {
        auto argument_type = generated_expr_type(builtin_args.front(), types);
        string element_type;
        if (argument_type && starts_with(*argument_type, "vector[") &&
            ends_with(*argument_type, "]"))
          element_type = canonical_type_name(argument_type->substr(
              7, argument_type->size() - 8));
        else if (argument_type && starts_with(*argument_type, "seq[") &&
            ends_with(*argument_type, "]"))
          element_type = canonical_type_name(argument_type->substr(
              4, argument_type->size() - 5));
        string result = expr(builtin_args.front(), d, locals, types) +
            ".iter().copied()";
        if (element_type == "int")
          return result +
              ".fold(0_i64, |__moss_sum, __moss_value| "
              "__moss_sum.wrapping_add(__moss_value))";
        if (!element_type.empty())
          return result + ".sum::<" + rust_type(element_type) + ">()";
        return result + ".sum()";
      }
      if (builtin == "Char" && builtin_args.size() == 1 && !functions_.count(builtin)) {
        return "__moss_char_from_codepoint(" + expr(builtin_args.front(), d, locals, types) + ")";
      }
      if ((builtin == "less" || builtin == "isequal") && builtin_args.size() == 2 &&
          !functions_.count(builtin))
        return scalar_protocol_rust(
            builtin, generated_expr_type(builtin_args[0], types).value_or(""),
            expr(builtin_args[0], d, locals, types),
            expr(builtin_args[1], d, locals, types));
      if (builtin == "hash" && builtin_args.size() == 1 && !functions_.count(builtin))
        return scalar_protocol_rust(
            "hash", generated_expr_type(builtin_args[0], types).value_or(""),
            expr(builtin_args.front(), d, locals, types));
      if (builtin == "sort" && !functions_.count(builtin) &&
          (builtin_args.size() == 1 || builtin_args.size() == 2))
        return sort_call_rust(builtin_args, d, locals, types);
    }

    // Named value-object construction. Moss prefers `=` here; `:` remains a
    // compatibility spelling for existing sources.
    string head;
    vector<string> parts;
    if (tree_parse_simple_call(e, head, parts)) {
      auto oit = objects_.find(head);
      if (oit != objects_.end()) {
        std::unordered_map<string,string> values;
        if (parts.size() == 1 && parts[0].empty()) parts.clear();
        for (const auto& part : parts) {
          string field_name, field_value;
          if (!tree_parse_named_argument(part, field_name, field_value))
            throw std::runtime_error("object constructor fields must be named");
          values[field_name] = field_value;
        }
        std::ostringstream r;
        r << head << " { ";
        bool first = true;
        for (const auto& f : oit->second->fields) {
          auto vit = values.find(f.name);
          if (vit == values.end()) throw std::runtime_error("missing object constructor field: " + head + "." + f.name);
          string v = expr(vit->second, d, locals, types);
          if (!first) r << ", ";
          first = false;
          r << rust_identifier(f.name) << ": " << v;
        }
        r << " }";
        return r.str();
      }
      vector<string> call_args;
      if (tree_parse_simple_call(e, head, call_args)) {
        head = resolved_callable_identity(head, types);
        const Function* function_target = nullptr;
        if (starts_with(head, "callable-v1:")) {
          function_target = find_function_by_callable_id(head);
        } else {
          auto it = functions_.find(head);
          if (it != functions_.end()) function_target = it->second;
        }
        bool known_function = (function_target != nullptr);
        if (known_function)
          return render_known_function_call(*function_target, function_target->name, call_args,
                                            d, locals, types);
        const Method* sibling_method = nullptr;
        if (d && objects_.count(d->name) && !known_function) {
          vector<string> arg_types;
          for (const auto& argument : call_args)
            arg_types.push_back(nominalized_generated_argument_type(
                argument, generated_expr_type(argument, types).value_or("")));
          sibling_method = resolve_object_method(objects_, d->name, head, arg_types, true, nullptr);
        }
        vector<string> sibling_temporaries(call_args.size());
        if (sibling_method && sibling_method->receiver_effect != Effect::Read) {
          for (size_t i = 0; i < call_args.size(); ++i) {
            string nested_name; vector<string> nested_args;
            if (tree_parse_simple_call(call_args[i], nested_name, nested_args) &&
                objects_.count(d->name) &&
                resolve_object_method(objects_, d->name, nested_name,
                    [&]() {
                      vector<string> result;
                      for (const auto& argument : nested_args)
                        result.push_back(generated_expr_type(argument, types).value_or(""));
                      return result;
                    }(), true, nullptr))
              sibling_temporaries[i] = "__moss_call_argument_" +
                  std::to_string(call_argument_temp_++);
          }
        }
        std::ostringstream r; if (d && objects_.count(d->name) && !known_function) r << "self.";
        r << viewed_function_name(head, call_args, d, locals, types) << "(";
        size_t emitted_arguments = 0;
        for (size_t i = 0; i < call_args.size(); ++i) {
          bool compile_time_callable = known_function &&
              is_compile_time_callable(generated_expr_type(call_args[i], types).value_or(""));
          if (compile_time_callable) continue;
          if (emitted_arguments++) r << ", ";
          if (sibling_method)
            r << method_call_argument(*sibling_method, i,
                sibling_temporaries[i].empty() ? call_args[i]
                                               : sibling_temporaries[i],
                d, locals, types);
          else
            r << expr(call_args[i], d, locals, types);
        }
        r << ")";
        if (sibling_method &&
            !sibling_method->observable_effects.raise_set.empty()) {
          if (!phase21_result_context_)
            throw std::runtime_error(
                "internal error: checked raising sibling method call lacks a Phase 21 outcome context");
          const string propagated = phase21_propagate(r.str());
          r.str("");
          r.clear();
          r << propagated;
        }
        if (std::any_of(sibling_temporaries.begin(), sibling_temporaries.end(),
                        [](const string& value) { return !value.empty(); })) {
          std::ostringstream sequenced;
          sequenced << "{ ";
          for (size_t i = 0; i < call_args.size(); ++i)
            if (!sibling_temporaries[i].empty())
              sequenced << "let " << sibling_temporaries[i] << " = "
                        << expr(call_args[i], d, locals, types) << "; ";
          sequenced << r.str() << " }";
          return sequenced.str();
        }
        return r.str();
      }
    }

    // Expand borrowed access paths before the ordinary state identifier rewrite.
    if (view_domain_ || view_method_ || !view_parameters_.empty()) {
      string result;
      bool quoted = false, escaped = false;
      for (size_t i = 0; i < e.size();) {
        char ch = e[i];
        if (quoted) {
          result += ch; ++i;
          if (escaped) escaped = false;
          else if (ch == '\\') escaped = true;
          else if (ch == '\"') quoted = false;
        } else if (ch == '\"') { quoted = true; result += ch; ++i; }
        else if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
          size_t end = i + 1;
          while (end < e.size() && (std::isalnum(static_cast<unsigned char>(e[end])) || e[end] == '_' || e[end] == '.')) ++end;
          string path = e.substr(i, end - i);
          auto place = view_place(path, d, locals, types);
          result += place ? *place : path; i = end;
        } else { result += ch; ++i; }
      }
      e = result;
    }
    // Replace domain state identifiers with state.<name>, respecting basic identifier boundaries.
    if (d && !view_domain_ && !view_method_) {
      std::set<string> fields;
      for (const auto& f : d->state) fields.insert(f.name);
      for (const auto& route : d->routes) fields.insert(route.name);
      string out;
      bool in_str = false, esc = false;
      for (size_t i = 0; i < e.size();) {
        char c = e[i];
        if (in_str) {
          out.push_back(c);
          if (esc) esc = false;
          else if (c == '\\') esc = true;
          else if (c == '"') in_str = false;
          ++i;
          continue;
        }
        if (c == '"') { in_str = true; out.push_back(c); ++i; continue; }
        if (std::isalpha((unsigned char)c) || c == '_') {
          size_t j = i + 1;
          while (j < e.size() && (std::isalnum((unsigned char)e[j]) || e[j] == '_')) ++j;
          string tok = e.substr(i, j-i);
          // If already preceded by '.', it is a member name, not state identifier.
          bool member = i > 0 && e[i-1] == '.';
          if (!member && fields.count(tok) && !locals.count(tok))
            out += (objects_.count(d->name) ? "self." : "state.") + tok;
          else out += tok;
          i = j;
        } else { out.push_back(c); ++i; }
      }
      e = out;
      if (objects_.count(d->name)) {
        for (const auto& f : d->state) if (canonical_type_name(f.type) == "float") {
          for (const auto& op : {string(" > "), string(" < "), string(" >= "), string(" <= "), string(" == "), string(" != ")}) {
            string needle = "self." + f.name + op + "0";
            auto pos = e.find(needle);
            if (pos != string::npos) e.replace(pos, needle.size(), "self." + f.name + op + "0.0");
          }
        }
      }
    }
    for (const auto& parameter : write_through_parameters_)
      e = replace_unqualified_word(e, parameter, "(*" + parameter + ")");
    // Nim-ish boolean words.
    replace_word(e, "and", "&&");
    replace_word(e, "xor", "^");
    replace_word(e, "or", "||");
    replace_word(e, "not", "!");
    // Escape Rust-reserved member names without changing the Moss identifier.
    for (size_t pos = 0; pos + 1 < e.size();) {
      if (e[pos] != '.') { ++pos; continue; }
      size_t begin = pos + 1, end = begin;
      while (end < e.size() &&
             (std::isalnum(static_cast<unsigned char>(e[end])) || e[end] == '_'))
        ++end;
      if (end > begin) {
        string member = e.substr(begin, end - begin);
        string escaped = rust_identifier(member);
        if (escaped != member) {
          e.replace(begin, member.size(), escaped);
          end += escaped.size() - member.size();
        }
      }
      pos = end;
    }
    return e;
  }

  static void replace_word(string& s, const string& from, const string& to) {
    string out;
    for (size_t i = 0; i < s.size();) {
      if (i + from.size() <= s.size() && s.compare(i, from.size(), from) == 0 &&
          (i == 0 || !(std::isalnum((unsigned char)s[i-1]) || s[i-1] == '_')) &&
          (i+from.size() == s.size() || !(std::isalnum((unsigned char)s[i+from.size()]) || s[i+from.size()] == '_'))) {
        out += to; i += from.size();
      } else out.push_back(s[i++]);
    }
    s.swap(out);
  }

  static string replace_unqualified_word(const string& expression,
                                         const string& from, const string& to) {
    string out;
    for (size_t index = 0; index < expression.size();) {
      if (!(std::isalpha(static_cast<unsigned char>(expression[index])) ||
            expression[index] == '_')) {
        out.push_back(expression[index++]);
        continue;
      }
      size_t start = index;
      size_t end = index + 1;
      while (end < expression.size() &&
             (std::isalnum(static_cast<unsigned char>(expression[end])) ||
              expression[end] == '_'))
        ++end;
      size_t previous = start;
      while (previous > 0 &&
             std::isspace(static_cast<unsigned char>(expression[previous - 1])))
        --previous;
      if (expression.substr(start, end - start) == from &&
          (previous == 0 || expression[previous - 1] != '.'))
        out += to;
      else
        out.append(expression, start, end - start);
      index = end;
    }
    return out;
  }

  static string fresh_generated_name(const string& base, std::set<string>& used) {
    string candidate = base;
    for (size_t suffix = 0; used.count(candidate); ++suffix)
      candidate = base + "_" + std::to_string(suffix);
    used.insert(candidate);
    return candidate;
  }

  // Message values have one Moss-level rule (an immutable value snapshot),
  // but two backend representations.  The owned form is retained at an ABI
  // boundary and for replies.  The borrowed forms are valid only while an
  // internal synchronous `_shared` call is active; Handler2PL retains every
  // guard needed by a state view until that nested call returns.
  enum class MessagePayloadLowering {
    CopyValue,
    BorrowOwned,
    BorrowView,
    MaterializeOwned,
  };

  bool borrowed_message_parameter(const string& expression) const {
    return tree_plain_identifier(trim(expression)) &&
        borrowed_message_parameters_.count(trim(expression));
  }

  bool borrowed_message_type(const string& type) const {
    const string concrete = canonical_type_name(type);
    return !concrete.empty() && !copy_type(concrete) && concrete != "_";
  }

  string shared_message_parameter(const Param& parameter) const {
    const string type = canonical_type_name(parameter.type);
    if (!borrowed_message_type(type))
      return parameter.name + ": " + rust_type(type);
    // A view is a static access capability. The internal ABI receives an
    // immutable reference whether this is an owned object, a temporary state
    // projection, or an already-borrowed incoming payload.
    if (view_object_type(type))
      return parameter.name + ": &impl " + access_trait(type);
    return parameter.name + ": &" + rust_type(type);
  }

  string shared_message_forward_argument(const Param& parameter,
                                         bool from_owned_boundary) const {
    if (!borrowed_message_type(parameter.type)) return parameter.name;
    // An exported/native boundary owns its Rust argument.  It deliberately
    // materializes that boundary, then lends it to the controlled internal
    // call.  `_shared` forwarding already receives the required borrowed or
    // access-view representation.
    return from_owned_boundary ? "&" + parameter.name : parameter.name;
  }

  MessagePayloadLowering message_payload_lowering(
      const string& expression, const string& type, const Domain* d,
      const std::set<string>& locals,
      const std::unordered_map<string,string>* types,
      bool owned_boundary) const {
    if (owned_boundary) return MessagePayloadLowering::MaterializeOwned;
    if (!borrowed_message_type(type)) return MessagePayloadLowering::CopyValue;
    if (is_object_view(expression, d, locals, types))
      return MessagePayloadLowering::BorrowView;
    return MessagePayloadLowering::BorrowOwned;
  }

  string message_arg(const string& e, const string& type, const Domain* d,
                     const std::set<string>& locals,
                     const std::unordered_map<string,string>* types = nullptr,
                     size_t functional_pipeline_id = 0) const {
    string r = nominal_domain_handle_argument(e, type, d, locals, types, functional_pipeline_id);
    string t = trim(type);
    if (is_object_view(e, d, locals, types)) return "(" + r + ").__moss_value()";
    if (copy_type(t) || t == "_") return r;
    // Messages are the explicit Moss semantic copy boundary.  A payload is
    // detached from the sender even when the source binding remains available;
    // the generated clone is an implementation of that boundary, never an
    // implicit copy for an ordinary local call.
    return "(" + r + ").clone()";
  }

  string shared_message_arg(const string& e, const string& type,
                            const Domain* d, const std::set<string>& locals,
                            const std::unordered_map<string,string>* types = nullptr,
                            size_t functional_pipeline_id = 0) const {
    const auto lowering = message_payload_lowering(
        e, type, d, locals, types, false);
    string rendered = nominal_domain_handle_argument(
        e, type, d, locals, types, functional_pipeline_id);
    switch (lowering) {
      case MessagePayloadLowering::CopyValue:
        return rendered;
      case MessagePayloadLowering::BorrowOwned:
        // A temporary owner is naturally extended through this synchronous
        // call.  No clone implements the Moss snapshot here.
        return borrowed_message_parameter(e) ? trim(e) : "&(" + rendered + ")";
      case MessagePayloadLowering::BorrowView:
        // An incoming object payload is already `&impl MossAccess_T` and can
        // be forwarded directly. State projections are compact values holding
        // field borrows, so lend the temporary view for this nested call.
        return borrowed_message_parameter(e) ? trim(e) : "&(" + rendered + ")";
      case MessagePayloadLowering::MaterializeOwned:
        break;
    }
    throw std::runtime_error("internal error: shared message selected owned boundary");
  }

  string value_from_borrowed_message_parameter(
      const string& source, const string& rendered,
      const std::unordered_map<string,string>* types) const {
    auto type = generated_expr_type(source, types);
    if (borrowed_message_parameter(source) && type &&
        !view_object_type(*type))
      return "*(" + rendered + ")";
    return rendered;
  }

  void gen_enum(std::ostringstream& o, const EnumType& declaration) {
    string enum_identity = "enum:" + declaration.name + "@" +
        std::to_string(declaration.line);
    tooling_begin(o, 0, "enum", enum_identity, declaration.name);
    source_comment(o, 0, declaration.line, declaration.header);
    o << "#[derive(Clone, Debug)]\n"
      << ((declaration.exported || p_.explicit_module) ? "pub " : "")
      << "enum " << declaration.name << " {\n";
    for (const auto& item : declaration.cases) {
      string case_identity = "enum_case:" + declaration.name + "." +
          item.name + "@" + std::to_string(item.line);
      tooling_begin(o, 4, "enum_case", case_identity,
                    declaration.name + "::" + rust_identifier(item.name));
      source_comment(o, 4, item.line, item.name);
      o << "    " << rust_identifier(item.name);
      if (!item.fields.empty()) {
        o << " { ";
        for (size_t index = 0; index < item.fields.size(); ++index) {
          if (index) o << ", ";
          o << rust_identifier(item.fields[index].name) << ": "
            << rust_type(item.fields[index].type);
        }
        o << " }";
      }
      o << ",\n";
      tooling_end(o, 4, case_identity);
    }
    o << "}\n\n";
    o << "impl std::fmt::Display for " << declaration.name << " {\n"
      << "    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {\n"
      << "        match self {\n";
    for (const auto& item : declaration.cases) {
      o << "            " << declaration.name << "::"
        << rust_identifier(item.name);
      if (!item.fields.empty()) {
        o << " { ";
        for (size_t index = 0; index < item.fields.size(); ++index) {
          if (index) o << ", ";
          o << rust_identifier(item.fields[index].name);
        }
        o << " }";
      }
      o << " => write!(formatter, \"" << declaration.name << "."
        << item.name;
      if (!item.fields.empty()) {
        o << "(";
        for (size_t index = 0; index < item.fields.size(); ++index) {
          if (index) o << ", ";
          const string field_type = canonical_type_name(item.fields[index].type);
          const bool display_field = field_type == "int" ||
              field_type == "float" || field_type == "bool" ||
              field_type == "string" || enums_.count(field_type);
          o << item.fields[index].name
            << (display_field ? ": {}" : ": {:?}");
        }
        o << ")\"";
        for (const auto& field : item.fields)
          o << ", " << rust_identifier(field.name);
        o << "),\n";
      } else {
        o << "\"),\n";
      }
    }
    o << "        }\n"
      << "    }\n"
      << "}\n\n";
    tooling_end(o, 0, enum_identity);
  }

  void gen_object(std::ostringstream& o, const ObjectType& t) {
    bool is_generic = std::any_of(t.fields.begin(), t.fields.end(),
        [](const Field& f) { return f.inferred || f.type.empty(); });
    string type_identity = "type:" + t.name + "@" + std::to_string(t.line);
    tooling_begin(o, 0, "type", type_identity, t.name);
    source_comment(o, 0, t.line, t.header.empty() ? "type " + t.name : t.header);
    backend_comment(o, 0, "value representation for the Moss object; object ownership stays in Moss");

    string type_params;
    if (is_generic) {
      type_params += "<";
      size_t gi = 0;
      for (const auto& f : t.fields) {
        if (f.inferred || f.type.empty()) {
          if (gi > 0) type_params += ", ";
          type_params += "F" + std::to_string(gi);
          gi++;
        }
      }
      type_params += ">";
    }

    o << "#[derive(Clone, Debug)]\n"
      << ((t.exported || p_.explicit_module) ? "pub " : "") << "struct " << t.name << type_params << " {\n";
    size_t gi = 0;
    for (const auto& f : t.fields) {
      source_comment(o, 4, f.line, f.header.empty() ? f.name + ": " + f.type : f.header);
      string ftype;
      if (f.inferred || f.type.empty()) {
        ftype = "F" + std::to_string(gi++);
      } else {
        ftype = rust_type(f.type);
      }
      o << "    " << ((t.exported || p_.explicit_module) ? "pub " : "")
        << rust_identifier(f.name) << ": " << ftype << ",\n";
    }
    o << "}\n\n";
    tooling_end(o, 0, type_identity);
    // Native provider storage decomposition moves fields without exposing Moss
    // field access or requiring the consumer to read Rust-private fields.
    if (is_generic) {
      o << "impl" << type_params << " " << t.name << type_params << " {\n    "
        << ((t.exported || p_.explicit_module) ? "pub " : "") << "fn __moss_into_parts(self) -> (";
      gi = 0;
      for (const auto& field : t.fields) {
        if (field.inferred || field.type.empty()) o << "F" << std::to_string(gi++) << ",";
        else o << rust_type(field.type) << ",";
      }
      o << ") { (";
      for (const auto& field : t.fields) o << "self." << rust_identifier(field.name) << ",";
      o << ") }\n}\n";
    } else {
      o << "impl " << t.name << " {\n    " << ((t.exported || p_.explicit_module) ? "pub " : "") << "fn __moss_into_parts(self) -> (";
      for (const auto& field : t.fields) o << rust_type(field.type) << ",";
      o << ") { (";
      for (const auto& field : t.fields) o << "self." << rust_identifier(field.name) << ",";
      o << ") }\n}\n";
    }
    for (const auto& method : t.methods) for (bool view : {false, true}) {
      bool needs_view = false;
      for (size_t i = 0; i < method.params.size(); ++i)
        needs_view |= method_effect(method, i) != Effect::Consume && view_object_type(method.params[i].type);
      if (view && !needs_view) continue;
      const string method_name = (view ? "__moss_view_" : "") + method.name;
      string semantic_identity = method_semantic_identity(t, method);
      string generated_symbol = t.name + "::" + method_name;
      string native_symbol = tooling_native_symbol(
          "method", t.name + "__" + method.name, semantic_identity);
      tooling_begin(o, 0, "method", semantic_identity, generated_symbol,
                    view ? "" : native_symbol);
      o << "impl" << (is_generic ? type_params : "") << " " << t.name << (is_generic ? type_params : "") << " {\n";
      source_comment(o, 4, method.line,
                     "fn " + method.name);
      debug_symbol_attributes(o, 4, native_symbol, !view);
      const bool raises = !method.observable_effects.raise_set.empty();
      if (raises) o << "    #[allow(unreachable_code)]\n";
      o << "    " << ((t.exported || p_.explicit_module) ? "pub " : "")
        << "fn " << method_name;
      if (method.receiver_effect == Effect::Consume) o << "(self";
      else if (method.receiver_effect == Effect::Write) o << "(&mut self";
      else o << "(&self";
      for (const auto& p : method.params) {
        if (p.type.empty()) throw std::runtime_error("unresolved concrete method parameter type: " + method.name + "." + p.name);
        size_t parameter_index = static_cast<size_t>(&p - method.params.data());
        Effect effect = method_effect(method, parameter_index);
        string parameter_type = view && effect != Effect::Consume && view_object_type(p.type)
            ? "impl " + access_trait(p.type) : rust_type(p.type);
        if (is_generic && p.type == t.name) {
          parameter_type = t.name + type_params;
        }
        if (effect != Effect::Consume && (effect == Effect::Write || borrowable_type(p.type))) {
          o << ", " << (effect == Effect::Write ? "" : "") << p.name << ": "
            << (effect == Effect::Write ? "&mut " : "&") << parameter_type;
        } else {
          o << ", mut " << p.name << ": " << parameter_type;
        }
      }
      if (method.return_type) {
        o << ") -> ";
        if (raises) o << "Result<";
        string ret_type = rust_type(*method.return_type);
        if (is_generic && *method.return_type == t.name) {
          ret_type = t.name + type_params;
        }
        o << ret_type;
        if (raises) o << ", __MossRaised>";
        o << " {\n";
      } else {
        o << ")";
        if (raises) o << " -> Result<(), __MossRaised>";
        o << " {\n";
      }
      write_through_parameters_.clear(); view_parameters_.clear();
      for (size_t index = 0; index < method.params.size(); ++index) {
        if (view && method_effect(method, index) != Effect::Consume && view_object_type(method.params[index].type))
          view_parameters_.insert(method.params[index].name);
        else if (method_effect(method, index) == Effect::Write)
          write_through_parameters_.insert(method.params[index].name);
      }
      Domain receiver;
      receiver.name = t.name;
      receiver.state = t.fields;
      std::set<string> locals;
      std::unordered_map<string,string> types{{"self", t.name}};
      for (const auto& field : t.fields) types[field.name] = field.type;
      for (const auto& p : method.params) { locals.insert(p.name); types[p.name] = p.type; }
      const bool saved_phase21_result_context = phase21_result_context_;
      phase21_result_context_ = raises;
      gen_stmts(o, method.body, &receiver, nullptr, "", locals, types, 2, false, true, functional_method_context(t, method));
      if (method.result_expression) {
        string context = functional_method_context(t, method);
        auto plan = method.result_functional_pipeline_ids.find(context);
        FunctionalExpressionHandlesScope result_handles_scope(
            active_functional_expression_handles_,
            &method.expression_handles);
        source_comment(o, 8,
                       method.result_line ? method.result_line : method.line,
                       *method.result_expression);
        o << "        ";
        if (raises) o << "Ok(";
        o << expr(*method.result_expression, &receiver, locals, &types,
                  plan == method.result_functional_pipeline_ids.end()
                      ? 0 : plan->second)
          ;
        if (raises) o << ")";
        o << "\n";
      } else if (raises && !method.return_type) {
        o << "        Ok(())\n";
      } else if (method.return_type &&
                 canonical_type_name(*method.return_type) != "unit") {
        // The checker requires every source path to return. Rust does not
        // infer that a checked try's Ok arm is unreachable after `return`.
        o << "        #[allow(unreachable_code)]\n"
          << "        { std::process::abort() }\n";
      }
      phase21_result_context_ = saved_phase21_result_context;
      o << "    }\n";
      o << "}\n\n";
      tooling_end(o, 0, semantic_identity);
      write_through_parameters_.clear(); view_parameters_.clear();
    }
  }

  void gen_function_instance(std::ostringstream& o, const Function& f,
                             const FunctionSpecialization* specialization, bool view = false) {
    struct ActiveSpecializationScope {
      string& slot;
      string prior;
      ActiveSpecializationScope(string& target,
                                const FunctionSpecialization* current)
          : slot(target), prior(target) {
        slot = current ? current->stable_type_key : string();
      }
      ~ActiveSpecializationScope() { slot = std::move(prior); }
    } active_specialization_scope(codegen_active_specialization_key_,
                                  specialization);
    borrowed_function_parameters_.clear();
    bool container_generic = !specialization &&
        std::any_of(f.params.begin(), f.params.end(), [](const Param& p) {
          return p.type == "vector" || p.type == "queue" || p.type == "map";
        });
    string semantic_identity = function_semantic_identity(f, specialization);
    string generated_symbol = specialization ? specialization->generated_name : f.name;
    if (view) generated_symbol = "__moss_view_" + generated_symbol;
    // A specialization can be projected into distinct provider/consumer rlibs.
    // Preserve native debug-map symbols, but scope them to the emitting module
    // artifact so those otherwise-identical semantic projections cannot collide
    // An open result stays a Rust type parameter even in a static
    // specialization (e.g. a consumer's `Provider.fresh()`): each Moss call
    // site binds it independently, exactly as for a source-backed `fresh()`.
    const string open_result_type = specialization
        ? specialization->return_type : f.return_type.value_or("unit");
    bool open_return = open_result_type == "vector" ||
        open_result_type == "queue" || open_result_type == "map";
    bool can_export = !view && !open_return &&
        (specialization || (!f.generic && !container_generic));
    string native_identity = semantic_identity;
    if (specialization && p_.explicit_module && !p_.module_name.empty())
      native_identity += "@artifact:" + p_.module_name;
    string native_symbol = tooling_native_symbol(
        "function", f.name, native_identity);
    tooling_begin(o, 0, "function", semantic_identity, generated_symbol,
                  can_export ? native_symbol : "");
    source_comment(o, 0, f.line, f.header.empty() ? "fn " + f.name : f.header);
    backend_comment(o, 0, specialization
        ? "STATIC specialization: concrete local function selected before Rust generation"
        : "LOCAL function: ordinary intra-domain call; no domain dispatch");
    debug_symbol_attributes(o, 0, native_symbol, can_export);
    const bool raises = !(specialization ? specialization->observable_effects
                                       : f.observable_effects).raise_set.empty();
    if (raises)
      o << "#[allow(unreachable_code)]\n";
    bool materialized_export = f.exported && !specialization &&
        !f.generic && !container_generic && !open_return;
    o << ((materialized_export || (specialization && public_specializations_)) ? "pub " : "")
      << "fn " << generated_symbol;
    if (open_return || (!specialization && (f.generic || container_generic))) {
      o << "<";
      size_t gi = 0;
      for (const auto& p : f.params)
        if (!specialization &&
            (constraint_ops(f, p.name).size() || p.type == "vector" || p.type == "queue")) {
          if (gi++) o << ", ";
          o << "T_" << p.name;
        }
      if (open_return) {
        if (gi++) o << ", ";
        if (*f.return_type == "map") o << "K, V";
        else o << "T";
      }
      o << ">";
    }
    o << "(";
    size_t emitted_parameters = 0;
    for (size_t index = 0; index < f.params.size(); ++index) {
      string pt = specialization ? specialization->parameter_types[index]
                                 : f.params[index].type;
      // A callable parameter in a static higher-order specialization is a
      // compile-time identity, not a Rust function value.
      if (specialization && callable_identity(pt)) continue;
      if (emitted_parameters++) o << ", ";
      auto ops = constraint_ops(f, f.params[index].name);
      string parameter_rust_type;
      if (!specialization && pt.empty() && !ops.empty()) {
        if (ops.count("[]")) parameter_rust_type = "std::vec::Vec<T_" + f.params[index].name + ">";
        else parameter_rust_type = "T_" + f.params[index].name;
      } else if (!specialization && pt == "vector") {
        parameter_rust_type = "std::vec::Vec<T_" + f.params[index].name + ">";
      } else if (!specialization && pt == "queue") {
        parameter_rust_type = "std::collections::VecDeque<T_" + f.params[index].name + ">";
      } else if (!specialization && pt == "map") {
        parameter_rust_type = "std::collections::HashMap<K_" + f.params[index].name + ", V_" + f.params[index].name + ">";
      } else {
        parameter_rust_type = rust_type(pt);
      }
      auto abi = function_parameter_abi(f, index, pt, specialization);
      Effect effect = abi.effect;
      if (view && effect != Effect::Consume && view_object_type(pt)) {
        o << f.params[index].name << ": " << (effect == Effect::Write ? "&mut " : "&") << "impl " << access_trait(pt);
        continue;
      }
      bool borrow = abi.passing != ParameterPassingMode::Value;
      if (borrow && !copy_type(pt))
        borrowed_function_parameters_.insert(f.params[index].name);
      bool mutates_owned_parameter = effect == Effect::Consume && !view &&
          (specialization ? index < specialization->parameter_mutations.size() &&
              specialization->parameter_mutations[index] :
              index < f.parameter_mutations.size() && f.parameter_mutations[index]);
      o << ((borrow && effect == Effect::Write) || mutates_owned_parameter ? "mut " : "")
        << f.params[index].name << ": "
        << (borrow ? (effect == Effect::Write ? "&mut " : "&") : "")
        << parameter_rust_type;
    }
    string return_type = specialization ? specialization->return_type
                                        : f.return_type.value_or("unit");
    if (return_type != "unit") {
      string rr = return_type;
      if (!specialization && open_generic(rr)) rr = "T_" + rr.substr(9);
      else if (!specialization && starts_with(rr, "_element:_element:")) rr = "T_" + rr.substr(17);
      else if (!specialization && starts_with(rr, "_element:element:")) rr = "T_" + rr.substr(17);
      o << ") -> ";
      if (raises) o << "Result<";
      o << (rr == return_type ? rust_type(rr) : rr);
      if (raises) o << ", __MossRaised>";
      if (!specialization && (f.generic || container_generic || open_return)) {
        std::ostringstream where_clause;
        size_t bi = 0;
        for (const auto& p : f.params) {
          auto ops = constraint_ops(f, p.name);
          if (ops.empty() && p.type != "vector" && p.type != "queue") continue;
          if (bi++) where_clause << ", ";
          where_clause << "T_" << p.name << ": ";
          bool idx = ops.count("[]");
          if (idx) where_clause << "Clone";
          else {
            string op = ops.count("+") ? "Add" : ops.count("*") ? "Mul" : ops.count("-") ? "Sub" : "Div";
            where_clause << "std::ops::" << op << "<Output = T_" << p.name << "> + Copy";
          }
        }
        if (bi > 0) o << " where " << where_clause.str();
      }
      o << " {\n";
    }
    else {
      o << ")";
      if (raises) o << " -> Result<(), __MossRaised>";
      o << " {\n";
    }
    std::set<string> locals;
    std::unordered_map<string,string> types;
    for (size_t index = 0; index < f.params.size(); ++index) {
      locals.insert(f.params[index].name);
      types[f.params[index].name] = specialization
          ? specialization->parameter_types[index] : f.params[index].type;
    }
    write_through_parameters_.clear(); view_parameters_.clear();
    for (size_t index = 0; index < f.params.size(); ++index) {
      if (view && function_effect(f, index, types.at(f.params[index].name), specialization) != Effect::Consume && view_object_type(types.at(f.params[index].name)))
        view_parameters_.insert(f.params[index].name);
      else if (function_effect(f, index, types.at(f.params[index].name), specialization) == Effect::Write)
        write_through_parameters_.insert(f.params[index].name);
    }
    string functional_context = functional_function_context(f, specialization);
    const bool saved_phase21_result_context = phase21_result_context_;
    phase21_result_context_ = raises;
    gen_stmts(o, f.body, nullptr, nullptr, "", locals, types, 1, false, true, functional_context);
    if (f.result_expression) {
      struct ResultCodegenScope {
        const FunctionSpecialization*& specialization_slot;
        string& expression_slot;
        const FunctionSpecialization* prior_specialization;
        string prior_expression;
        ResultCodegenScope(const FunctionSpecialization*& selected,
                           string& source, const FunctionSpecialization* current,
                           const string& expression)
            : specialization_slot(selected), expression_slot(source),
              prior_specialization(selected), prior_expression(source) {
          specialization_slot = current;
          expression_slot = expression;
        }
        ~ResultCodegenScope() {
          specialization_slot = prior_specialization;
          expression_slot = std::move(prior_expression);
        }
      } result_scope(codegen_result_specialization_,
                     codegen_result_expression_, specialization,
                     *f.result_expression);
      FunctionalExpressionHandlesScope result_handles_scope(
          active_functional_expression_handles_, &f.expression_handles);
      source_comment(o, 4, f.result_line, *f.result_expression);
      auto plan = f.result_functional_pipeline_ids.find(functional_context);
      string rendered = expr(*f.result_expression, nullptr, locals, &types,
                            plan == f.result_functional_pipeline_ids.end()
                                ? 0 : plan->second);
      if (view_object_type(return_type)) {
        string expr_trimmed = trim(*f.result_expression);
        auto param_it = std::find_if(f.params.begin(), f.params.end(),
                                     [&](const Param& p) { return p.name == expr_trimmed; });
        if (param_it != f.params.end()) {
          size_t p_idx = std::distance(f.params.begin(), param_it);
          if (function_effect(f, p_idx, types.at(f.params[p_idx].name), specialization) != Effect::Consume) {
            if (view) rendered = expr_trimmed + ".__moss_value()";
            else rendered = "(" + rendered + ").clone()";
          }
        }
      }
      o << "    ";
      if (raises) o << "Ok(";
      o << rendered;
      if (raises) o << ")";
      o << "\n";
    } else if (raises && return_type == "unit") {
      o << "    Ok(())\n";
    } else if (return_type != "unit") {
      // The checker requires every source path to return. Keep a diverging
      // native fallback for a try whose normal outcome is unreachable.
      o << "    #[allow(unreachable_code)]\n"
        << "    { std::process::abort() }\n";
    }
    phase21_result_context_ = saved_phase21_result_context;
    o << "}\n\n";
    tooling_end(o, 0, semantic_identity);
    write_through_parameters_.clear(); view_parameters_.clear();
    borrowed_function_parameters_.clear();
    bool needs_view = false;
    for (size_t i = 0; i < f.params.size(); ++i)
      needs_view |= function_effect(f, i, types.at(f.params[i].name), specialization) != Effect::Consume &&
          view_object_type(specialization ? specialization->parameter_types[i] : f.params[i].type);
    if (!view && needs_view) gen_function_instance(o, f, specialization, true);
  }

  void gen_function(std::ostringstream& o, const Function& f) {
    if (f.static_dispatch) {
      if (!emit_static_specializations_) return;
      for (const auto& specialization : f.specializations)
        gen_function_instance(o, f, &specialization);
      return;
    }
    gen_function_instance(o, f, nullptr);
  }

  void gen_ref_decl(std::ostringstream& o, const Domain& d) {
    o << "#[derive(Clone)]\n" << (d.exported ? "pub " : "") << "struct " << d.name << "Ref {\n"
      << "    state: std::sync::Arc<" << d.name << "Runtime>,\n";
    for (const auto& route : d.routes)
      o << "    " << route.name << ": " << rust_type(route.type) << ",\n";
    o << "}\n\n";
  }

  static string reply_binding(const Domain& d, const Handler& h) {
    std::set<string> used;
    for (const auto& f : d.state) used.insert(f.name);
    for (const auto& p : h.params) used.insert(p.name);
    for (const auto& s : h.body) {
      if (s.kind == Stmt::Kind::Let || s.kind == Stmt::Kind::Var)
        used.insert(s.a);
    }
    string name = "__moss_reply";
    for (size_t suffix = 0; used.count(name); ++suffix)
      name = "__moss_reply_" + std::to_string(suffix);
    return name;
  }

  static bool handler_needs_label(const Handler& h) {
    return std::any_of(h.body.begin(), h.body.end(), [](const Stmt& s) {
      return s.kind == Stmt::Kind::Return || s.kind == Stmt::Kind::Reply;
    });
  }

  string handler_internal_return_type(const Handler& handler) const {
    string normal = handler.reply_type
        ? "Option<" + rust_type(*handler.reply_type) + ">"
        : "()";
    return handler.observable_effects.raise_set.empty()
        ? normal
        : "Result<" + normal + ", " +
            (handler_has_failure_captures(handler)
                ? handler_capture_carrier_type(handler)
                : "__MossRaised") + ">";
  }

  string handler_message_return_type(const Handler& handler) const {
    string normal = rust_type(handler.reply_type.value_or("unit"));
    return handler.observable_effects.raise_set.empty()
        ? normal
        : "Result<" + normal + ", __MossRaised>";
  }

#include "handler_lowering.inc"
#include "borrowed_views.inc"
#include "executor_invoke_codegen.inc"
#include "file_chunk_codegen.inc"

  static string nominal_handle_variant(const string& domain) {
    return "Specialized_" + stable_hash(domain).substr(0, 12);
  }

  void gen_nominal_handle(std::ostringstream& o, const Domain& source) {
    if (!has_domain_specializations(source.name)) return;
    vector<const Domain*> routes{&source};
    for (const auto& specialization : p_.domain_specializations) {
      if (specialization.source_domain != source.name) continue;
      auto generated = specialization_names_.find(
          specialization.source_domain + "\n" + specialization.instance);
      if (generated == specialization_names_.end()) continue;
      auto specialized = std::find_if(
          specialized_domains_.begin(), specialized_domains_.end(),
          [&](const Domain& domain) { return domain.name == generated->second; });
      if (specialized != specialized_domains_.end()) routes.push_back(&*specialized);
    }
    // The route list above is keyed by semantic specialization records. It
    // deliberately does not infer membership from generated Rust names: a
    // source domain may collide with a would-be specialized layout spelling.
    if (routes.size() < 2) return;

    const string handle = source.name + "Handle";
    const string route = handle + "Route";
    o << "// Moss backend: nominal " << source.name
      << " handle routes statically to each materialized instance\n";
    o << "#[derive(Clone)]\n" << "enum " << route << " {\n";
    o << "    Base(" << source.name << "Ref),\n";
    for (size_t index = 1; index < routes.size(); ++index)
      o << "    " << nominal_handle_variant(routes[index]->name) << "("
        << routes[index]->name << "Ref),\n";
    o << "}\n";
    o << "#[derive(Clone)]\n" << (source.exported ? "pub " : "")
      << "struct " << handle << " { route: " << route << " }\n\n";
    o << "impl From<" << source.name << "Ref> for " << handle << " {\n"
      << "    fn from(value: " << source.name << "Ref) -> Self { Self { route: "
      << route << "::Base(value) } }\n}\n";
    for (size_t index = 1; index < routes.size(); ++index) {
      o << "impl From<" << routes[index]->name << "Ref> for " << handle << " {\n"
        << "    fn from(value: " << routes[index]->name << "Ref) -> Self { Self { route: "
        << route << "::" << nominal_handle_variant(routes[index]->name)
        << "(value) } }\n}\n";
    }
    o << "\nimpl " << handle << " {\n";
    for (const auto& handler : source.handlers) {
      const Handler* route_handler = find_handler(*routes.front(), handler.name);
      if (!route_handler) continue;
      bool compatible = true;
      for (size_t index = 0; index < handler.params.size(); ++index) {
        string expected = rust_type(handler.params[index].type);
        for (size_t route_index = 1; route_index < routes.size(); ++route_index) {
          const Handler* candidate = find_handler(*routes[route_index], handler.name);
          if (!candidate || index >= candidate->params.size() ||
              rust_type(candidate->params[index].type) != expected)
            compatible = false;
        }
      }
      for (size_t route_index = 1; route_index < routes.size(); ++route_index) {
        const Handler* candidate = find_handler(*routes[route_index], handler.name);
        if (!candidate || candidate->reply_type.has_value() !=
                handler.reply_type.has_value() ||
            (candidate->reply_type && handler.reply_type &&
             rust_type(*candidate->reply_type) != rust_type(*handler.reply_type)))
          compatible = false;
      }
      if (!compatible) continue;
      if (source.exported) {
        o << "    pub fn __moss_message_" << handler.name << "(&self";
        string arguments;
        for (const auto& parameter : handler.params) {
          o << ", " << parameter.name << ": " << rust_type(parameter.type);
          if (!arguments.empty()) arguments += ", ";
          arguments += parameter.name;
        }
        o << ") -> " << handler_message_return_type(handler)
          << " {\n        match &self.route {\n";
        for (size_t index = 0; index < routes.size(); ++index)
          o << "            " << route << "::"
            << (index == 0 ? "Base" : nominal_handle_variant(routes[index]->name))
            << "(inner) => inner.__moss_message_" << handler.name
            << "(" << arguments << "),\n";
        o << "        }\n    }\n";
      }

        o << "    fn " << handler.name << "_shared(&self";
        for (const auto& parameter : handler.params)
          o << ", " << shared_message_parameter(parameter);
        o << ") -> " << handler_internal_return_type(handler)
          << " {\n        match &self.route {\n";
        for (size_t index = 0; index < routes.size(); ++index) {
          o << "            " << route << "::" << (index == 0 ? "Base" : nominal_handle_variant(routes[index]->name))
            << "(inner) => inner." << handler.name << "_shared(";
          for (const auto& parameter : handler.params) o << parameter.name << ", ";
          o << "),\n";
        }
        o << "        }\n    }\n";
          }
    o << "}\n\n";
  }

  // Expose a synchronous provider entry over its private state representation.
  // The target is fixed by the checked concrete route graph.
  void gen_exported_domain_bridge(std::ostringstream& out, const Domain& domain) {
    out << "impl " << domain.name << "Ref {\n";
    exported_domain_bridge_body_ = true;
    for (const auto& handler : domain.handlers) {
      std::set<string> locals{"self"};
      std::unordered_map<string,string> types{{"self", domain.name}};
      string call = "message self." + handler.name + "(";
      out << "    pub fn __moss_message_" << handler.name << "(&self";
      for (size_t index = 0; index < handler.params.size(); ++index) {
        const auto& parameter = handler.params[index];
        out << ", " << parameter.name << ": " << rust_type(parameter.type);
        locals.insert(parameter.name);
        types[parameter.name] = parameter.type;
        if (index) call += ", ";
        call += parameter.name;
      }
      call += ")";
      const bool saved_phase21_result_context = phase21_result_context_;
      phase21_result_context_ = !handler.observable_effects.raise_set.empty();
      out << ") -> " << handler_message_return_type(handler) << " {\n"
          << "        ";
      if (handler_has_failure_captures(handler)) {
        out << "self." << handler.name << "_shared(";
        for (const auto& parameter : handler.params)
          out << parameter.name << ", ";
        out << ")";
        if (handler.reply_type)
          out << ".map(|value| value.unwrap_or_else(|| std::process::abort()))";
        if (auto module = phase21_external_module_for_symbol(domain.name))
          out << ".map_err(|carrier| __MossRaised::from("
              << phase21_external_raised_type(*module)
              << "::from(carrier)))";
        else
          out << ".map_err(__MossRaised::from)";
      } else {
        if (!handler.observable_effects.raise_set.empty()) out << "Ok(";
        out << expr(call, nullptr, locals, &types);
        if (!handler.observable_effects.raise_set.empty()) out << ")";
      }
      out << "\n    }\n";
      phase21_result_context_ = saved_phase21_result_context;
    }
    exported_domain_bridge_body_ = false;
    out << "}\n\n";
  }

  void gen_domain(std::ostringstream& o, const Domain& d) { gen_planned_domain(o, d); }

  static string rust_string_literal(const string& value) {
    return "\"" + debug_json_escape(value) + "\"";
  }

  static string test_or_benchmark_function_name(
      const string& prefix, const string& semantic_identity) {
    return "__moss_" + prefix + "_" +
        stable_hash(semantic_identity).substr(0, 12);
  }

  static void gen_protocol_hex_helper(std::ostringstream& o) {
    o << "fn __moss_protocol_hex(value: &str) -> String {\n";
    o << "    let mut output = String::with_capacity(value.len() * 2);\n";
    o << "    for byte in value.as_bytes() { use std::fmt::Write as _; let _ = write!(&mut output, \"{:02x}\", byte); }\n";
    o << "    output\n}\n\n";
  }

  void gen_test_harness(std::ostringstream& o) {
    gen_protocol_hex_helper(o);
    const RaiseSet raised = phase21_program_raise_set();
    const bool typed_outcomes = !raised.empty();
    for (const auto& test : p_.tests) {
      string function_name = test_or_benchmark_function_name(
          "test", test.semantic_identity);
      tooling_begin(o, 0, "test", test.semantic_identity, function_name);
      source_comment(o, 0, test.line, test.header);
      o << "#[inline(never)]\n#[allow(unreachable_code)]\nfn "
        << function_name << "()";
      if (typed_outcomes) o << " -> Result<(), __MossRaised>";
      o << " {\n";
      std::set<string> locals;
      std::unordered_map<string,string> types;
      const bool saved_result_context = phase21_result_context_;
      phase21_result_context_ = typed_outcomes;
      gen_stmts(o, test.body, nullptr, nullptr, "", locals, types, 1,
                false, false,
                "test:" + test.name);
      phase21_result_context_ = saved_result_context;
      if (typed_outcomes) o << "    Ok(())\n";
      o << "}\n";
      tooling_end(o, 0, test.semantic_identity);
      o << "\n";
    }

    o << "fn main() {\n";
    o << "    let previous_hook = std::panic::take_hook();\n";
    o << "    std::panic::set_hook(Box::new(|_| {}));\n";
    o << "    let mut failures = 0_usize;\n";
    if (typed_outcomes) {
      o << "    fn __moss_test_error_identity(error: &__MossRaised) -> &'static str {\n"
        << "        match error {\n";
      for (const auto& identity : raised)
        o << "            __MossRaised::" << phase21_rust_variant(identity)
          << "(_) => " << rust_string_literal(identity.canonical()) << ",\n";
      o << "        }\n    }\n";
    }
    for (const auto& test : p_.tests) {
      string function_name = test_or_benchmark_function_name(
          "test", test.semantic_identity);
      o << "    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| "
        << function_name << "()));\n";
      o << "    match outcome {\n";
      o << "        " << (typed_outcomes ? "Ok(Ok(()))" : "Ok(())")
        << " => println!(\"MOSS_TEST|PASS|{}|{}|\", "
        << "__moss_protocol_hex(" << rust_string_literal(test.semantic_identity)
        << "), __moss_protocol_hex(" << rust_string_literal(test.name)
        << ")),\n";
      if (typed_outcomes) {
        o << "        Ok(Err(error)) => {\n"
          << "            failures += 1;\n"
          << "            let detail = format!(\"unhandled typed raise {}\", __moss_test_error_identity(&error));\n"
          << "            println!(\"MOSS_TEST|FAIL|{}|{}|{}\", "
          << "__moss_protocol_hex(" << rust_string_literal(test.semantic_identity)
          << "), __moss_protocol_hex(" << rust_string_literal(test.name)
          << "), __moss_protocol_hex(&detail));\n"
          << "        }\n";
      }
      o << "        Err(payload) => {\n";
      o << "            failures += 1;\n";
      o << "            let detail = if let Some(value) = payload.downcast_ref::<String>() { value.clone() } else if let Some(value) = payload.downcast_ref::<&str>() { (*value).to_string() } else { \"test panicked without a Moss assertion message\".to_string() };\n";
      o << "            println!(\"MOSS_TEST|FAIL|{}|{}|{}\", "
        << "__moss_protocol_hex(" << rust_string_literal(test.semantic_identity)
        << "), __moss_protocol_hex(" << rust_string_literal(test.name)
        << "), __moss_protocol_hex(&detail));\n";
      o << "        }\n";
      o << "    }\n";
    }
    o << "    std::panic::set_hook(previous_hook);\n";
    o << "    if failures != 0 { std::process::exit(1); }\n";
    o << "}\n";
  }

  void gen_benchmark_harness(std::ostringstream& o) {
    gen_protocol_hex_helper(o);
    const RaiseSet raised = phase21_program_raise_set();
    const bool typed_outcomes = !raised.empty();
    for (const auto& benchmark : p_.benchmarks) {
      string function_name = test_or_benchmark_function_name(
          "bench", benchmark.semantic_identity);
      tooling_begin(o, 0, "benchmark", benchmark.semantic_identity,
                    function_name);
      source_comment(o, 0, benchmark.line, benchmark.header);
      o << "#[inline(never)]\n#[allow(unreachable_code)]\nfn " << function_name << "()";
      if (typed_outcomes) o << " -> Result<(), __MossRaised>";
      o << " {\n";
      std::set<string> locals;
      std::unordered_map<string,string> types;
      bool previous_benchmark_body = benchmark_body_;
      const bool saved_result_context = phase21_result_context_;
      benchmark_body_ = true;
      phase21_result_context_ = typed_outcomes;
      gen_stmts(o, benchmark.body, nullptr, nullptr, "", locals, types, 1,
                false, false,
                "bench:" + benchmark.name);
      benchmark_body_ = previous_benchmark_body;
      phase21_result_context_ = saved_result_context;
      std::set<string> consumed_locals;
      for (const auto& statement : benchmark.body)
        consumed_locals.insert(statement.ownership_consumed_bindings.begin(),
                               statement.ownership_consumed_bindings.end());
      for (const auto& local : locals)
        if (!consumed_locals.count(local))
          o << "    std::hint::black_box(&" << local << ");\n";
      if (typed_outcomes) o << "    Ok(())\n";
      o << "}\n";
      tooling_end(o, 0, benchmark.semantic_identity);
      o << "\n";
    }

    o << "fn main() {\n";
    o << "    const WARMUP: usize = 5;\n";
    o << "    const SAMPLES: usize = 31;\n";
    o << "    const ITERATIONS: usize = 1000;\n";
    o << "    let previous_hook = std::panic::take_hook();\n";
    o << "    std::panic::set_hook(Box::new(|_| {}));\n";
    for (const auto& benchmark : p_.benchmarks) {
      string function_name = test_or_benchmark_function_name(
          "bench", benchmark.semantic_identity);
      o << "    for _ in 0..WARMUP { for _ in 0..ITERATIONS { let _ = std::hint::black_box("
        << function_name << "()); } }\n";
      o << "    let mut samples: std::vec::Vec<u128> = std::vec::Vec::with_capacity(SAMPLES);\n";
      o << "    for _ in 0..SAMPLES {\n";
      o << "        let started = std::time::Instant::now();\n";
      o << "        for _ in 0..ITERATIONS { let _ = std::hint::black_box("
        << function_name << "()); }\n";
      o << "        let elapsed = started.elapsed().as_nanos();\n";
      o << "        samples.push((elapsed + ITERATIONS as u128 - 1) / ITERATIONS as u128);\n";
      o << "    }\n";
      o << "    samples.sort_unstable();\n";
      o << "    let p25 = samples[SAMPLES / 4];\n";
      o << "    let median = samples[SAMPLES / 2];\n";
      o << "    let p75 = samples[(SAMPLES * 3) / 4];\n";
      o << "    let sample_text = samples.iter().map(|value| value.to_string()).collect::<std::vec::Vec<_>>().join(\",\");\n";
      o << "    println!(\"MOSS_BENCH|{}|{}|{}|{}|{}|{}|{}|{}|{}\", "
        << "__moss_protocol_hex(" << rust_string_literal(benchmark.semantic_identity)
        << "), __moss_protocol_hex(" << rust_string_literal(benchmark.name)
        << "), median, p25, p75, SAMPLES, WARMUP, ITERATIONS, sample_text);\n";
    }
    o << "    std::panic::set_hook(previous_hook);\n";
    o << "}\n";
  }

  void gen_main(std::ostringstream& o, const MainProc& m) {
    string semantic_identity = "main@" + std::to_string(m.line);
    tooling_begin(o, 0, "main", semantic_identity, "main",
                  "moss__main");
    source_comment(o, 0, m.line, m.header.empty() ? "proc main()" : m.header);
    backend_comment(o, 0, "main entry point; domain calls below retain their statically selected lowering");
    // An explicit module may also be compiled as a provider rlib. Its main
    // belongs to the executable build only: even a local rlib main body may
    // reference process-root FileIO and executor items absent from the rlib.
    if (project_module_artifact_) {
      o << "#[cfg(moss_process_root)]\n";
      if (debug_build_) o << "#[inline(never)]\n";
      o << "#[cfg_attr(moss_process_root, export_name = \"moss__main\")]\n";
    } else {
      debug_symbol_attributes(o, 0, "moss__main");
    }
    o << "fn main() {\n";
    if (program_uses_fileio(p_))
      o << "    moss_set_solo_hooks(executor_fileio_solo_enter_callback(), "
           "executor_fileio_solo_leave_callback());\n";
    std::set<string> locals;
    std::unordered_map<string,string> types;
    // Domain construction is a structural composition prefix.  Emit that
    // prefix in the graph's deterministic topological order so a route may
    // name a concrete instance whose source binding appears later in main.
    // This keeps source order from becoming a hidden dependency while leaving
    // ordinary executable statements in their source order.
    vector<Stmt> constructions;
    vector<Stmt> executable;
    for (const auto& statement : m.body) {
      if (domain_construction(statement.b)) constructions.push_back(statement);
      else executable.push_back(statement);
    }
    std::unordered_map<string,int> domain_ranks;
    std::unordered_map<string,string> domain_identities;
    for (const auto& instance : p_.concrete_domain_graph.instances) {
      domain_ranks[instance.binding] = instance.domain_rank;
      domain_identities[instance.binding] = instance.identity;
    }
    std::stable_sort(constructions.begin(), constructions.end(),
                     [&](const Stmt& left, const Stmt& right) {
                       // A route target must exist before its owner can be
                       // constructed; domain_rank itself points from owner
                       // to target, so construction runs in reverse rank.
                       return domain_ranks[left.a] > domain_ranks[right.a];
                     });
    domain_instance_bindings_.clear();
    ambiguous_domain_instance_bindings_.clear();
    size_t construction_index = 0;
    gen_block(o, constructions, construction_index, 0, nullptr, nullptr, "", locals, types, 1,
              false, false, {}, "main");
    size_t index = 0;
    gen_block(o, executable, index, 0, nullptr, nullptr, "", locals, types, 1,
              false, false, {}, "main");
    o << "}\n";
    tooling_end(o, 0, semantic_identity);
  }

  static size_t statement_functional_pipeline_id(
      const Stmt& statement, const string& context, size_t slot) {
    auto plans = statement.functional_pipeline_ids.find(context);
    if (plans == statement.functional_pipeline_ids.end() ||
        slot >= plans->second.size())
      return 0;
    return plans->second[slot];
  }

  void gen_stmts(std::ostringstream& o, const vector<Stmt>& ss, const Domain* d,
                 const Handler* current_handler, const string& reply_slot,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler,
                 bool in_function = false,
                 const string& functional_context = "") {
    // Binding metadata is lexical to this generated Moss body.  Keeping it
    // separate from Rust type strings prevents a local/backend name collision
    // from being mistaken for a specialization relationship.
    domain_instance_bindings_.clear();
    ambiguous_domain_instance_bindings_.clear();
    size_t i = 0;
    gen_block(o, ss, i, 0, d, current_handler, reply_slot, locals, types, base,
              in_handler, in_function, {},
              functional_context);
    if (i != ss.size()) throw std::runtime_error("internal error: statement indentation tree not fully consumed");
  }

  void gen_block(std::ostringstream& o, const vector<Stmt>& ss, size_t& i, int level,
                 const Domain* d, const Handler* current_handler, const string& reply_slot,
                 std::set<string>& locals, std::unordered_map<string,string>& types,
                 int base, bool in_handler, bool in_function,
                 std::set<string> join_assignments = {},
                 const string& functional_context = "") {
    struct RecoverySlotScope {
      std::set<string>& slot;
      std::set<string> prior;
      explicit RecoverySlotScope(std::set<string>& current)
          : slot(current), prior(current) {}
      ~RecoverySlotScope() { slot = std::move(prior); }
    } recovery_slot_scope(phase21_option_slots_);
    auto indent = [&](int lev){ return string((base + lev) * 4, ' '); };
    while (i < ss.size()) {
      const auto& s = ss[i];
      auto concrete_binding = s.semantic_types_by_context.find(functional_context);
      const string& statement_type = concrete_binding == s.semantic_types_by_context.end()
          ? s.semantic_type : concrete_binding->second;
      if (s.indent < level) return;
      if (s.indent > level) throw std::runtime_error("internal error: unexpected statement indentation");
      if (s.kind == Stmt::Kind::Else) return; // consumed by the preceding if

      struct CodegenStatementScope {
        const Stmt*& slot;
        const Stmt* prior;
        CodegenStatementScope(const Stmt*& target, const Stmt* current)
            : slot(target), prior(target) { slot = current; }
        ~CodegenStatementScope() { slot = prior; }
      } codegen_statement_scope(phase21_codegen_statement_, &s);
      FunctionalExpressionHandlesScope expression_handles_scope(
          active_functional_expression_handles_, &s.expression_handles);
      struct CodegenContextScope {
        string& slot;
        string prior;
        CodegenContextScope(string& target, const string& current)
            : slot(target), prior(target) { slot = current; }
        ~CodegenContextScope() { slot = std::move(prior); }
      } codegen_context_scope(codegen_body_context_, functional_context);

      source_comment(o, (base + level) * 4, s.line, s.text);

      struct RootIngressScope {
        bool& flag; bool saved;
        RootIngressScope(bool& f, bool value) : flag(f), saved(f) { flag = value; }
        ~RootIngressScope() { flag = saved; }
      } root_ingress_scope(root_ingress_statement_, s.message_root_ingress);
      struct CaptureLocalsScope {
        const std::set<string>*& slot;
        const std::set<string>* prior;
        CaptureLocalsScope(const std::set<string>*& target,
                           const std::set<string>& current)
            : slot(target), prior(target) { slot = &current; }
        ~CaptureLocalsScope() { slot = prior; }
      } capture_locals_scope(phase21_capture_locals_, locals);
      struct CaptureAlternativesScope {
        const RaiseSet*& slot;
        const RaiseSet* prior;
        CaptureAlternativesScope(const RaiseSet*& target,
                                 const RaiseSet& current)
            : slot(target), prior(target) { slot = &current; }
        ~CaptureAlternativesScope() { slot = prior; }
      } capture_alternatives_scope(phase21_capture_alternatives_,
                                   s.exceptional_alternatives);

      size_t direct_functional_id = statement_functional_pipeline_id(
          s, functional_context, 0);
      const FunctionalPipeline* direct_functional =
          planned_functional_pipeline(direct_functional_id);
      if (direct_functional &&
          direct_functional->virtualized_into_pipeline_id &&
          (s.kind == Stmt::Kind::Assign || s.kind == Stmt::Kind::Let ||
           s.kind == Stmt::Kind::Var)) {
        backend_comment(
            o, (base + level) * 4,
            "FUNCTIONAL INTERMEDIATE '" + direct_functional->binding_name +
                "' VIRTUALIZED across one immutable binding; no collection allocated");
        locals.insert(s.a);
        types[s.a] = direct_functional->output_type;
        ++i;
        continue;
      }
      if (direct_functional && direct_functional->traversal_group_id) {
        const FunctionalTraversalGroup* group = functional_traversal_group(
            direct_functional->traversal_group_id);
        if (!group || group->consumers.empty() ||
            group->consumers.front().pipeline_id != direct_functional_id)
          throw std::runtime_error(
              "internal error: shared functional traversal did not start at "
              "its first consumer");
        gen_shared_functional_traversal(
            o, *group, ss, i, d, locals, types, (base + level) * 4,
            functional_context);
        i += group->consumers.size();
        continue;
      }
      if (direct_functional && !direct_functional->binding_name.empty() &&
          !direct_functional->virtualized_into_pipeline_id &&
          !direct_functional->binding_materialization_reason.empty())
        backend_comment(
            o, (base + level) * 4,
            "FUNCTIONAL INTERMEDIATE '" + direct_functional->binding_name +
                "' MATERIALIZED; reason: " +
                direct_functional->binding_materialization_reason);

      if (gen_executor_or_fileio_statement(o, s, (base + level) * 4, d, locals, types)) {
        ++i;
        continue;
      }

      switch (s.kind) {
        case Stmt::Kind::Try: {
          // Rust cannot infer that a move on one exceptional variant leaves
          // an owned local available on another. The checker has already
          // proved the recovery path; retain only affected locals in slots.
          size_t attempt_end = i + 1;
          while (attempt_end < ss.size() && ss[attempt_end].indent > level)
            ++attempt_end;
          size_t recover_end = attempt_end;
          while (recover_end < ss.size() && ss[recover_end].indent == level &&
                 ss[recover_end].kind == Stmt::Kind::Recover) {
            ++recover_end;
            while (recover_end < ss.size() && ss[recover_end].indent > level)
              ++recover_end;
          }
          std::set<string> recovery_slots;
          for (size_t candidate = i + 1; candidate < attempt_end; ++candidate) {
            const Stmt& consumed = ss[candidate];
            for (const auto& binding : consumed.ownership_consumed_bindings) {
              if (!types.count(binding) || copy_type(types.at(binding)) ||
                  phase21_option_slots_.count(binding))
                continue;
              for (size_t read = attempt_end; read < recover_end; ++read) {
                const Stmt& recovered = ss[read];
                bool mentions = tree_expression_mentions_identifier(
                    recovered.b, binding);
                if (recovered.kind != Stmt::Kind::Assign)
                  mentions |= tree_expression_mentions_identifier(
                      recovered.a, binding);
                for (const auto& argument : recovered.args)
                  mentions |= tree_expression_mentions_identifier(
                      argument, binding);
                if (mentions) {
                  recovery_slots.insert(binding);
                  break;
                }
              }
            }
          }
          for (const auto& binding : recovery_slots) {
            o << indent(level) << "let mut __moss_recovery_slot_" << binding
              << " = Some(" << rust_identifier(binding) << ");\n";
            phase21_option_slots_.insert(binding);
          }
          const auto exact_join =
              s.joined_types_by_context.find(functional_context);
          const auto& joined_types = exact_join ==
                  s.joined_types_by_context.end()
              ? s.joined_types
              : exact_join->second;
          vector<string> joined_bindings;
          for (const auto& entry : joined_types)
            if (!types.count(entry.first) &&
                !contains_internal_placeholder(entry.second))
              joined_bindings.push_back(entry.first);
          std::sort(joined_bindings.begin(), joined_bindings.end());
          for (const auto& binding : joined_bindings) {
            o << indent(level) << "let mut " << binding << ": "
              << rust_type(joined_types.at(binding)) << ";\n";
            locals.insert(binding);
            types[binding] = joined_types.at(binding);
          }

          if (s.exceptional_alternatives.empty()) {
            ++i;
            auto attempt_locals = locals;
            auto attempt_types = types;
            auto attempt_join_assignments = join_assignments;
            attempt_join_assignments.insert(joined_bindings.begin(),
                                            joined_bindings.end());
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      attempt_locals, attempt_types, base, in_handler,
                      in_function, attempt_join_assignments,
                      functional_context);
            while (i < ss.size() && ss[i].indent == level &&
                   ss[i].kind == Stmt::Kind::Recover) {
              ++i;
              while (i < ss.size() && ss[i].indent > level) ++i;
            }
            break;
          }

          const string outcome = "__moss_try_outcome_" +
              std::to_string(phase21_try_temp_++);
          // The attempt's normal completion leaves this outer block
          // directly, so rustc sees every join binding initialized on the
          // normal path even when a raise precedes its first assignment.
          const string done_label = "__moss_try_done_" +
              std::to_string(phase21_try_temp_++);
          o << indent(level) << "'" << done_label << ": {\n";
          o << indent(level) << "#[allow(unreachable_code)]\n";
          const string try_label = "__moss_try_" +
              std::to_string(phase21_try_temp_++);
          const bool captured_try = phase21_capture_handler_ != nullptr;
          const string carrier = captured_try
              ? handler_capture_carrier_type(*phase21_capture_handler_) : "";
          o << indent(level) << "let " << outcome
            << ": Result<(), " << (captured_try ? carrier : "__MossRaised")
            << "> = '" << try_label << ": {\n";
          ++i;
          auto attempt_locals = locals;
          auto attempt_types = types;
          auto attempt_join_assignments = join_assignments;
          attempt_join_assignments.insert(joined_bindings.begin(),
                                          joined_bindings.end());
          const bool saved_result_context = phase21_result_context_;
          const string saved_try_label = phase21_try_label_;
          const auto saved_return_result_context = phase21_return_result_context_;
          if (!phase21_return_result_context_)
            phase21_return_result_context_ = saved_result_context;
          phase21_result_context_ = true;
          phase21_try_label_ = try_label;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    attempt_locals, attempt_types, base, in_handler,
                    in_function, attempt_join_assignments,
                    functional_context);
          phase21_result_context_ = saved_result_context;
          phase21_try_label_ = saved_try_label;
          o << indent(level + 1) << "break '" << done_label << ";\n";
          o << indent(level) << "};\n";
          o << indent(level) << "#[allow(unreachable_patterns)]\n";
          o << indent(level) << "match " << outcome << " {\n";
          o << indent(level + 1) << "Ok(()) => std::process::abort(),\n";

          RaiseSet handled;
          while (i < ss.size() && ss[i].indent == level &&
                 ss[i].kind == Stmt::Kind::Recover) {
            const Stmt& arm = ss[i++];
            const size_t body_begin = i;
            size_t body_end = i;
            while (body_end < ss.size() && ss[body_end].indent > level)
              ++body_end;

            for (const auto& identity : arm.exceptional_alternatives) {
              handled.insert(identity);
              const string variant = phase21_rust_variant(identity);
              const string inner = "__moss_selected_value";
              if (captured_try) {
                o << indent(level + 1) << "Err(__moss_selected_carrier) "
                  << "if matches!(&__moss_selected_carrier.0.error, "
                  << "__MossRaised::" << variant << "(";
              } else {
                o << indent(level + 1) << "Err(__MossRaised::" << variant
                  << "(" << inner << " @ ";
              }
              if (identity.kind == RaisedIdentityKind::EnumVariant) {
                o << phase21_rust_error_type(identity) << "::"
                  << rust_identifier(identity.variant);
                string enum_key = canonical_type_name(identity.type);
                if (!enums_.count(enum_key)) {
                  for (size_t separator = enum_key.find("::");
                       separator != string::npos;
                       separator = enum_key.find("::"))
                    enum_key.replace(separator, 2, "__");
                }
                auto declaration = enums_.find(enum_key);
                auto item = declaration == enums_.end()
                    ? vector<EnumCase>::const_iterator{}
                    : std::find_if(
                          declaration->second->cases.begin(),
                          declaration->second->cases.end(),
                          [&](const EnumCase& candidate) {
                            return candidate.name == identity.variant;
                          });
                if (declaration != enums_.end() &&
                    item != declaration->second->cases.end() &&
                    !item->fields.empty())
                  o << " { .. }";
              } else {
                o << "_";
              }
              o << ")";
              if (captured_try) {
                o << ") => {\n";
                o << indent(level + 2) << "let " << inner
                  << " = match &__moss_selected_carrier.0.error { "
                  << "__MossRaised::" << variant
                  << "(__moss_value) => __moss_value.clone(), "
                  << "_ => std::process::abort() };\n";
              } else {
                o << ") => {\n";
              }
              const string selected = "__moss_selected_error";
              o << indent(level + 2) << "let " << selected
                << " = __MossRaised::" << variant << "(" << inner
                << ".clone());\n";

              auto arm_locals = locals;
              auto arm_types = types;
              if (arm.est_pattern && arm.est_pattern->kind ==
                  est::PatternNode::Kind::RecoveryBinding) {
                const auto& binding = arm.est_pattern->binding;
                o << indent(level + 2) << "let " << binding << " = " << inner
                  << ";\n";
                arm_locals.insert(binding);
                arm_types[binding] = identity.type;
              } else if (arm.est_pattern &&
                  (arm.est_pattern->kind == est::PatternNode::Kind::QualifiedRecoveryCase ||
                   arm.est_pattern->kind == est::PatternNode::Kind::GroupedAlternatives)) {
                const auto alternatives = arm.est_pattern->kind ==
                        est::PatternNode::Kind::GroupedAlternatives
                    ? arm.est_pattern->alternatives
                    : std::vector<est::PatternNode>{*arm.est_pattern};
                for (const auto& pattern : alternatives) {
                  if (!(generated_failure_pattern_identity(pattern) == identity))
                    continue;
                  emit_generated_error_payload_bindings(
                      o, (base + level + 2) * 4, inner, identity,
                      generated_error_pattern_bindings(pattern, identity),
                      arm_locals, arm_types);
                  break;
                }
              }
              auto arm_join_assignments = join_assignments;
              arm_join_assignments.insert(joined_bindings.begin(),
                                          joined_bindings.end());
              const string saved_selected_error = phase21_selected_error_;
              const string saved_selected_carrier = phase21_selected_carrier_;
              phase21_selected_error_ = selected;
              phase21_selected_carrier_ = captured_try
                  ? "__moss_selected_carrier" : string();
              size_t rendered_end = body_begin;
              gen_block(o, ss, rendered_end, level + 1, d, current_handler,
                        reply_slot, arm_locals, arm_types, base + 1,
                        in_handler, in_function, arm_join_assignments,
                        functional_context);
              phase21_selected_error_ = saved_selected_error;
              phase21_selected_carrier_ = saved_selected_carrier;
              if (rendered_end != body_end)
                throw std::runtime_error(
                    "internal error: recover body was not fully lowered");
              o << indent(level + 1) << "},\n";
            }
            i = body_end;
          }
          RaiseSet remaining = raise_set_without(
              s.exceptional_alternatives, handled);
          if (!remaining.empty()) {
            o << indent(level + 1)
              << "Err(__moss_unhandled_error) => ";
            if (!saved_try_label.empty())
              o << "break '" << saved_try_label
                << " Err(__moss_unhandled_error),\n";
            else if (saved_result_context)
              o << "return Err(" <<
                  "__moss_unhandled_error" << "),\n";
            else
              o << "std::process::abort(),\n";
          } else {
            o << indent(level + 1)
              << "Err(_) => std::process::abort(),\n";
          }
          o << indent(level) << "}\n";
          o << indent(level) << "}\n";
          phase21_return_result_context_ = saved_return_result_context;
          break;
        }
        case Stmt::Kind::Recover:
          throw std::runtime_error(
              "internal error: recover arm escaped checked try lowering");
        case Stmt::Kind::Raise: {
          if (s.a.empty()) {
            if (phase21_selected_error_.empty())
              throw std::runtime_error(
                  "internal error: bare raise lacks a selected error");
            if (phase21_selected_carrier_.empty()) {
              o << indent(level)
                << phase21_error_exit(phase21_selected_error_) << ";\n";
            } else if (phase21_try_label_.empty()) {
              o << indent(level) << "return Err("
                << phase21_selected_carrier_ << ");\n";
            } else {
              o << indent(level) << "break '" << phase21_try_label_
                << " Err(" << phase21_selected_carrier_ << ");\n";
            }
          } else {
            auto type = generated_expr_type(s.a, &types);
            if (!type)
              throw std::runtime_error(
                  "internal error: raise lacks a checked error value type");
            const string concrete = canonical_type_name(*type);
            const string value = expr(s.a, d, locals, &types,
                statement_functional_pipeline_id(
                    s, functional_context, 0));
            if (enums_.count(concrete)) {
              const string raised_value = "__moss_error_value_" +
                  std::to_string(phase21_try_temp_++);
              const string temporary = "__moss_raised_value_" +
                  std::to_string(phase21_try_temp_++);
              o << indent(level) << "let " << raised_value << " = "
                << value << ";\n"
                << indent(level) << "#[allow(unreachable_patterns)]\n"
                << indent(level) << "let " << temporary << " = match "
                << raised_value << " {\n";
              for (const auto& identity : s.exceptional_alternatives) {
                if (identity.kind != RaisedIdentityKind::EnumVariant ||
                    canonical_type_name(identity.type) != concrete)
                  continue;
                const auto& declaration = *enums_.at(concrete);
                auto item = std::find_if(declaration.cases.begin(),
                    declaration.cases.end(), [&](const EnumCase& candidate) {
                      return candidate.name == identity.variant;
                    });
                if (item == declaration.cases.end()) continue;
                o << indent(level + 1) << "value @ "
                  << phase21_rust_error_type(identity) << "::"
                  << rust_identifier(identity.variant);
                if (!item->fields.empty()) o << " { .. }";
                o << " => __MossRaised::"
                  << phase21_rust_variant(identity) << "(value),\n";
              }
              o << indent(level + 1)
                << "_ => std::process::abort(),\n"
                << indent(level) << "};\n"
                << indent(level)
                << phase21_error_exit(temporary) << ";\n";
            } else {
              const RaisedIdentity identity{
                  RaisedIdentityKind::ScalarType, concrete, {}};
              if (!s.exceptional_alternatives.count(identity))
                throw std::runtime_error(
                    "internal error: raise lacks its checked scalar identity");
              o << indent(level) << phase21_error_exit(
                  "__MossRaised::" + phase21_rust_variant(identity) +
                  "(" + value + ")") << ";\n";
            }
          }
          ++i;
          while (i < ss.size() && ss[i].indent >= level) ++i;
          break;
        }
        case Stmt::Kind::Match: {
          auto scrutinee_type = generated_expr_type(s.a, &types);
          if (!scrutinee_type || !enums_.count(canonical_type_name(*scrutinee_type)))
            throw std::runtime_error("unchecked enum match");
          const EnumType& declaration = *enums_.at(canonical_type_name(*scrutinee_type));
          const auto exact_join = s.joined_types_by_context.find(functional_context);
          const auto& joined_types = exact_join == s.joined_types_by_context.end()
              ? s.joined_types : exact_join->second;
          vector<string> joined_bindings;
          for (const auto& entry : joined_types)
            if (!types.count(entry.first) && !contains_internal_placeholder(entry.second))
              joined_bindings.push_back(entry.first);
          std::sort(joined_bindings.begin(), joined_bindings.end());
          for (const auto& binding : joined_bindings) {
            o << indent(level) << "let mut " << binding << ": "
              << rust_type(joined_types.at(binding)) << ";\n";
            locals.insert(binding);
            types[binding] = joined_types.at(binding);
          }
          string match_callee;
          vector<string> match_arguments;
          bool struct_literal = false;
          // A Rust struct literal is not allowed as a bare scrutinee.
          if (tree_parse_simple_call(s.a, match_callee, match_arguments)) {
            for (const auto& candidate : declaration.cases) {
              const string suffix = declaration.name + "." + candidate.name;
              if (!candidate.fields.empty() &&
                  (match_callee == suffix ||
                   (match_callee.size() > suffix.size() &&
                    match_callee.compare(match_callee.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                    match_callee[match_callee.size() - suffix.size() - 1] == '.'))) {
                struct_literal = true;
                break;
              }
            }
          }
          o << indent(level) << "match ";
          const bool recovery_slot = phase21_option_slots_.count(s.a) != 0;
          if (s.b != "consume" && !recovery_slot) o << "&";
          if (struct_literal) o << "(";
          if (recovery_slot && s.b == "consume")
            o << "__moss_recovery_slot_" << s.a
              << ".take().unwrap_or_else(|| std::process::abort())";
          else
            o << expr(s.a, d, locals, &types);
          if (struct_literal) o << ")";
          o << " {\n";
          ++i;
          while (i < ss.size() && ss[i].indent == level + 1) {
            const Stmt& arm = ss[i];
            auto item = std::find_if(declaration.cases.begin(), declaration.cases.end(),
                [&](const EnumCase& candidate) { return candidate.name == arm.a; });
            if (item == declaration.cases.end())
              throw std::runtime_error("unchecked enum case");
            o << indent(level + 1) << declaration.name << "::"
              << rust_identifier(arm.a);
            if (!item->fields.empty()) {
              o << " { ";
              for (size_t field_index = 0; field_index < arm.args.size(); ++field_index) {
                if (field_index) o << ", ";
                string field_name = rust_identifier(item->fields[field_index].name);
                if (field_name == arm.args[field_index])
                  o << field_name;
                else
                  o << field_name << ": " << arm.args[field_index];
              }
              o << " }";
            }
            o << " => {\n";
            auto branch_locals = locals;
            auto branch_types = types;
            auto saved_borrowed = borrowed_function_parameters_;
            for (size_t field_index = 0; field_index < arm.args.size(); ++field_index) {
              const string& name = arm.args[field_index];
              const string& type = item->fields[field_index].type;
              branch_locals.insert(name);
              branch_types[name] = type;
              if (s.b != "consume") {
                if (type == "int" || type == "bool" || type == "float")
                  o << indent(level + 2) << "let " << name << " = *" << name << ";\n";
                else borrowed_function_parameters_.insert(name);
              }
            }
            ++i;
            auto branch_join = join_assignments;
            branch_join.insert(joined_bindings.begin(), joined_bindings.end());
            gen_block(o, ss, i, level + 2, d, current_handler, reply_slot,
                      branch_locals, branch_types, base, in_handler, in_function,
                      branch_join, functional_context);
            borrowed_function_parameters_ = std::move(saved_borrowed);
            o << indent(level + 1) << "},\n";
          }
          o << indent(level) << "}\n";
          break;
        }
        case Stmt::Kind::Case:
          throw std::runtime_error("case outside checked match");
        case Stmt::Kind::If: {
          const auto exact_join = s.joined_types_by_context.find(functional_context);
          if (functional_context.find('<') != string::npos &&
              exact_join == s.joined_types_by_context.end())
            throw std::runtime_error(
                "internal error: missing checked control-flow join for " +
                functional_context);
          const auto& joined_types = exact_join == s.joined_types_by_context.end()
              ? s.joined_types : exact_join->second;
          vector<string> joined_bindings;
          for (const auto& entry : joined_types)
            if (!types.count(entry.first) && !contains_internal_placeholder(entry.second))
              joined_bindings.push_back(entry.first);
          std::sort(joined_bindings.begin(), joined_bindings.end());
          for (const auto& binding : joined_bindings) {
            const string& type = joined_types.at(binding);
            backend_comment(o, (base + level) * 4,
                            "CONTROL-FLOW JOIN: binding has one definite static type on every path");
            o << indent(level) << "let mut " << binding << ": "
              << rust_type(type)
              << ";\n";
            locals.insert(binding);
            types[binding] = type;
          }
          o << indent(level) << "if "
            << expr(s.a, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, 0))
            << " {\n";
          ++i;
          auto child_locals = locals;
          auto child_types = types;
          auto child_join_assignments = join_assignments;
          child_join_assignments.insert(joined_bindings.begin(), joined_bindings.end());
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    child_locals, child_types, base, in_handler, in_function, child_join_assignments,
                    functional_context);
          o << indent(level) << "}";
          if (i < ss.size() && ss[i].indent == level && ss[i].kind == Stmt::Kind::Else) {
            o << " else {\n";
            source_comment(o, (base + level + 1) * 4, ss[i].line, ss[i].text);
            ++i;
            auto else_locals = locals;
            auto else_types = types;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      else_locals, else_types, base, in_handler, in_function, child_join_assignments,
                      functional_context);
            o << indent(level) << "}\n";
          } else {
            o << "\n";
          }
          // Existing bindings keep their already established type in this
          // instance; only new joined locals use this context's checked type.
          break;
        }
        case Stmt::Kind::While: {
          o << indent(level) << "while "
            << expr(s.a, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, 0))
            << " {\n";
          ++i;
          auto child_locals = locals;
          auto child_types = types;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    child_locals, child_types, base, in_handler, in_function, join_assignments,
                    functional_context);
          o << indent(level) << "}\n";
          break;
        }
        case Stmt::Kind::For: {
          string source = trim(s.b);
          string range_name;
          vector<string> range_args;
          bool is_range = tree_parse_simple_call(source, range_name, range_args) &&
              range_name == "range";
          auto source_type = generated_expr_type(source, &types);
          string element_type = source_type ? canonical_type_name(*source_type) : "";
          if (is_range) {
            // Materialize each range bound into a typed local before the
            // for-loop.  An expression such as `values |> count` expands to a
            // Rust block `{ … __moss_result }`.  When placed after `..` in
            //   for index in start..{ block } { body }
            // rustc parses `{ block }` as the for-body, not the upper bound,
            // silently dropping the induction binding and the intended body.
            // Binding the result first avoids this precedence ambiguity entirely.
            string start_var = "__moss_range_start_" + std::to_string(s.line);
            string end_var   = "__moss_range_end_"   + std::to_string(s.line);
            o << indent(level) << "let " << start_var << ": i64 = "
              << expr(range_args[0], d, locals, &types) << ";\n";
            o << indent(level) << "let " << end_var << ": i64 = "
              << expr(range_args[1], d, locals, &types) << ";\n";
            if (range_args.size() == 3) {
              string step_var = "__moss_range_step_" + std::to_string(s.line);
              o << indent(level) << "let " << step_var << ": i64 = "
                << expr(range_args[2], d, locals, &types) << ";\n";
              o << indent(level) << "for " << s.a << " in ("
                << start_var << ".." << end_var
                << ").step_by(" << step_var << " as usize) {\n";
            } else {
              o << indent(level) << "for " << s.a << " in "
                << start_var << ".." << end_var << " {\n";
            }
            auto child_locals = locals;
            auto child_types = types;
            child_locals.insert(s.a);
            child_types[s.a] = "int";
            ++i;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      child_locals, child_types, base, in_handler, in_function, join_assignments,
                      functional_context);
            o << indent(level) << "}\n";
            break;
          }

          bool vector_source = source_type &&
              (starts_with(element_type, "vector[") ||
               starts_with(element_type, "seq["));
          if (source_type &&
              canonical_type_name(*source_type) == "RangeBatch") {
            string collection_expression = expr(source, d, locals, &types);
            o << indent(level) << "for " << s.a << " in ("
              << collection_expression << ").ranges() {\n";
            auto child_locals = locals;
            auto child_types = types;
            child_locals.insert(s.a);
            child_types[s.a] = "Range";
            ++i;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      child_locals, child_types, base, in_handler, in_function,
                      join_assignments, functional_context);
            o << indent(level) << "}\n";
            break;
          }
          if (source_type && canonical_type_name(*source_type) == "Range") {
            string collection_expression = expr(source, d, locals, &types);
            o << indent(level) << "for " << s.a << " in ("
              << collection_expression << ").bytes() {\n";
            auto child_locals = locals;
            auto child_types = types;
            child_locals.insert(s.a);
            child_types[s.a] = "int";
            ++i;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      child_locals, child_types, base, in_handler, in_function,
                      join_assignments, functional_context);
            o << indent(level) << "}\n";
            break;
          }
          if (vector_source) {
            string element = starts_with(element_type, "vector[")
                ? trim(element_type.substr(7, element_type.size() - 8))
                : trim(element_type.substr(4, element_type.size() - 5));
            string collection_expression = expr(source, d, locals, &types);
            string traversal = "(" + collection_expression + ").iter()";
            // FileIO.chunks yields Agent B's MossChunks, already an
            // Iterator<Item = Range> borrowing the FileIO.
            if (canonical_type_name(element_type) == "seq[Range]")
              traversal = collection_expression;
            else if (copy_type(element)) traversal += ".copied()";
            o << indent(level) << "for " << s.a << " in " << traversal
              << " {\n";
            auto child_locals = locals;
            auto child_types = types;
            child_locals.insert(s.a);
            child_types[s.a] = element;
            ++i;
            gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                      child_locals, child_types, base, in_handler, in_function, join_assignments,
                      functional_context);
            o << indent(level) << "}\n";
            break;
          }

          if (!source_type)
            throw std::runtime_error("internal error: unresolved static iterator source");
          string concrete = canonical_type_name(*source_type);
          const Method* next = resolve_object_method(objects_, concrete, "next", {}, true, nullptr);
          bool direct_iterator = next != nullptr;
          const Method* iterator_factory = nullptr;
          if (!direct_iterator) {
            iterator_factory = resolve_object_method(objects_, concrete, "iter", {}, true, nullptr);
            if (iterator_factory && iterator_factory->return_type)
              next = resolve_object_method(
                  objects_, canonical_type_name(*iterator_factory->return_type),
                  "next", {}, true, nullptr);
          }
          string iterator_expression;
          if (direct_iterator && next->receiver_effect == Effect::Consume)
            iterator_expression = expr(source, d, locals, &types);
          else if (direct_iterator)
            iterator_expression = "&mut " + expr(source, d, locals, &types);
          else
            iterator_expression = expr(source, d, locals, &types) + ".iter()";
          o << indent(level) << "let mut __moss_iterator = "
            << iterator_expression << ";\n";
          o << indent(level) << "loop {\n";
          o << indent(level + 1) << "match __moss_iterator.next() {\n";
          o << indent(level + 2) << "Some(" << s.a << ") => {\n";
          auto child_locals = locals;
          auto child_types = types;
          child_locals.insert(s.a);
          string next_type = next && next->return_type
              ? canonical_type_name(*next->return_type) : "option[_]";
          string element = starts_with(next_type, "option[") && ends_with(next_type, "]")
              ? trim(next_type.substr(7, next_type.size() - 8)) : "_value";
          child_types[s.a] = element;
          ++i;
          gen_block(o, ss, i, level + 1, d, current_handler, reply_slot,
                    child_locals, child_types, base, in_handler, in_function, join_assignments,
                    functional_context);
          o << indent(level + 2) << "}\n";
          o << indent(level + 2) << "None => break,\n";
          o << indent(level + 1) << "}\n";
          o << indent(level) << "}\n";
          break;
        }
        case Stmt::Kind::Echo: {
          o << indent(level);
          if (s.args.empty()) o << "println!();\n";
          else {
            o << "println!(\"";
            for (size_t k = 0; k < s.args.size(); ++k) { if (k) o << " "; o << "{}"; }
            o << "\"";
            for (size_t argument = 0; argument < s.args.size(); ++argument) {
              string rendered = expr(
                  s.args[argument], d, locals, &types,
                  statement_functional_pipeline_id(s, functional_context,
                                                   argument));
              auto type = generated_expr_type(s.args[argument], &types);
              auto resolved = [](const std::optional<string>& t) {
                if (!t) return false;
                const string c = canonical_type_name(*t);
                return !c.empty() && c != "vector" && !internal_semantic_placeholder(c);
              };
              if (!resolved(type)) {
                // e.g. `[[], [1]]`: the backend's local typing cannot see the
                // element type; use the type the checker accepted instead.
                auto checked = s.echo_types_by_context.find(functional_context);
                if (checked == s.echo_types_by_context.end())
                  checked = s.echo_types_by_context.find(string());
                if (checked != s.echo_types_by_context.end() &&
                    argument < checked->second.size() &&
                    resolved(checked->second[argument]))
                  type = checked->second[argument];
              }
              // The checker owns the echo display contract (S-B); a Vector
              // formats through the Moss display runtime, never Rust Debug.
              // Anything else without a contract is an internal error here,
              // reported with the checker's code rather than as rustc E0277.
              const string shown = type ? canonical_type_name(*type) : string();
              if (shown == "map" || shown == "queue" || starts_with(shown, "map[") ||
                  starts_with(shown, "queue[") || objects_.count(shown) ||
                  enums_.count(shown) ||
                  (starts_with(shown, "vector[") &&
                   !native_echo_vector_displayable(shown)))
                throw CompileError(s.line, "echo cannot display a value of type '" +
                    shown + "'", "ECHO_DISPLAY_UNSUPPORTED");
              if (shown == "float") {
                rendered = "__moss_ryu::canonical_float_text(" + rendered + ")";
              } else if (starts_with(shown, "vector[")) {
                auto element = native_echo_element(shown.substr(7, shown.size() - 8));
                if (!element) {
                  // The backend saw a partially open element type; use the
                  // complete type the checker accepted for this context.
                  auto checked = s.echo_types_by_context.find(functional_context);
                  if (checked == s.echo_types_by_context.end())
                    checked = s.echo_types_by_context.find(string());
                  if (checked != s.echo_types_by_context.end() &&
                      argument < checked->second.size() &&
                      starts_with(checked->second[argument], "vector[")) {
                    const string& full = checked->second[argument];
                    element = native_echo_element(full.substr(7, full.size() - 8));
                  }
                }
                if (!element)
                  throw CompileError(s.line, "internal error: echo of '" + shown +
                      "' has no resolved element type for native display",
                      "ECHO_DISPLAY_UNSUPPORTED");
                rendered = "__moss_display_vector(&(" + rendered + "), &" + *element + ")";
              }
              o << ", " << rendered;
            }
            o << ");\n";
          }
          ++i;
          break;
        }
        case Stmt::Kind::Assign: {
          if (s.a == "_") {
            o << indent(level) << "let _ = "
              << expr(s.b, d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0))
              << ";\n";
            ++i;
            break;
          }
          if (phase21_option_slots_.count(s.a)) {
            o << indent(level) << "__moss_recovery_slot_" << s.a
              << " = Some(" << expr(s.b, d, locals, &types,
                     statement_functional_pipeline_id(
                         s, functional_context, 0)) << ");\n";
            ++i;
            break;
          }
          if (auto sd = domain_construction(s.b)) {
            const string source_domain = *sd;
            construction_binding_ = s.a;
            remember_domain_instance_binding(s.a, source_domain, s.a);
            auto specialized = specialization_names_.find(*sd + "\n" + s.a);
            if (specialized != specialization_names_.end()) *sd = specialized->second;
            bool existing_binding = locals.count(s.a);
            o << indent(level) << (existing_binding ? "" : "let ") << s.a << " = ";
            emit_domain_construction(o, *sd, s.b, d, locals, &types);
            locals.insert(s.a);
            types[s.a] = *sd;
            ++i;
            break;
          }
          string lhs;
          string lhs_base, lhs_index;
          string index_temporary;
          if (tree_parse_index(s.a, lhs_base, lhs_index)) {
            string root_id = lhs_base;
            auto dot_pos = root_id.find('.');
            if (dot_pos != string::npos) root_id = root_id.substr(0, dot_pos);
            bool conflicts_with_base = !root_id.empty() && tree_expression_mentions_identifier(lhs_index, root_id);
            bool reads_sibling = false;
            if (view_method_ && d && is_pure_state_read_expression(lhs_index)) {
              for (const auto& field : d->state) {
                if (field.name != lhs_base &&
                    tree_expression_mentions_identifier(lhs_index, field.name)) {
                  reads_sibling = true;
                  break;
                }
              }
            }
            if (conflicts_with_base || reads_sibling) {
              index_temporary = "__moss_index_" +
                  std::to_string(call_argument_temp_++);
              o << indent(level) << "let " << index_temporary << " = "
                << expr(lhs_index, d, locals, &types) << ";\n";
            }
          }
          if (tree_parse_index(s.a, lhs_base, lhs_index)) {
            bool string_index = lhs_index.size() >= 2 && lhs_index.front() == '"' &&
                lhs_index.back() == '"';
            string ir = !index_temporary.empty() ? index_temporary :
                (string_index ? lhs_index : expr(lhs_index, d, locals, &types));
            auto base_type = generated_expr_type(lhs_base, &types);
            bool map_index = base_type &&
                (canonical_type_name(*base_type) == "map" ||
                 starts_with(canonical_type_name(*base_type), "map["));
            if (!string_index && !map_index) ir = "(" + ir + ") as usize";
            lhs = "(" + write_call_place(lhs_base, d, locals, &types) + ")[" + ir + "]";
          }
          else lhs = write_call_place(s.a, d, locals, &types);
          bool state_field = d && std::any_of(d->state.begin(), d->state.end(),
                                              [&](const Field& field) { return field.name == s.a; });
          if (tree_plain_identifier(s.a) && !locals.count(s.a) && !state_field) {
            backend_comment(o, (base + level) * 4,
                            "LOCAL assignment: introduce an inferred Moss binding");
            string annotation;
            string inferred_map_type;
            if (staged_abi_v10() && tree_empty_constructor_named(s.b, "Map") &&
                !starts_with(statement_type, "map[")) {
              // A bare Map constructor has no intrinsic element types. The
              // checked body may close it at a later indexed assignment;
              // preserve that evidence before emitting its Rust declaration.
              for (size_t next = i + 1; next < ss.size(); ++next) {
                string map_base, index_expression;
                if (ss[next].kind != Stmt::Kind::Assign ||
                    !tree_parse_index(ss[next].a, map_base, index_expression) ||
                    map_base != s.a) continue;
                auto key_type = generated_expr_type(index_expression, &types);
                auto value_type = generated_expr_type(ss[next].b, &types);
                if (key_type && value_type && shared_protocol_map_key(*key_type)) {
                  inferred_map_type = "map[" + canonical_type_name(*key_type) +
                      "," + canonical_type_name(*value_type) + "]";
                  break;
                }
              }
            }
            if (starts_with(statement_type, "map[") && ends_with(statement_type, "]"))
              annotation = rust_type(statement_type);
            else if (!inferred_map_type.empty()) annotation = rust_type(inferred_map_type);
            else if (canonical_type_name(statement_type) == "float" ||
                     canonical_type_name(generated_expr_type(s.b, &types).value_or("")) == "float")
              annotation = "f64";
            o << indent(level) << "let mut " << s.a;
            if (!annotation.empty()) o << ": " << annotation;
            o << " = "
              << expr(s.b, d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0))
              << ";\n";
            locals.insert(s.a);
            auto generated_type = generated_expr_type(s.b, &types);
            types[s.a] = !inferred_map_type.empty() ? inferred_map_type
                : (staged_abi_v10() && generated_type && *generated_type == "set" &&
                   starts_with(statement_type, "set[")) ? statement_type
                : generated_type ? *generated_type
                : !statement_type.empty() ? statement_type
                : tree_empty_constructor_named(s.b, "Map") ? "map"
                : tree_empty_constructor_named(s.b, "Queue") ? "queue"
                : staged_abi_v10() && tree_empty_constructor_named(s.b, "Set") ? "set"
                : (tree_parse_vector_literal(s.b) ? "vector" : "_value");
            auto source_binding = domain_instance_bindings_.find(trim(s.b));
            if (source_binding != domain_instance_bindings_.end())
              remember_domain_instance_binding(
                  s.a, source_binding->second.source_domain,
                  source_binding->second.instance);
            else
              forget_domain_instance_binding(s.a);
          } else {
            string mb, mi;
            bool map_assignment = tree_parse_index(s.a, mb, mi);
            auto map_assignment_type = map_assignment
                ? generated_expr_type(mb, &types) : std::optional<string>{};
            if (map_assignment && map_assignment_type &&
                (canonical_type_name(*map_assignment_type) == "map" ||
                 starts_with(canonical_type_name(*map_assignment_type), "map["))) {
              string key = mi.size() >= 2 && mi.front() == '"' && mi.back() == '"'
                  ? "MossStr::from(" + mi + ")" : expr(mi, d, locals, &types);
              auto key_type = generated_expr_type(mi, &types);
              if (key_type && shared_nominal_map_key(*key_type) &&
                  is_object_view(mi, d, locals, &types))
                key = "(" + key + ").__moss_value()";
              else if (key_type && !copy_type(*key_type) &&
                  !(mi.size() >= 2 && mi.front() == '"' && mi.back() == '"'))
                key = "(" + key + ").clone()";
              string inserted = expr(s.b, d, locals, &types,
                  statement_functional_pipeline_id(s, functional_context, 0));
              if (borrowed_function_parameters_.count(trim(s.b)))
                inserted = "(" + inserted + ").clone()";
              if (auto shared_key =
                      shared_protocol_map_key_from_type(*map_assignment_type)) {
                o << indent(level) << "{ let __moss_key = " << key
                  << "; (" << write_call_place(mb, d, locals, &types)
                  << ").insert_with(" << shared_map_hash(*shared_key, "__moss_key")
                  << ", __moss_key, " << inserted << ", |a, b| "
                  << shared_map_equal(*shared_key, "a", "b") << "); }\n";
              } else {
                o << indent(level) << "(" << write_call_place(mb, d, locals, &types)
                  << ").insert(" << key << ", "
                  << inserted
                  << ");\n";
              }
            }
            else if (is_object_view(s.a, d, locals, &types))
              o << indent(level) << "(" << lhs << ").__moss_replace(" << expr(s.b, d, locals, &types, statement_functional_pipeline_id(s, functional_context, 0)) << ");\n";
            else {
              string assigned;
              vector<string> concat_operands;
              std::function<void(string)> collect_concat = [&](string source) {
                source = tree_strip_expression_grouping(std::move(source));
                auto binary = tree_generated_binary(source, {"+"});
                auto left_type = binary
                    ? generated_expr_type(binary->left, &types) : std::nullopt;
                auto right_type = binary
                    ? generated_expr_type(binary->right, &types) : std::nullopt;
                if (binary && left_type && right_type &&
                    canonical_type_name(*left_type) == "string" &&
                    canonical_type_name(*right_type) == "string") {
                  collect_concat(binary->left);
                  collect_concat(binary->right);
                } else {
                  concat_operands.push_back(std::move(source));
                }
              };
              collect_concat(s.b);
              bool reuse_string_buffer = tree_plain_identifier(s.a) &&
                  locals.count(s.a) && concat_operands.size() > 1 &&
                  tree_expressions_equal(concat_operands.front(), s.a) &&
                  canonical_type_name(types.count(s.a) ? types.at(s.a) : "") ==
                      "string";
              for (size_t operand = 1;
                   reuse_string_buffer && operand < concat_operands.size();
                   ++operand)
                if (tree_expression_mentions_identifier(concat_operands[operand], s.a))
                  reuse_string_buffer = false;
              if (reuse_string_buffer) {
                std::ostringstream reused;
                reused << "{ ";
                for (size_t operand = 1; operand < concat_operands.size(); ++operand) {
                  const string part = trim(concat_operands[operand]);
                  const bool literal = part.size() >= 2 &&
                      part.front() == '"' && part.back() == '"';
                  reused << "let __moss_append_" << operand << " = ";
                  if (literal) reused << part << "; ";
                  else
                    reused << "&(" << expr(concat_operands[operand], d, locals, &types)
                           << "); ";
                }
                string place = write_call_place(s.a, d, locals, &types);
                reused << "(" << place << ").reserve(";
                for (size_t operand = 1; operand < concat_operands.size(); ++operand) {
                  if (operand > 1) reused << ".saturating_add(";
                  reused << "__moss_append_" << operand << ".len()";
                }
                for (size_t operand = 2; operand < concat_operands.size(); ++operand)
                  reused << ")";
                reused << "); ";
                for (size_t operand = 1; operand < concat_operands.size(); ++operand)
                  reused << "(" << place << ").push_str(__moss_append_"
                         << operand
                         << (trim(concat_operands[operand]).size() >= 2 &&
                             trim(concat_operands[operand]).front() == '"' &&
                             trim(concat_operands[operand]).back() == '"'
                                 ? "); " : ".as_str()); ");
                reused << "}";
                assigned = reused.str();
              } else {
                assigned = expr(s.b, d, locals, &types,
                    statement_functional_pipeline_id(s, functional_context, 0));
              }
              if (map_assignment && borrowed_function_parameters_.count(trim(s.b)))
                assigned = "(" + assigned + ").clone()";
              if (reuse_string_buffer)
                o << indent(level) << assigned << ";\n";
              else
                o << indent(level) << lhs << " = " << assigned << ";\n";
            }
            if (tree_plain_identifier(s.a)) forget_domain_instance_binding(s.a);
          }
          ++i;
          break;
        }
        case Stmt::Kind::Call: {
          auto checked_receiver = s.receiver_types_by_context.find(functional_context);
          if (checked_receiver == s.receiver_types_by_context.end())
            checked_receiver = s.receiver_types_by_context.find("");
          if (checked_receiver != s.receiver_types_by_context.end() && types.count(s.a))
            types[s.a] = checked_receiver->second;
          string statement_receiver_type = !s.b.empty()
              ? canonical_type_name(generated_expr_type(s.a, &types).value_or("")) : "";
          if (!s.b.empty() &&
              (statement_receiver_type == "string" ||
               statement_receiver_type == "vector" ||
               starts_with(statement_receiver_type, "vector[") ||
               statement_receiver_type == "queue" ||
               starts_with(statement_receiver_type, "queue[") ||
               (staged_abi_v10() &&
                (statement_receiver_type == "set" ||
                 starts_with(statement_receiver_type, "set["))) ||
               (s.b == "delete" && (statement_receiver_type == "map" ||
                                    starts_with(statement_receiver_type, "map["))))) {
            string call = s.a + "." + s.b + "(";
            for (size_t argument = 0; argument < s.args.size(); ++argument) {
              if (argument) call += ", ";
              call += s.args[argument];
            }
            call += ")";
            o << indent(level) << expr(call, d, locals, &types) << ";\n";
            ++i;
            break;
          }
          if (s.b.empty() && s.a == "assert") {
            o << indent(level) << "if !("
              << expr(s.args.front(), d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0))
              << ") { panic!(\"{}\", "
              << rust_string_literal(
                     "Moss assertion failed at line " +
                     std::to_string(s.line) + ": " + s.text)
              << "); }\n";
            ++i;
            break;
          }
          if (s.b.empty() && s.a == "assertEqual") {
            size_t id = assertion_temp_++;
            string actual = "__moss_assert_actual_" + std::to_string(id);
            string expected = "__moss_assert_expected_" + std::to_string(id);
            o << indent(level) << "let " << actual << " = &("
              << expr(s.args[0], d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0))
              << ");\n";
            o << indent(level) << "let " << expected << " = &("
              << expr(s.args[1], d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 1))
              << ");\n";
            string escaped_text;
            for (char ch : s.text) {
              if (ch == '{') escaped_text += "{{";
              else if (ch == '}') escaped_text += "}}";
              else escaped_text += ch;
            }
            string message = "Moss assertion failed at line " +
                std::to_string(s.line) + ": " + escaped_text +
                "; actual={:?}, expected={:?}";
            string cond = actual + " != " + expected;
            o << indent(level) << "if " << cond
              << " { panic!(" << rust_string_literal(message) << ", "
              << actual << ", " << expected << "); }\n";
            ++i;
            break;
          }
          if (s.b.empty() && s.a == "sort" && !functions_.count(s.a) &&
              (s.args.size() == 1 || s.args.size() == 2)) {
            o << indent(level) << sort_call_rust(s.args, d, locals, &types) << ";\n";
            ++i;
            break;
          }
          string function_name = resolved_callable_identity(s.a, &types);
          const Function* function_target = nullptr;
          if (starts_with(function_name, "callable-v1:")) {
            function_target = find_function_by_callable_id(function_name);
          } else {
            auto it = functions_.find(function_name);
            if (it != functions_.end()) function_target = it->second;
          }
          bool known_function = (function_target != nullptr);
          if (s.b.empty() && known_function && !benchmark_body_) {
            o << indent(level)
              << render_known_function_call(*function_target, function_target->name, s.args,
                                           d, locals, &types)
              << ";\n";
            ++i;
            break;
          }
          bool implicit_method = in_function && d && objects_.count(d->name) &&
              s.b.empty() && !known_function;
          auto call_receiver_type = implicit_method
              ? std::optional<string>(d->name)
              : !s.b.empty() ? generated_expr_type(s.a, &types)
                             : std::optional<string>{};
          const Method* call_method = nullptr;
          if (call_receiver_type && (!s.b.empty() || implicit_method))
            call_method = resolve_object_method(
                objects_, canonical_type_name(*call_receiver_type),
                implicit_method ? s.a : s.b, [&]() {
                  vector<string> result;
                  for (const auto& argument : s.args)
                    result.push_back(nominalized_generated_argument_type(
                        argument, generated_expr_type(argument, &types).value_or("")));
                  return result;
                }(), true, nullptr);
          const auto external_method_module = call_method && call_receiver_type
              ? phase21_external_module_for_symbol(
                    canonical_type_name(*call_receiver_type))
              : std::optional<string>{};
          const bool standard_write_bridge = external_method_module &&
              std::any_of(call_method->params.begin(), call_method->params.end(),
                  [&](const Param& parameter) {
                    const size_t index = static_cast<size_t>(
                        &parameter - call_method->params.data());
                    return !standard_error_variants(canonical_type_name(
                        parameter.type)).empty() &&
                        method_effect(*call_method, index) == Effect::Write;
                  });
          vector<string> argument_temporaries(s.args.size());
          if (call_method && call_method->receiver_effect != Effect::Read) {
            for (size_t k = 0; k < s.args.size(); ++k) {
              bool same_receiver_mutating_argument = false;
              if (!s.b.empty()) {
                string nested_receiver, nested_method;
                vector<string> nested_args;
                same_receiver_mutating_argument =
                    tree_parse_member_call(s.args[k], nested_receiver, nested_method,
                                      nested_args) &&
                    tree_expressions_equal(nested_receiver, s.a);
              } else if (implicit_method) {
                string nested_name;
                vector<string> nested_args;
                if (tree_parse_simple_call(s.args[k], nested_name, nested_args)) {
                  vector<string> nested_types;
                  for (const auto& argument : nested_args)
                    nested_types.push_back(nominalized_generated_argument_type(
                        argument, generated_expr_type(argument, &types).value_or("")));
                  same_receiver_mutating_argument =
                      resolve_object_method(objects_, d->name, nested_name,
                                            nested_types, true, nullptr) != nullptr;
                }
              }
              if (!same_receiver_mutating_argument) continue;
              argument_temporaries[k] = "__moss_call_argument_" +
                  std::to_string(call_argument_temp_++);
              string temporary_type =
                  generated_expr_type(s.args[k], &types).value_or("_value");
              o << indent(level) << "let " << argument_temporaries[k] << " = "
                << expr(s.args[k], d, locals, &types,
                        statement_functional_pipeline_id(
                            s, functional_context, k))
                << ";\n";
              locals.insert(argument_temporaries[k]);
              types[argument_temporaries[k]] = temporary_type;
            }
          }
          string method_receiver_expression = (implicit_method ? "self." : "") +
            (s.b.empty() && known_function
                    ? viewed_function_name(function_target->name, s.args, d, locals, &types)
                    : !s.b.empty() ? method_receiver_place(s.a, s.b, s.args, d, locals, &types)
                    : expr(s.a, d, locals, &types));
          std::ostringstream bridge_before, bridge_after, call_output;
          call_output << (standard_write_bridge ? "__moss_bridge_receiver" :
              method_receiver_expression);
          if (!s.b.empty()) {
            string method = s.b;
            auto it = types.find(s.a);
            if (it != types.end() && collection_named(parsed_type(it->second), {"queue"}))
              method = method == "push" ? "push_back" : method == "pop" ? "pop_front" : method;
            call_output << "." << viewed_method_name(s.a, method, s.args, d, locals, &types);
          }
          call_output << "(";
          size_t emitted_arguments = 0;
          for (size_t k = 0; k < s.args.size(); ++k) {
            bool compile_time_callable = known_function &&
                is_compile_time_callable(generated_expr_type(s.args[k], &types).value_or(""));
            if (compile_time_callable) continue;
            if (emitted_arguments++) call_output << ", ";
            if (benchmark_body_) call_output << "std::hint::black_box(";
            string argument_expression = argument_temporaries[k].empty()
                ? s.args[k] : argument_temporaries[k];
            if (known_function) {
              call_output << function_call_argument(*function_target, k, argument_expression, d,
                                          locals, &types,
                                          statement_functional_pipeline_id(
                                              s, functional_context, k),
                                          call_specialization(*function_target,
                                                              s.args, &types));
            } else if (!s.b.empty() || implicit_method) {
              if (call_method) {
                string rendered = method_call_argument(
                    *call_method, k, argument_expression, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, k));
                bool standard_write_argument = false;
                if (external_method_module && k < call_method->params.size() &&
                    !standard_error_variants(canonical_type_name(
                        call_method->params[k].type)).empty()) {
                  const string local_type =
                      canonical_type_name(call_method->params[k].type);
                  const string external_type = tooling_name("moss_" +
                      *external_method_module) + "::" + local_type;
                  const Effect effect = method_effect(*call_method, k);
                  if (effect == Effect::Write) {
                    standard_write_argument = true;
                    const string prefix = "__moss_standard_write_" +
                        std::to_string(call_argument_temp_++);
                    const string local_ref = prefix + "_local";
                    const string provider_value = prefix + "_provider";
                    bridge_before << "let " << local_ref << " = " << rendered
                      << "; let mut " << provider_value << " = "
                      << external_type << "::from((*" << local_ref
                      << ").clone()); ";
                    bridge_after << "*" << local_ref << " = " << local_type
                      << "::from(" << provider_value << "); ";
                    rendered = "&mut " + provider_value;
                  } else
                    rendered = effect == Effect::Read
                        ? "&" + external_type + "::from((*(" + rendered +
                            ")).clone())"
                        : external_type + "::from(" + rendered + ")";
                }
                if (standard_write_bridge && !standard_write_argument) {
                  const string staged = "__moss_call_argument_" +
                      std::to_string(call_argument_temp_++);
                  bridge_before << "let " << staged << " = " << rendered
                    << "; ";
                  rendered = staged;
                }
                call_output << rendered;
              } else
                call_output << expr(argument_expression, d, locals, &types,
                          statement_functional_pipeline_id(
                              s, functional_context, k));
            } else {
              call_output << expr(s.args[k], d, locals, &types,
                        statement_functional_pipeline_id(
                            s, functional_context, k));
            }
            if (benchmark_body_) call_output << ")";
          }
          call_output << ")";
          string lowered_call = call_output.str();
          if (standard_write_bridge) {
            string receiver_binding = call_method->receiver_effect == Effect::Consume
                ? method_receiver_expression
                : (call_method->receiver_effect == Effect::Write ? "&mut (" :
                    "&(") + method_receiver_expression + ")";
            lowered_call = "{ let __moss_bridge_receiver = " +
                receiver_binding + "; " + bridge_before.str() +
                "let __moss_bridge_result = " + lowered_call + "; " +
                bridge_after.str() + "__moss_bridge_result }";
          }
          if (call_method && !call_method->observable_effects.raise_set.empty()) {
            if (!phase21_result_context_)
              throw std::runtime_error(
                  "internal error: checked raising method statement lacks a Phase 21 outcome context");
            lowered_call = phase21_propagate(lowered_call);
          }
          o << indent(level);
          if (benchmark_body_) o << "std::hint::black_box(";
          o << lowered_call;
          auto receiver_type = !s.b.empty()
              ? generated_expr_type(s.a, &types) : std::optional<string>{};
          if (!s.b.empty() && s.b == "pop" && receiver_type &&
              (starts_with(canonical_type_name(*receiver_type), "vector[") ||
               starts_with(canonical_type_name(*receiver_type), "queue[")))
            o << ".unwrap_or_else(|| std::process::abort())";
          if (benchmark_body_) o << ")";
          o << ";\n";
          ++i;
          break;
        }
        case Stmt::Kind::Let:
        case Stmt::Kind::Var: {
          bool joined_assignment = join_assignments.erase(s.a) != 0;
          auto sd = domain_construction(s.b);
          if (sd) {
            const string source_domain = *sd;
            construction_binding_ = s.a;
            remember_domain_instance_binding(s.a, source_domain, s.a);
            auto specialized = specialization_names_.find(*sd + "\n" + s.a);
            if (specialized != specialization_names_.end()) *sd = specialized->second;
            o << indent(level);
            if (!joined_assignment)
              o << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "");
            o << s.a << " = ";
            emit_domain_construction(o, *sd, s.b, d, locals, &types);
            types[s.a] = *sd;
          } else {
            auto source = types.find(trim(s.b));
            bool domain_capability = source != types.end() && domains_.count(source->second);
            string inferred_map_type;
            if (staged_abi_v10() && tree_empty_constructor_named(s.b, "Map") &&
                !starts_with(statement_type, "map[")) {
              for (size_t next = i + 1; next < ss.size(); ++next) {
                string map_base, index_expression;
                if (ss[next].kind != Stmt::Kind::Assign ||
                    !tree_parse_index(ss[next].a, map_base, index_expression) ||
                    map_base != s.a) continue;
                auto key_type = generated_expr_type(index_expression, &types);
                auto value_type = generated_expr_type(ss[next].b, &types);
                if (key_type && value_type && shared_protocol_map_key(*key_type)) {
                  inferred_map_type = "map[" + canonical_type_name(*key_type) +
                      "," + canonical_type_name(*value_type) + "]";
                  break;
                }
              }
            }
            o << indent(level);
            if (!joined_assignment)
              o << "let " << (s.kind == Stmt::Kind::Var ? "mut " : "");
            o << s.a;
            if (!joined_assignment && !domain_capability &&
                (canonical_type_name(statement_type) == "float" ||
                 canonical_type_name(generated_expr_type(s.b, &types).value_or("")) == "float"))
              o << ": f64";
            else if (!joined_assignment && !domain_capability &&
                     (starts_with(statement_type, "map[") || !inferred_map_type.empty()))
              o << ": " << rust_type(!inferred_map_type.empty()
                  ? inferred_map_type : statement_type);
            o << " = "
              << expr(s.b, d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0));
            if (domain_capability) o << ".clone()";
            o << ";\n";
            auto generated_type = generated_expr_type(s.b, &types);
            bool unspecialized_collection = generated_type &&
                (*generated_type == "vector" || *generated_type == "queue" ||
                 *generated_type == "map" ||
                 (staged_abi_v10() && *generated_type == "set"));
            types[s.a] = domain_capability ? source->second
                : !inferred_map_type.empty() ? inferred_map_type
                : (unspecialized_collection && !statement_type.empty() && statement_type != *generated_type)
                    ? statement_type
                : generated_type ? *generated_type
                : !statement_type.empty() ? statement_type : "_value";
            if (domain_capability) {
              auto source_binding = domain_instance_bindings_.find(trim(s.b));
              if (source_binding != domain_instance_bindings_.end())
                remember_domain_instance_binding(
                    s.a, source_binding->second.source_domain,
                    source_binding->second.instance);
              else
                forget_domain_instance_binding(s.a);
            } else {
              forget_domain_instance_binding(s.a);
            }
          }
          locals.insert(s.a);
          ++i;
          break;
        }
        case Stmt::Kind::Message: {
          string call = "message " + s.a + "." + s.b + "(";
          for (size_t argument = 0; argument < s.args.size(); ++argument) {
            if (argument) call += ", ";
            call += s.args[argument];
          }
          call += ")";
          o << indent(level);
          // `field = message ...` in a handler writes the domain state field
          // unless a local binding of that name is in scope.
          const bool state_result = d && !s.message_result.empty() &&
              !locals.count(s.message_result) &&
              std::any_of(d->state.begin(), d->state.end(), [&](const Field& field) {
                return field.name == s.message_result;
              });
          const bool state_view = state_result &&
              is_object_view(s.message_result, d, locals, &types);
          if (state_view) {
            o << "(" << write_call_place(s.message_result, d, locals, &types)
              << ").__moss_replace(";
          } else if (state_result) {
            o << write_call_place(s.message_result, d, locals, &types) << " = ";
          } else if (!s.message_result.empty()) {
            if (!locals.count(s.message_result)) o << "let mut ";
            o << s.message_result << " = ";
          }
          vector<size_t> argument_plans;
          for (size_t argument = 0; argument < s.args.size(); ++argument)
            argument_plans.push_back(statement_functional_pipeline_id(s, functional_context, argument));
          o << expr(call, d, locals, &types, 0, &argument_plans)
            << (state_view ? ")" : "") << ";\n";
          if (!s.message_result.empty() && !state_result) {
            const Domain& target = *domains_.at(canonical_type_name(types.at(s.a)));
            locals.insert(s.message_result);
            types[s.message_result] = find_handler(target, s.b)->reply_type.value_or("unit");
          }
          ++i;
          break;
        }
        case Stmt::Kind::Reply:
          if (!current_handler || !current_handler->reply_type || reply_slot.empty())
            throw std::runtime_error("internal error: unchecked reply reached code generation");
          o << indent(level) << reply_slot << " = Some("
            << message_arg(s.a, *current_handler->reply_type, d, locals, &types,
                statement_functional_pipeline_id(s, functional_context, 0)) << ");\n";
          o << indent(level) << "break 'handler;\n";
          ++i;
          // `reply` terminates the handler.  Do not emit statements that are
          // lexically after an unconditional reply: rustc would otherwise
          // diagnose the generated code as unreachable, even though Moss's
          // control-flow checker has already established the terminating
          // semantics.
          while (i < ss.size() && ss[i].indent >= level) ++i;
          break;
        case Stmt::Kind::Return:
          if (in_function && !s.a.empty()) {
            string rendered = expr(s.a, d, locals, &types,
                      statement_functional_pipeline_id(
                          s, functional_context, 0));
            string expr_trimmed = trim(s.a);
            if (view_parameters_.count(expr_trimmed)) {
              rendered = expr_trimmed + ".__moss_value()";
            } else if (types.count(expr_trimmed) && view_object_type(types.at(expr_trimmed))) {
              rendered = "(" + rendered + ").clone()";
            }
            o << indent(level) << "return ";
            const bool returns_result = phase21_return_result_context_.value_or(
                phase21_result_context_);
            if (returns_result) o << "Ok(";
            o << rendered;
            if (returns_result) o << ")";
            o << ";\n";
          }
          else
            o << indent(level) << (in_handler ? "break 'handler;" : "return;") << "\n";
          ++i;
          break;
        case Stmt::Kind::Pass:
          o << indent(level) << "let _ = ();\n";
          ++i;
          break;
        case Stmt::Kind::Raw:
          o << indent(level);
          // A bare expression is a valid benchmark body.  Protect its value
          // just like an ordinary call result; otherwise rustc may erase the
          // very arithmetic or functional pipeline being measured.
          if (benchmark_body_) o << "std::hint::black_box(";
          o << expr(s.text, d, locals, &types,
                    statement_functional_pipeline_id(
                        s, functional_context, 0));
          if (benchmark_body_) o << ")";
          o << ";\n";
          ++i;
          break;
        case Stmt::Kind::Else:
          return;
      }
    }
  }

  std::optional<string> domain_construction(const string& expr) const {
    string e = trim(expr);
    string callee; vector<string> args;
    if (!tree_parse_simple_call(e, callee, args)) return std::nullopt;
    return domains_.count(callee) ? std::optional<string>(callee) : std::nullopt;
  }

  void emit_domain_construction(std::ostringstream& o, const string& domain,
                       const string& expression, const Domain* context,
                       const std::set<string>& locals,
                       const std::unordered_map<string,string>* types) const {
    auto found = domains_.find(domain);
    if (found == domains_.end()) {
      throw std::runtime_error("internal error: unknown concrete domain constructor");
    }
    const Domain& definition = *found->second;
    o << "construct_" << snake_case(domain) << "(";
    string constructor = trim(expression);
    string callee; vector<string> arguments;
    std::unordered_map<string,string> named;
    if (tree_parse_simple_call(constructor, callee, arguments)) {
      for (const auto& argument : arguments) {
        string name, value;
        if (tree_parse_named_argument(argument, name, value)) named[name] = value;
      }
    }
    bool constructor_argument = false;
    {
      for (const auto& route : definition.routes) {
        auto value = named.find(route.name);
        if (value == named.end())
          throw std::runtime_error("internal error: missing route binding during lowering");
        // The checked graph has already proved nominal route compatibility.
        // Adapt the concrete storage reference to the existing route handle;
        // never use a generated type spelling to establish compatibility.
        if (constructor_argument) o << ", ";
        constructor_argument = true;
        o << "(" << expr(value->second, context, locals, types) << ").clone().into()";
      }
      for (const auto& field : definition.state) {
        auto value = named.find(field.name);
        if (constructor_argument) o << ", ";
        constructor_argument = true;
        o << (value == named.end()
            ? (field.init.empty() ? default_value(field.type)
                                  : expr(field.init, context, locals, types))
            : expr(value->second, context, locals, types));
      }
    }
    if (constructor_argument) o << ", ";
    const auto& plan = physical_domain_plan(construction_binding_);
    o << rust_string_literal(plan.concrete_instance_id) << ", " << plan.domain_rank;
    for (const auto& h : definition.handlers) {
      auto found_handler = std::find_if(plan.handlers.begin(), plan.handlers.end(),
          [&](const auto& candidate) { return candidate.name == h.name; });
      synchronization_require(found_handler != plan.handlers.end(), "missing typed handler identity");
      o << ", " << rust_string_literal(found_handler->handler_identity);
    }
    o << ");\n";
  }

  const Handler* find_handler(const Domain& d, const string& name) const {
    for (const auto& h : d.handlers) if (h.name == name) return &h;
    return nullptr;
  }

};

std::string generate_native(
    const Program& program, const OptimizationPlan& plan, bool debug_build,
    ProgramGenerationMode mode, vector<string> rust_dependencies,
    const Program* resolution_program, bool project_module_artifact,
    bool emit_static_specializations, bool public_specializations) {
  return Generator(program, plan, debug_build, mode,
                   std::move(rust_dependencies), resolution_program,
                   project_module_artifact, emit_static_specializations,
                   public_specializations).generate();
}

}  // namespace moss
