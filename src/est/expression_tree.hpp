#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace moss::est {

using ExprId = std::uint32_t;

// Semantic roles, independent of the legacy field containing the spelling.
enum class ExprSlot : std::uint8_t {
  Condition, Initializer, ReturnValue, Iterable, Receiver, Argument,
  LhsPlace, RhsValue, Scrutinee, Pattern, RaisedValue, Expression
};

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
  ExprId append(Node node);
  const Node& at(ExprId id) const;
  std::size_t size() const { return nodes_.size(); }

  // Same-source copies retain handles and share this immutable arena.
  // A transformed or duplicated logical occurrence calls duplicate() and
  // binds the returned fresh ID to its owner. No old node is edited/erased.
  ExprId duplicate(ExprId id);
  void rebuild(std::vector<Handle>& handles, ExprSlot slot,
               std::uint32_t argument_index, Node replacement,
               std::uint64_t fingerprint);

 private:
  std::vector<Node> nodes_;
};

std::uint64_t fingerprint(const std::string& source);
void assert_fresh(const Handle& handle, const std::string& source);

}  // namespace moss::est
