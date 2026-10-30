#include "expression_tree.hpp"
#include "../native_generator_support.hpp"
#include "../parser_support.hpp"
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <type_traits>

namespace moss::est {

std::optional<std::string> ExpressionView::source_text(ExprId expression) const {
  if (!arena || root >= arena->size() || expression >= arena->size()) return std::nullopt;
  const auto r = arena->span(root), c = arena->span(expression);
  if (c.source_begin < r.source_begin || c.source_end < c.source_begin ||
      c.source_end > r.source_end) return std::nullopt;
  const auto begin = c.source_begin - r.source_begin;
  const auto length = c.source_end - c.source_begin;
  if (begin > source.size() || length > source.size() - begin) return std::nullopt;
  const auto text = std::string_view(source).substr(begin, length);
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return std::string{};
  const auto last = text.find_last_not_of(" \t\r\n");
  return std::string(text.substr(first, last - first + 1));
}

ExpressionView view(const Arena& arena, ExprId root, std::string_view source) {
  return ExpressionView{nullptr, &arena, root, std::string(source)};
}

std::optional<ExpressionView> parse_view(std::string_view source) {
  auto arena = std::make_shared<Arena>();
  try {
    const auto root = parse_expression(*arena, source);
    return ExpressionView{arena, arena.get(), root, std::string(source)};
  } catch (const std::invalid_argument&) {
    return std::nullopt;
  }
}

ExprId unparenthesized(const Arena& arena, ExprId expression) {
  while (const auto* grouped = std::get_if<Parenthesized>(&arena.at(expression)))
    expression = grouped->inner;
  return expression;
}

namespace {
std::optional<std::vector<std::string>> child_texts(
    const ExpressionView& expression, const std::vector<ExprId>& children) {
  std::vector<std::string> result;
  result.reserve(children.size());
  for (const auto child : children) {
    auto text = expression.source_text(child);
    if (!text) return std::nullopt;
    result.push_back(std::move(*text));
  }
  return result;
}

bool equal_node(const Arena& left, ExprId lhs, const Arena& right, ExprId rhs) {
  const auto& a = left.at(lhs); const auto& b = right.at(rhs);
  if (a.index() != b.index()) return false;
  return std::visit([&](const auto& node) -> bool {
    using T = std::decay_t<decltype(node)>;
    const auto* other = std::get_if<T>(&b);
    if (!other) return false;
    auto same_children = [&](const auto& xs, const auto& ys) {
      if (xs.size() != ys.size()) return false;
      for (std::size_t i = 0; i < xs.size(); ++i)
        if (!equal_node(left, xs[i], right, ys[i])) return false;
      return true;
    };
    if constexpr (std::is_same_v<T, Literal>) return node.spelling == other->spelling;
    else if constexpr (std::is_same_v<T, Identifier>) return node.name == other->name;
    else if constexpr (std::is_same_v<T, Unary>)
      return node.op == other->op && equal_node(left, node.operand, right, other->operand);
    else if constexpr (std::is_same_v<T, Binary>)
      return node.op == other->op && equal_node(left, node.left, right, other->left) &&
             equal_node(left, node.right, right, other->right);
    else if constexpr (std::is_same_v<T, Call>)
      return equal_node(left, node.callee, right, other->callee) &&
             same_children(node.arguments, other->arguments);
    else if constexpr (std::is_same_v<T, Member>)
      return node.name == other->name && equal_node(left, node.base, right, other->base);
    else if constexpr (std::is_same_v<T, Index>)
      return equal_node(left, node.base, right, other->base) &&
             equal_node(left, node.index, right, other->index);
    else if constexpr (std::is_same_v<T, Parenthesized>)
      return equal_node(left, node.inner, right, other->inner);
    else if constexpr (std::is_same_v<T, Message>)
      return node.handler == other->handler &&
             equal_node(left, node.target, right, other->target) &&
             same_children(node.arguments, other->arguments);
    else if constexpr (std::is_same_v<T, NamedArgument>)
      return node.name == other->name && equal_node(left, node.value, right, other->value);
    else if constexpr (std::is_same_v<T, Placeholder>) return true;
    else if constexpr (std::is_same_v<T, Collection>)
      return node.delimiter == other->delimiter && same_children(node.elements, other->elements);
    else if constexpr (std::is_same_v<T, Tuple>) return same_children(node.elements, other->elements);
    else if constexpr (std::is_same_v<T, Constructor>)
      return equal_node(left, node.type, right, other->type) &&
             same_children(node.type_arguments, other->type_arguments) &&
             same_children(node.arguments, other->arguments);
    else if constexpr (std::is_same_v<T, Pipeline>)
      return equal_node(left, node.input, right, other->input) &&
             same_children(node.stages, other->stages);
    return false;
  }, a);
}

void mentions_node(const Arena& arena, ExprId id, std::string_view name, bool& found) {
  if (found) return;
  std::visit([&](const auto& node) {
    using T = std::decay_t<decltype(node)>;
    auto visit = [&](ExprId child) { mentions_node(arena, child, name, found); };
    auto visit_all = [&](const std::vector<ExprId>& children) {
      for (const auto child : children) visit(child);
    };
    if constexpr (std::is_same_v<T, Identifier>) found = node.name == name;
    else if constexpr (std::is_same_v<T, Unary>) visit(node.operand);
    else if constexpr (std::is_same_v<T, Binary>) { visit(node.left); visit(node.right); }
    else if constexpr (std::is_same_v<T, Call>) { visit(node.callee); visit_all(node.arguments); }
    else if constexpr (std::is_same_v<T, Member>) { found = node.name == name; if (!found) visit(node.base); }
    else if constexpr (std::is_same_v<T, Index>) { visit(node.base); visit(node.index); }
    else if constexpr (std::is_same_v<T, Parenthesized>) visit(node.inner);
    else if constexpr (std::is_same_v<T, Message>) { found = node.handler == name; if (!found) visit(node.target); visit_all(node.arguments); }
    else if constexpr (std::is_same_v<T, NamedArgument>) { found = node.name == name; if (!found) visit(node.value); }
    else if constexpr (std::is_same_v<T, Collection> || std::is_same_v<T, Tuple>) visit_all(node.elements);
    else if constexpr (std::is_same_v<T, Constructor>) { visit(node.type); visit_all(node.type_arguments); visit_all(node.arguments); }
    else if constexpr (std::is_same_v<T, Pipeline>) { visit(node.input); visit_all(node.stages); }
  }, arena.at(id));
}
}

bool project_simple_call(const ExpressionView& e, std::string& callee,
                         std::vector<std::string>& arguments) {
  bool result = false;
  if (e.arena && e.root < e.arena->size())
    if (const auto* call = std::get_if<Call>(&e.arena->at(e.root)))
      if (auto name = e.source_text(call->callee))
        if (auto args = child_texts(e, call->arguments)) {
          callee = std::move(*name); arguments = std::move(*args); result = true;
        }
  if (mode() == Mode::Shadow) {
    std::string old_name; std::vector<std::string> old_args;
    const bool legacy = moss::parse_simple_call(e.source, old_name, old_args);
    assert(legacy == result);
    if (result) assert(old_name == callee && old_args == arguments);
  }
  return result;
}

bool project_member_call(const ExpressionView& e, std::string& receiver,
                         std::string& member, std::vector<std::string>& arguments,
                         bool* outputs_written) {
  if (outputs_written) *outputs_written = false;
  bool result = false;
  if (e.arena && e.root < e.arena->size())
    if (const auto* call = std::get_if<Call>(&e.arena->at(e.root)))
      if (const auto* method = std::get_if<Member>(&e.arena->at(call->callee))) {
        if (auto base = e.source_text(method->base)) {
          receiver = std::move(*base); member = method->name;
          if (outputs_written) *outputs_written = true;
          if (!receiver.empty())
            if (auto args = child_texts(e, call->arguments)) {
              arguments = std::move(*args); result = true;
            }
        }
      }
  if (mode() == Mode::Shadow) {
    std::string old_receiver, old_member; std::vector<std::string> old_args;
    bool old_outputs = false;
    const bool legacy = moss::parse_member_call(e.source, old_receiver, old_member,
                                                old_args, &old_outputs);
    assert(legacy == result);
    if (old_outputs) assert(old_receiver == receiver && old_member == member);
    if (outputs_written) assert(old_outputs == *outputs_written);
    if (result) assert(old_args == arguments);
  }
  return result;
}

bool project_named_argument(const ExpressionView& e, std::string& name,
                            std::string& value) {
  bool result = false;
  if (e.arena && e.root < e.arena->size())
    if (const auto* arg = std::get_if<NamedArgument>(&e.arena->at(e.root)))
      if (auto text = e.source_text(arg->value)) {
        name = arg->name; value = std::move(*text); result = true;
      }
  if (mode() == Mode::Shadow) {
    std::string old_name, old_value;
    const bool legacy = moss::parse_named_argument(e.source, old_name, old_value);
    assert(legacy == result);
    if (result) assert(old_name == name && old_value == value);
  }
  return result;
}

bool project_index(const ExpressionView& e, std::string& base, std::string& index) {
  bool result = false;
  if (e.arena && e.root < e.arena->size())
    if (const auto* access = std::get_if<Index>(&e.arena->at(e.root)))
      if (auto receiver = e.source_text(access->base))
        if (auto key = e.source_text(access->index)) {
          base = std::move(*receiver); index = std::move(*key); result = true;
        }
  if (mode() == Mode::Shadow) {
    std::string old_base, old_index;
    const bool legacy = moss::parse_index(e.source, old_base, old_index);
    assert(legacy == result);
    if (result) assert(old_base == base && old_index == index);
  }
  return result;
}

std::optional<std::vector<std::string>> project_vector_literal(const ExpressionView& e) {
  std::optional<std::vector<std::string>> result;
  if (e.arena && e.root < e.arena->size())
    if (const auto* vector = std::get_if<Collection>(&e.arena->at(e.root));
        vector && vector->delimiter == '[')
      result = child_texts(e, vector->elements);
  if (mode() == Mode::Shadow)
    assert(moss::parse_vector_literal(e.source) == result);
  return result;
}

bool project_enum_case(const ExpressionView& e, std::string& enum_name,
                       std::string& case_name, std::vector<std::string>& fields) {
  bool result = false;
  if (e.arena && e.root < e.arena->size())
    if (const auto* ctor = std::get_if<Constructor>(&e.arena->at(e.root)))
      if (const auto* member = std::get_if<Member>(&e.arena->at(ctor->type)))
        if (auto enum_text = e.source_text(member->base))
          if (auto args = child_texts(e, ctor->arguments)) {
            enum_name = std::move(*enum_text); case_name = member->name;
            fields = std::move(*args); result = true;
          }
  if (mode() == Mode::Shadow) {
    std::string old_enum, old_case; std::vector<std::string> old_fields;
    bool called = false;
    const bool legacy = moss::parse_enum_case_expression(e.source, old_enum,
        old_case, old_fields, called);
    assert(legacy == result);
    if (result) assert(called && old_enum == enum_name && old_case == case_name &&
                       old_fields == fields);
  }
  return result;
}

bool project_binary(const ExpressionView& e, const std::vector<std::string>& operators,
                    std::pair<std::string, std::string>& operands,
                    std::string& operation) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  const auto* binary = std::get_if<Binary>(&e.arena->at(e.root));
  if (!binary || std::find(operators.begin(), operators.end(), binary->op) == operators.end())
    return false;
  auto left = e.source_text(binary->left); auto right = e.source_text(binary->right);
  if (!left || !right) return false;
  operands = {std::move(*left), std::move(*right)}; operation = binary->op;
  return true;
}

