#pragma once

#include "abi_v10_records.hpp"
#include "checked_body_ir.hpp"
#include "native_generator_support.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace moss {

struct FunctionalOperationDescriptor {
  const char* name;
  FunctionalNodeKind kind;
  const char* signature;
};

enum class BuiltinOperatorAdmission { Admitted, Deferred, Rejected };

struct CheckedPrivateChainBody {
  std::vector<checked_ir::Stmt> statements;
  std::vector<SemanticDependency> dependencies;
  std::string semantic_hash;
};

std::string canonical_type_name(const TypeRef& type);
std::string module_name_from_symbol(const std::string& symbol);
std::string source_code_only(std::string source);
bool bare_collection(const TypeRef& type,
                     std::initializer_list<std::string_view> names);
bool bare_matches_applied(const std::string& bare,
                          const std::string& applied);
TypeRef parsed_type(const TypeRef& type);
BuiltinOperatorAdmission admit_typed_arithmetic_operator(
    const std::string& operation, const TypeRef& subject,
    const TypeRef& argument, const TypeRef& result);
BuiltinOperatorAdmission admit_typed_numeric_comparison(
    const std::string& operation, const TypeRef& subject,
    const TypeRef& argument, const TypeRef& result);
const std::vector<FunctionalOperationDescriptor>& functional_operations();
std::optional<BuiltinReceiverFamily> builtin_receiver_family(
    const std::string& type);
std::optional<BuiltinReceiverFamily> builtin_receiver_family(
    const TypeRef& type);
std::optional<std::string> builtin_operation_type(
    const std::string& receiver, const std::string& pattern);
std::optional<std::pair<const Method*, const Method*>> nominal_key_methods(
    const ObjectType& object, const std::string& key_type);
bool nominal_key_free_target(const Function& function,
                             const std::string& key_type);
abi_v10::Record nominal_key_proof_record(
    const ObjectType& object, const std::string& declaration_id);
std::string keyed_operation_specialization_id(
    const std::string& key_type, const std::string& proof_id,
    const std::string& family, const std::string& operation);
std::optional<SpecializationKey> specialization_key_inputs(
    const std::string& callable_id, const std::vector<std::string>& types,
    const std::optional<TypeRef>& receiver = std::nullopt,
    const std::vector<std::pair<std::uint32_t, TypeRef>>& open_result_bindings = {});
std::string specialization_calling_module(
    const Program& program, const std::string& context,
    const std::string& root_module);
std::size_t top_level_field_dot(const std::string& text);
bool phase20_is_nontransferable_capability(const std::string& type);
bool phase20_allowed_as_ordinary_parameter(const std::string& type);
void build_type_context(Program& program);
bool abi_v10_effects_pure(const ObservableEffects& effects);
bool abi_v10_deferred_member_effects(const ObservableEffects& effects);
bool valid_checked_string_literal_spelling(const std::string& source);
std::optional<CheckedPrivateChainBody> common_checked_body(
    const Program& program, const Function& function);
TypeRef imported_abi_v10_type(const abi_v10::WireType& wire,
                              const TypeContext& context,
                              std::uint32_t variable_count,
                              unsigned depth = 0);
AccessMode imported_abi_v10_requirement_access(abi_v10::Access access);
std::vector<Requirement> imported_abi_v10_requirements(
    const abi_v10::Summary& summary, const Function& function,
    const TypeContext& context,
    std::vector<MemberDispatchRequirement>* member_requirements = nullptr);
void project_imported_h3_checked_bodies(Program& program);
void validate_imported_h3_summary_closure(Program& program);

}
