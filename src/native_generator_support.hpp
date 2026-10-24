#pragma once

#include "ast.hpp"
#include "functional_source_pipeline.hpp"
#include "numeric_literal.hpp"

#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace moss {

struct TypeEnv;

enum class ParameterPassingMode { Value, SharedBorrow, MutableBorrow };
struct ConcreteParameterABI {
  Effect effect;
  ParameterPassingMode passing;
};

enum class MethodResolutionFailure {
  None,
  UnknownReceiver,
  MissingMethod,
  WrongArity,
  IncompatibleArguments,
  Ambiguous
};

enum class BuiltinReceiverFamily {
  Vector, Map, Queue, Set, String, Char, Int, Float, Bool
};

struct BuiltinOperationDescriptor {
  BuiltinReceiverFamily receiver;
  const char* name;
  const char* signature;
  const char* result;
  Effect receiver_effect;
  std::vector<Effect> argument_effects;
  std::vector<std::string> argument_types;
  bool may_panic = false;
};

bool staged_abi_v10();
bool indexed_strings_staged();
std::string stable_hash(const std::string& value);
std::string tooling_name(std::string value);
std::string tooling_native_symbol(const std::string& kind,
                                  const std::string& name,
                                  const std::string& semantic_identity);
std::string rust_identifier(const std::string& value);
std::string snake_case(const std::string& value);

bool decimal_float_literal(const std::string& value);
bool callable_identity(const std::string& type);
bool callable_identity(const TypeRef& type);
bool contains_internal_placeholder(const std::string& type);
bool contains_internal_placeholder(const TypeRef& type);
bool is_nominal_instance_type(const std::string& type);
std::string nominal_template_name(const std::string& type);
std::vector<std::pair<std::string, std::string>>
nominal_field_substitutions(const std::string& type);
std::optional<std::string> nominal_field_type(const std::string& type,
                                              const std::string& field_name);
std::string standard_error_identity(const std::string& name);
std::vector<std::string> standard_error_variants(const std::string& identity);
std::optional<std::pair<std::string, std::string>>
map_key_value_types(const std::string& type);
std::optional<std::string> indexed_element(const TypeRef& type);
bool collection_named(const TypeRef& type,
                      std::initializer_list<std::string_view> names);
bool open_generic(const std::string& type);
bool open_generic(const TypeRef& type);
ConcreteParameterABI concrete_parameter_abi(Effect effect,
                                            const std::string& type);
const FunctionSpecialization* concrete_specialization(
    const Function& function, const std::vector<std::string>& types);
const FunctionSpecialization* functional_specialization(
    const Function& function, const std::vector<std::string>& types);
Effect sort_key_effect(const Function& function, const std::string& element);

bool parse_index(const std::string& text, std::string& base,
                 std::string& index);
std::optional<std::string> typed_empty_vector_constructor(
    const std::string& text);
bool is_single_string_literal(const std::string& value);
bool is_single_char_literal(const std::string& value);
bool parse_executor_chain(
    const std::string& text,
    std::vector<std::pair<std::string, std::vector<std::string>>>& calls);
const std::set<std::string>& executor_config_methods();
bool parse_fileio_open(const std::string& text,
                       std::vector<std::string>& arguments);
bool executor_typed(const TypeEnv& env, const std::string& name);
bool fileio_typed(const TypeEnv& env, const std::string& name);
bool executor_typed(const std::unordered_map<std::string, std::string>& env,
                    const std::string& name);
bool fileio_typed(const std::unordered_map<std::string, std::string>& env,
                  const std::string& name);
bool parse_fileio_chunks_source(const std::string& source,
                                std::string& fileio_receiver,
                                std::string& size_expression);
bool parse_enum_case_expression(const std::string& expression,
                                std::string& enum_name,
                                std::string& case_name,
                                std::vector<std::string>& fields,
                                bool& called);
bool parse_bound_method_callable(const std::string& text,
                                 std::string& receiver,
                                 std::string& method);
std::string strip_redundant_outer_parentheses(std::string value);
std::optional<std::vector<std::string>> parse_vector_literal(
    const std::string& expression);
bool parse_named_argument(const std::string& text, std::string& name,
                          std::string& value);
std::optional<ParsedFunctionalPipeline> parse_functional_pipeline(
    const std::string& expression);
std::string normalize_pipeline(std::string expression);

const Method* resolve_object_method(
    const std::unordered_map<std::string, ObjectType*>& objects,
    const std::string& type, const std::string& name,
    const std::vector<std::string>& argument_types,
    bool unknown_argument_is_wildcard, MethodResolutionFailure* failure);
const Method* resolve_object_method(
    const std::unordered_map<std::string, const ObjectType*>& objects,
    const std::string& type, const std::string& name,
    const std::vector<std::string>& argument_types,
    bool unknown_argument_is_wildcard, MethodResolutionFailure* failure);
Effect specialized_iterator_parameter_effect(
    const Function& function, std::size_t index,
    const std::string& concrete_type,
    const std::unordered_map<std::string, ObjectType*>& objects);
Effect specialized_iterator_parameter_effect(
    const Function& function, std::size_t index,
    const std::string& concrete_type,
    const std::unordered_map<std::string, const ObjectType*>& objects);

const BuiltinOperationDescriptor* builtin_operation(
    const std::string& receiver_type, const std::string& name);
const BuiltinOperationDescriptor* builtin_operation(
    const TypeRef& receiver_type, const std::string& name);
std::optional<std::string> builtin_operation_result(
    const std::string& receiver_type, const std::string& name,
    std::size_t arity);
std::optional<std::string> builtin_operation_result(
    const TypeRef& receiver_type, const std::string& name,
    std::size_t arity);

}