std::optional<std::vector<std::string>> project_delimited_children(
    const ExpressionView& e, char delimiter) {
  if (delimiter != ',' || !e.arena || e.root >= e.arena->size()) return std::nullopt;
  const auto& node = e.arena->at(e.root);
  if (const auto* call = std::get_if<Call>(&node)) return child_texts(e, call->arguments);
  if (const auto* message = std::get_if<Message>(&node)) return child_texts(e, message->arguments);
  if (const auto* ctor = std::get_if<Constructor>(&node)) return child_texts(e, ctor->arguments);
  if (const auto* collection = std::get_if<Collection>(&node)) return child_texts(e, collection->elements);
  if (const auto* tuple = std::get_if<Tuple>(&node)) return child_texts(e, tuple->elements);
  auto root = e.source_text(e.root);
  if (root && root->empty()) return std::vector<std::string>{};
  if (root) return std::vector<std::string>{std::move(*root)};
  return std::nullopt;
}

bool project_identifier(const ExpressionView& e, std::string& name) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  const auto* id = std::get_if<Identifier>(&e.arena->at(e.root));
  if (!id) return false;
  name = id->name; return true;
}

bool expression_mentions(const ExpressionView& e, std::string_view name) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  bool found = false; mentions_node(*e.arena, e.root, name, found); return found;
}

bool structurally_equal(const ExpressionView& left, const ExpressionView& right) {
  return left.arena && right.arena && left.root < left.arena->size() &&
      right.root < right.arena->size() &&
      equal_node(*left.arena, left.root, *right.arena, right.root);
}

}  // namespace moss::est
