#pragma once

#include <cassert>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <set>
#include <tuple>
#include <variant>
#include <vector>

#include "../constraints.hpp"
#include "../functional_source_pipeline.hpp"
#include "../type_ir.hpp"

namespace moss { struct Program; struct Stmt; }
namespace moss::est {

using ExprId = std::uint32_t;
struct SourceSpan {
  std::uint32_t line = 1, column = 1;
  std::uint64_t origin_id = 0;
  // Byte range within the source string passed to parse_expression. These are
  // compiler-internal navigation data; diagnostics continue to use line/column.
  std::uint32_t source_begin = 0, source_end = 0;
};

// Semantic roles, independent of the legacy field containing the spelling.
enum class ExprSlot : std::uint8_t {
  Condition, Initializer, ReturnValue, Iterable, Receiver, Argument,
  LhsPlace, RhsValue, Scrutinee, Pattern, RaisedValue, Expression
};
enum class RawForm : std::uint8_t { Assign, CompoundAssign, ExprStmt };

struct Handle {
  std::uint64_t owner = 0;
  ExprSlot slot;
  std::uint32_t argument_index = 0;
  ExprId expr = 0;
  std::uint64_t source_fingerprint = 0;
  std::uint64_t origin_id = 0;
  std::uint32_t arena_index = 0;
};

// Checker-owned facts are deliberately stored beside, not inside, immutable
// syntax nodes. Specializations of one source occurrence have separate keys.
struct FactKey {
  ExprId expression = 0;
  std::string specialization_context;
  bool operator<(const FactKey& other) const {
    return expression < other.expression ||
        (expression == other.expression &&
         specialization_context < other.specialization_context);
  }
};
struct SemanticFacts {
  TypeRef type;
  std::string specialization_result;
  std::string resolved_target;
  std::optional<Effect> inferred_effect;
};

// Child IDs always denote distinct occurrences. The arena never interns text.
struct Literal { std::string spelling; };
struct Identifier { std::string name; };
struct Unary { std::string op; ExprId operand; };
struct Binary { std::string op; ExprId left, right; };
struct Call { ExprId callee; std::vector<ExprId> arguments; };
struct Member { ExprId base; std::string name; };
struct Index { ExprId base, index; };
struct Parenthesized { ExprId inner; };
struct Message { ExprId target; std::string handler; std::vector<ExprId> arguments; };
struct NamedArgument { std::string name; ExprId value; char separator = '='; };
struct Placeholder {};
struct Collection { char delimiter; std::vector<ExprId> elements; };
struct Tuple { std::vector<ExprId> elements; };
struct Constructor { ExprId type; std::vector<ExprId> type_arguments;
                     std::vector<ExprId> arguments;
                     std::uint32_t callee_source_begin = 0;
                     std::uint32_t callee_source_end = 0; };
struct Pipeline { ExprId input; std::vector<ExprId> stages; };
using Node = std::variant<Literal, Identifier, Unary, Binary, Call, Member,
                          Index, Parenthesized, Message, NamedArgument,
                          Placeholder, Collection, Tuple, Constructor, Pipeline>;

enum class Outcome : std::uint8_t { Built, Deferred27_1b, RejectedSource, Failed };

class Arena {
 public:
  struct Count {
    std::uint64_t attempts = 0, unique = 0, built = 0, deferred_27_1b = 0,
                  rejected_source = 0, failed = 0;
  };
  static constexpr std::size_t slot_count = 12;
  ExprId append(Node node);
  ExprId append(Node node, SourceSpan span);
  const Node& at(ExprId id) const;
  SourceSpan span(ExprId id) const { return spans_.at(id); }
  std::size_t size() const { return nodes_.size(); }
  void record_facts(ExprId id, std::string specialization_context,
                    SemanticFacts facts);
  const SemanticFacts* facts(ExprId id,
                             std::string_view specialization_context) const;

  // Same-source copies retain handles and share this immutable arena.
  // A transformed or duplicated logical occurrence calls duplicate() and
  // binds the returned fresh ID to its owner. No old node is edited/erased.
  ExprId duplicate(ExprId id);
  void note(std::uint64_t owner, ExprSlot slot, std::uint32_t argument_index,
            Outcome outcome);
  const std::array<Count, slot_count>& coverage() const { return coverage_; }
  void rebuild(std::vector<Handle>& handles, std::uint64_t owner, ExprSlot slot,
               std::uint32_t argument_index, Node replacement,
               std::uint64_t fingerprint);

 private:
  std::vector<Node> nodes_;
  std::vector<SourceSpan> spans_;
  std::map<FactKey, SemanticFacts> semantic_facts_;
  std::array<Count, slot_count> coverage_{};
  std::set<std::tuple<std::uint64_t, std::uint8_t, std::uint32_t>> seen_;
};

// A query view keeps the exact source buffer paired with an immutable EST
// occurrence. `owned_arena` is populated only for on-demand queries that do
// not have a retained handle; existing checker/emitter paths should bind their
// retained arena and ExprId whenever one is available.
struct ExpressionView {
  std::shared_ptr<Arena> owned_arena;
  const Arena* arena = nullptr;
  ExprId root = 0;
  std::string source;

