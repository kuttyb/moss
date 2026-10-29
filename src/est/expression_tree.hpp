#pragma once

#include <cassert>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace moss { struct Program; struct Stmt; }
namespace moss::est {

using ExprId = std::uint32_t;

// Semantic roles, independent of the legacy field containing the spelling.
enum class ExprSlot : std::uint8_t {
  Condition, Initializer, ReturnValue, Iterable, Receiver, Argument,
  LhsPlace, RhsValue, Scrutinee, Pattern, RaisedValue, Expression
};
enum class RawForm : std::uint8_t { Assign, CompoundAssign, ExprStmt };

struct Handle {
  ExprSlot slot;
  std::uint32_t argument_index = 0;
  ExprId expr = 0;
  std::uint64_t source_fingerprint = 0;
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
using Node = std::variant<Literal, Identifier, Unary, Binary, Call, Member,
                          Index, Parenthesized>;

class Arena {
 public:
  struct Count { std::uint64_t attempted = 0, built = 0, failed = 0; };
  static constexpr std::size_t slot_count = 12;
  ExprId append(Node node);
  const Node& at(ExprId id) const;
  std::size_t size() const { return nodes_.size(); }

  // Same-source copies retain handles and share this immutable arena.
  // A transformed or duplicated logical occurrence calls duplicate() and
  // binds the returned fresh ID to its owner. No old node is edited/erased.
  ExprId duplicate(ExprId id);
  void note(ExprSlot slot, bool success);
  const std::array<Count, slot_count>& coverage() const { return coverage_; }
  void rebuild(std::vector<Handle>& handles, ExprSlot slot,
               std::uint32_t argument_index, Node replacement,
               std::uint64_t fingerprint);

 private:
  std::vector<Node> nodes_;
  std::array<Count, slot_count> coverage_{};
};

std::uint64_t fingerprint(const std::string& source);
void assert_fresh(const Handle& handle, const std::string& source);

enum class Mode { Off, Shadow };
Mode mode();  // Rejects the reserved MOSS_EST=on setting.
ExprId parse_expression(Arena& arena, std::string_view source);
void populate(Program& program);

}  // namespace moss::est
