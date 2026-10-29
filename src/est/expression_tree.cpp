#include "expression_tree.hpp"

#include <stdexcept>
#include <type_traits>

namespace moss::est {

ExprId Arena::append(Node node) {
  if (nodes_.size() >= UINT32_MAX) throw std::overflow_error("EST arena full");
  const auto id = static_cast<ExprId>(nodes_.size());
  nodes_.push_back(std::move(node));
  return id;
}

const Node& Arena::at(ExprId id) const { return nodes_.at(id); }

ExprId Arena::duplicate(ExprId id) {
  const Node old = at(id);
  return std::visit([this](const auto& value) -> ExprId {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, Unary>)
      return append(Unary{value.op, duplicate(value.operand)});
    else if constexpr (std::is_same_v<T, Binary>)
      return append(Binary{value.op, duplicate(value.left), duplicate(value.right)});
    else if constexpr (std::is_same_v<T, Call>) {
      const auto callee = duplicate(value.callee);
      std::vector<ExprId> arguments;
      for (const auto argument : value.arguments) arguments.push_back(duplicate(argument));
      return append(Call{callee, std::move(arguments)});
    } else if constexpr (std::is_same_v<T, Member>)
      return append(Member{duplicate(value.base), value.name});
    else if constexpr (std::is_same_v<T, Index>)
      return append(Index{duplicate(value.base), duplicate(value.index)});
    else if constexpr (std::is_same_v<T, Parenthesized>)
      return append(Parenthesized{duplicate(value.inner)});
    else return append(value);
  }, old);
}

void Arena::rebuild(std::vector<Handle>& handles, ExprSlot slot,
                    std::uint32_t argument_index, Node replacement,
                    std::uint64_t source_fingerprint) {
  const auto old_size = nodes_.size();
  for (auto it = handles.begin(); it != handles.end(); ++it) {
    if (it->slot != slot || it->argument_index != argument_index) continue;
    handles.erase(it);
    break;
  }
  const auto id = append(std::move(replacement));
  assert(nodes_.size() == old_size + 1 && id == old_size);
  handles.push_back({slot, argument_index, id, source_fingerprint});
}

std::uint64_t fingerprint(const std::string& source) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : source) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

void assert_fresh(const Handle& handle, const std::string& source) {
  assert(handle.source_fingerprint == fingerprint(source));
}

}  // namespace moss::est