  std::optional<std::string> source_text(ExprId expression) const;
};

// Pattern syntax is represented separately from expression syntax. These
// nodes are immutable value records attached to the owning statement/arm;
// their child order is source order and no expression-text re-splitting is
// needed by checker or emitter consumers.
struct PatternNode {
  enum class Kind : std::uint8_t {
    Unknown, EnumTag, EnumCase, QualifiedRecoveryCase,
    GroupedAlternatives, RecoveryCatchall, RecoveryBinding
  } kind = Kind::Unknown;
  std::string type_name, case_name, binding;
  std::vector<std::string> payload_bindings;
  std::vector<PatternNode> alternatives;
  bool payload_pattern = false;
};
PatternNode match_pattern(std::string_view tag,
                          const std::vector<std::string>& bindings);
PatternNode recovery_pattern(std::string_view selector);
PatternNode qualified_recovery_pattern(std::string_view source);
ExpressionView view(const Arena& arena, ExprId root,
                    std::string_view source);
std::optional<ExpressionView> parse_view(std::string_view source);
std::optional<ExpressionView> parse_named_argument_view(std::string_view source);
ExprId unparenthesized(const Arena& arena, ExprId expression);
bool project_simple_call(const ExpressionView& view, std::string& callee,
                         std::vector<std::string>& arguments);
bool project_member_call(const ExpressionView& view, std::string& receiver,
                         std::string& member,
                         std::vector<std::string>& arguments,
                         bool* outputs_written = nullptr);
bool project_bound_method(const ExpressionView& view, std::string& receiver,
                          std::string& method);
bool project_member_access(const ExpressionView& view, std::string& base,
                           std::string& member);
std::string strip_grouping(const ExpressionView& view);
std::string normalize_pipeline_tree(const ExpressionView& view);
bool project_fileio_open(const ExpressionView& view,
                         std::vector<std::string>& arguments);
bool project_fileio_chunks_source(const ExpressionView& view,
                                  std::string& receiver,
                                  std::string& size_expression);
bool project_executor_chain(
    const ExpressionView& view,
    std::vector<std::pair<std::string, std::vector<std::string>>>& calls);
bool project_named_argument(const ExpressionView& view, std::string& name,
                            std::string& value);
std::optional<char> project_named_argument_separator(const ExpressionView& view);
bool project_index(const ExpressionView& view, std::string& base,
                   std::string& index);
std::optional<std::vector<std::string>> project_vector_literal(
    const ExpressionView& view);
std::optional<std::vector<std::string>> parse_vector_literal_children(
    std::string_view source);
bool project_enum_case(const ExpressionView& view, std::string& enum_name,
                       std::string& case_name,
                       std::vector<std::string>& fields,
                       bool* called = nullptr);
bool project_binary(const ExpressionView& view,
                    const std::vector<std::string>& operators,
                    std::pair<std::string, std::string>& operands,
                    std::string& operation);
std::optional<std::vector<std::string>> project_delimited_children(
    const ExpressionView& view, char delimiter);
std::optional<std::vector<std::string>> project_tuple_elements(
    const ExpressionView& view);
std::optional<std::vector<std::string>> parse_delimited_children(
    std::string_view source, char delimiter);
void assert_delimited_parity(const ExpressionView& view, char delimiter,
                             const std::optional<std::vector<std::string>>& tree);
bool project_identifier(const ExpressionView& view, std::string& name);
bool is_identifier(const ExpressionView& view);
bool identifier_named(const ExpressionView& view, std::string_view name);
bool expressions_equal(const ExpressionView& left, const ExpressionView& right);
bool contains_member_call(const ExpressionView& expression,
                          std::string_view receiver,
                          std::string_view member);
bool empty_vector_literal(const ExpressionView& view);
bool member_expression(const ExpressionView& view);
bool empty_constructor_named(const ExpressionView& view, std::string_view name);
bool placeholder_expression(const ExpressionView& view);
bool expression_mentions(const ExpressionView& view, std::string_view name);
bool structurally_equal(const ExpressionView& left,
                        const ExpressionView& right);

std::uint64_t fingerprint(const std::string& source);
void assert_fresh(const Handle& handle, const std::string& source);
const Handle* lookup(const std::vector<Handle>& handles, const Arena& arena,
                     std::uint64_t owner, ExprSlot slot,
                     std::uint32_t argument_index, const std::string& source);
ExprId fresh_occurrence(Arena& arena, ExprId root);
void rebuild_slot(Arena& arena, std::vector<Handle>& handles,
                  std::uint64_t owner, ExprSlot slot,
                  std::uint32_t argument_index, const std::string& new_text);

enum class Mode { Off, Shadow };
Mode mode();  // Rejects the reserved MOSS_EST=on setting.
ExprId parse_expression(Arena& arena, std::string_view source,
                        std::uint64_t origin_id = 0,
                        std::uint32_t base_line = 1,
                        std::uint32_t base_column = 1);
std::optional<ParsedFunctionalPipeline> functional_pipeline(
    const Arena& arena, ExprId expression, std::string_view source,
    const std::function<std::optional<FunctionalNodeKind>(
        std::string_view)>& resolve_stage);
void populate(Program& program);
void populate_loaded(Program& program, std::vector<Stmt>& statements,
                     std::uint64_t origin_id);
void adopt_program_arenas(Program& destination, Program& source);
std::uint64_t stable_origin_id(std::string_view identity);

}  // namespace moss::est
