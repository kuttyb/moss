#include "expression_tree.hpp"
#include "../native_generator_support.hpp"
#include "../numeric_literal.hpp"
#include "../parser_support.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace moss::est {

std::optional<std::string> ExpressionView::source_text(ExprId expression) const {
  if (!arena || root >= arena->size() || expression >= arena->size()) return std::nullopt;
  if (expression == root) {
    const auto first = source.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string{};
    const auto last = source.find_last_not_of(" \t\r\n");
    return source.substr(first, last - first + 1);
  }
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

std::optional<ExpressionView> parse_named_argument_view(std::string_view source) {
  auto wrapped = parse_view("__est_named__(" + std::string(source) + ")");
  if (!wrapped || !wrapped->arena || wrapped->root >= wrapped->arena->size())
    return std::nullopt;
  const auto* call = std::get_if<Call>(&wrapped->arena->at(wrapped->root));
  if (!call || call->arguments.size() != 1 ||
      !std::holds_alternative<NamedArgument>(
          wrapped->arena->at(call->arguments.front()))) return std::nullopt;
  return ExpressionView{wrapped->owned_arena, wrapped->arena,
                        call->arguments.front(), std::string(source)};
}

PatternNode match_pattern(std::string_view tag,
                          const std::vector<std::string>& bindings) {
  PatternNode node;
  node.kind = bindings.empty() ? PatternNode::Kind::EnumTag
                               : PatternNode::Kind::EnumCase;
  node.case_name = std::string(tag);
  node.payload_bindings = bindings;
  node.payload_pattern = !bindings.empty();
  return node;
}

PatternNode qualified_recovery_pattern(std::string_view source) {
  PatternNode node;
  node.kind = PatternNode::Kind::QualifiedRecoveryCase;
  auto syntax = parse_view(source);
  if (!syntax) return node;
  const Node* shape = &syntax->arena->at(
      unparenthesized(*syntax->arena, syntax->root));
  const std::vector<ExprId>* arguments = nullptr;
  if (const auto* constructor = std::get_if<Constructor>(shape)) {
    shape = &syntax->arena->at(constructor->type);
    arguments = &constructor->arguments;
  } else if (const auto* call = std::get_if<Call>(shape)) {
    shape = &syntax->arena->at(call->callee);
    arguments = &call->arguments;
  }
  node.payload_pattern = arguments != nullptr;
  const auto* member = std::get_if<Member>(shape);
  if (!member) return node;
  auto type_text = syntax->source_text(member->base);
  if (!type_text) return node;
  node.type_name = std::move(*type_text);
  node.case_name = member->name;
  if (arguments) for (const auto argument : *arguments) {
    const auto& payload = syntax->arena->at(argument);
    if (const auto* identifier = std::get_if<Identifier>(&payload))
      node.payload_bindings.push_back(identifier->name);
    else if (const auto* named = std::get_if<NamedArgument>(&payload))
      node.payload_bindings.push_back(named->name);
    else if (auto text = syntax->source_text(argument))
      node.payload_bindings.push_back(std::move(*text));
  }
  return node;
}

PatternNode recovery_pattern(std::string_view selector) {
  PatternNode result;
  std::string source(selector);
  const auto first = source.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    result.kind = PatternNode::Kind::RecoveryCatchall;
    return result;
  }
  const auto last = source.find_last_not_of(" \t\r\n");
  source = source.substr(first, last - first + 1);
  const auto alternatives = moss::split_top_level(source, ',');
  const auto qualified = std::all_of(alternatives.begin(), alternatives.end(),
      [](const std::string& alternative) {
        return alternative.find('.') != std::string::npos;
      });
  if (qualified) {
    if (alternatives.size() == 1)
      return qualified_recovery_pattern(alternatives.front());
    result.kind = PatternNode::Kind::GroupedAlternatives;
    for (const auto& alternative : alternatives)
      result.alternatives.push_back(qualified_recovery_pattern(alternative));
    return result;
  }
  result.kind = PatternNode::Kind::RecoveryBinding;
  result.binding = source;
  return result;
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

bool callee_contains_parenthesis(const Arena& arena, ExprId id) {
  const auto& node = arena.at(id);
  return std::visit([&](const auto& value) -> bool {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, Parenthesized> || std::is_same_v<T, Call>)
      return true;
    else if constexpr (std::is_same_v<T, Member>)
      return callee_contains_parenthesis(arena, value.base);
    else if constexpr (std::is_same_v<T, Index>)
      return callee_contains_parenthesis(arena, value.base) ||
             callee_contains_parenthesis(arena, value.index);
    else if constexpr (std::is_same_v<T, Unary>)
      return callee_contains_parenthesis(arena, value.operand);
    else if constexpr (std::is_same_v<T, Binary>)
      return callee_contains_parenthesis(arena, value.left) ||
             callee_contains_parenthesis(arena, value.right);
    else if constexpr (std::is_same_v<T, NamedArgument>)
      return callee_contains_parenthesis(arena, value.value);
    else if constexpr (std::is_same_v<T, Collection> || std::is_same_v<T, Tuple>) {
      for (const auto child : value.elements)
        if (callee_contains_parenthesis(arena, child)) return true;
      return false;
    } else if constexpr (std::is_same_v<T, Constructor>) {
      // Constructors retain explicit call syntax when they occur inside a
      // larger callee chain, matching the legacy reader's parenthesis stop.
      return true;
    } else if constexpr (std::is_same_v<T, Pipeline>) {
      if (callee_contains_parenthesis(arena, value.input)) return true;
      for (const auto child : value.stages)
        if (callee_contains_parenthesis(arena, child)) return true;
      return false;
    } else if constexpr (std::is_same_v<T, Message>) {
      if (callee_contains_parenthesis(arena, value.target)) return true;
      for (const auto child : value.arguments)
        if (callee_contains_parenthesis(arena, child)) return true;
      return false;
    }
    return false;
  }, node);
}

std::optional<ExprId> trailing_call_id(const Arena& arena, ExprId id) {
  const auto& node = arena.at(id);
  if (std::holds_alternative<Call>(node)) return id;
  if (const auto* binary = std::get_if<Binary>(&node))
    return trailing_call_id(arena, binary->right);
  if (const auto* unary = std::get_if<Unary>(&node))
    return trailing_call_id(arena, unary->operand);
  if (const auto* member = std::get_if<Member>(&node))
    return trailing_call_id(arena, member->base);
  return std::nullopt;
}

const Call* trailing_call(const Arena& arena, ExprId id) {
  auto call = trailing_call_id(arena, id);
  return call ? std::get_if<Call>(&arena.at(*call)) : nullptr;
}

bool contains_message_node(const Arena& arena, ExprId id) {
  return std::visit([&](const auto& value) -> bool {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, Message>) return true;
    else if constexpr (std::is_same_v<T, Call>) {
      if (contains_message_node(arena, value.callee)) return true;
      for (const auto child : value.arguments)
        if (contains_message_node(arena, child)) return true;
      return false;
    } else if constexpr (std::is_same_v<T, Binary>)
      return contains_message_node(arena, value.left) ||
             contains_message_node(arena, value.right);
    else if constexpr (std::is_same_v<T, Unary>)
      return contains_message_node(arena, value.operand);
    else if constexpr (std::is_same_v<T, Member>)
      return contains_message_node(arena, value.base);
    else if constexpr (std::is_same_v<T, Index>)
      return contains_message_node(arena, value.base) ||
             contains_message_node(arena, value.index);
    else if constexpr (std::is_same_v<T, Parenthesized>)
      return contains_message_node(arena, value.inner);
    else if constexpr (std::is_same_v<T, NamedArgument>)
      return contains_message_node(arena, value.value);
    else if constexpr (std::is_same_v<T, Collection> ||
                       std::is_same_v<T, Tuple>) {
      for (const auto child : value.elements)
        if (contains_message_node(arena, child)) return true;
      return false;
    } else if constexpr (std::is_same_v<T, Constructor>) {
      if (contains_message_node(arena, value.type)) return true;
      for (const auto child : value.type_arguments)
        if (contains_message_node(arena, child)) return true;
      for (const auto child : value.arguments)
        if (contains_message_node(arena, child)) return true;
      return false;
    } else if constexpr (std::is_same_v<T, Pipeline>) {
      if (contains_message_node(arena, value.input)) return true;
      for (const auto child : value.stages)
        if (contains_message_node(arena, child)) return true;
      return false;
    }
    return false;
  }, arena.at(id));
}

bool has_earlier_call_or_group(const Arena& arena, ExprId id,
                               std::uint32_t before) {
  if (arena.span(id).source_begin >= before) return false;
  const auto& node = arena.at(id);
  return std::visit([&](const auto& value) -> bool {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, Call> || std::is_same_v<T, Parenthesized> ||
                  std::is_same_v<T, Constructor>)
      return arena.span(id).source_end <= before;
    else if constexpr (std::is_same_v<T, Binary>)
      return has_earlier_call_or_group(arena, value.left, before) ||
             has_earlier_call_or_group(arena, value.right, before);
    else if constexpr (std::is_same_v<T, Unary>)
      return has_earlier_call_or_group(arena, value.operand, before);
    else if constexpr (std::is_same_v<T, Member>)
      return has_earlier_call_or_group(arena, value.base, before);
    else if constexpr (std::is_same_v<T, Index>)
      return has_earlier_call_or_group(arena, value.base, before) ||
             has_earlier_call_or_group(arena, value.index, before);
    else if constexpr (std::is_same_v<T, Collection> || std::is_same_v<T, Tuple>) {
      for (const auto child : value.elements)
        if (has_earlier_call_or_group(arena, child, before)) return true;
      return false;
    } else if constexpr (std::is_same_v<T, Pipeline>) {
      if (has_earlier_call_or_group(arena, value.input, before)) return true;
      for (const auto child : value.stages)
        if (has_earlier_call_or_group(arena, child, before)) return true;
      return false;
    }
    return false;
  }, node);
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
      return node.name == other->name && node.separator == other->separator &&
             equal_node(left, node.value, right, other->value);
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

void identifier_occurs_node(const Arena& arena, ExprId id,
                            std::string_view name, bool& found) {
  if (found) return;
  std::visit([&](const auto& node) {
    using T = std::decay_t<decltype(node)>;
    auto visit = [&](ExprId child) {
      identifier_occurs_node(arena, child, name, found);
    };
    auto visit_all = [&](const std::vector<ExprId>& children) {
      for (const auto child : children) visit(child);
    };
    if constexpr (std::is_same_v<T, Identifier>) found = node.name == name;
    else if constexpr (std::is_same_v<T, Placeholder>) found = name == "_";
    else if constexpr (std::is_same_v<T, Unary>) visit(node.operand);
    else if constexpr (std::is_same_v<T, Binary>) {
      visit(node.left); visit(node.right);
    } else if constexpr (std::is_same_v<T, Call>) {
      visit(node.callee); visit_all(node.arguments);
    } else if constexpr (std::is_same_v<T, Member>) visit(node.base);
    else if constexpr (std::is_same_v<T, Index>) {
      visit(node.base); visit(node.index);
    } else if constexpr (std::is_same_v<T, Parenthesized>) visit(node.inner);
    else if constexpr (std::is_same_v<T, Message>) {
      visit(node.target); visit_all(node.arguments);
    } else if constexpr (std::is_same_v<T, NamedArgument>) visit(node.value);
    else if constexpr (std::is_same_v<T, Collection> ||
                       std::is_same_v<T, Tuple>) visit_all(node.elements);
    else if constexpr (std::is_same_v<T, Constructor>) {
      visit(node.type); visit_all(node.type_arguments); visit_all(node.arguments);
    } else if constexpr (std::is_same_v<T, Pipeline>) {
      visit(node.input); visit_all(node.stages);
    }
  }, arena.at(id));
}
}

bool project_simple_call(const ExpressionView& e, std::string& callee,
                         std::vector<std::string>& arguments) {
  bool result = false;
  if (e.arena && e.root < e.arena->size()) {
    if (const auto* call = std::get_if<Call>(&e.arena->at(e.root))) {
      if (callee_contains_parenthesis(*e.arena, call->callee)) {
        // The legacy simple-call reader rejects a callee containing an earlier
        // parenthesis pair (for example `(number).to_string()`).
      } else if (auto name = e.source_text(call->callee))
        if (auto args = child_texts(e, call->arguments)) {
          callee = std::move(*name); arguments = std::move(*args); result = true;
        }
    } else if (const auto* constructor =
                   std::get_if<Constructor>(&e.arena->at(e.root))) {
      std::optional<std::string> name;
      const auto root_span = e.arena->span(e.root);
      if (constructor->callee_source_end >= constructor->callee_source_begin &&
          constructor->callee_source_begin >= root_span.source_begin &&
          constructor->callee_source_end <= root_span.source_end) {
        const auto begin = constructor->callee_source_begin - root_span.source_begin;
        const auto length = constructor->callee_source_end - constructor->callee_source_begin;
        if (begin <= e.source.size() && length <= e.source.size() - begin) {
          auto spelling = std::string_view(e.source).substr(begin, length);
          const auto first = spelling.find_first_not_of(" \t\r\n");
          if (first == std::string_view::npos) name = std::string{};
          else {
            const auto last = spelling.find_last_not_of(" \t\r\n");
            name = std::string(spelling.substr(first, last - first + 1));
          }
        }
      }
      if (!name) name = e.source_text(constructor->type);
      if (name)
        if (auto args = child_texts(e, constructor->arguments)) {
          callee = std::move(*name); arguments = std::move(*args); result = true;
        }
    } else if (const auto* message = std::get_if<Message>(&e.arena->at(e.root))) {
      if (auto target = e.source_text(message->target))
        if (auto args = child_texts(e, message->arguments)) {
          callee = "message " + *target + "." + message->handler;
          arguments = std::move(*args); result = true;
        }
    } else if (const auto* pipeline = std::get_if<Pipeline>(&e.arena->at(e.root))) {
      if (pipeline->stages.size() == 1)
        if (const auto* stage = std::get_if<Call>(&e.arena->at(pipeline->stages.front()))) {
          const auto callee_span = e.arena->span(stage->callee);
          if (!callee_contains_parenthesis(*e.arena, stage->callee) &&
              !has_earlier_call_or_group(*e.arena, e.root,
                                         callee_span.source_begin))
            if (auto input = e.source_text(pipeline->input))
              if (auto name = e.source_text(stage->callee))
                if (auto args = child_texts(e, stage->arguments)) {
                  callee = *input + " |> " + *name;
                  arguments = std::move(*args); result = true;
                }
        }
    }
    if (!result && !std::holds_alternative<Member>(e.arena->at(e.root))) {
      // The legacy splitter accepts a trailing call on a larger expression
      // (for example `total + item.length()`). Preserve that projection using
      // the terminal Call node, provided no earlier call/group would have
      // made the legacy scan stop before this call.
      if (const auto call_id = trailing_call_id(*e.arena, e.root)) {
        const auto* call = std::get_if<Call>(&e.arena->at(*call_id));
        const auto root_span = e.arena->span(e.root);
        const auto callee_span = e.arena->span(call->callee);
        if (e.arena->span(*call_id).source_end == root_span.source_end &&
            callee_span.source_begin >= root_span.source_begin &&
            !callee_contains_parenthesis(*e.arena, call->callee) &&
            !has_earlier_call_or_group(*e.arena, e.root,
                                       callee_span.source_begin) &&
            callee_span.source_end <= root_span.source_end) {
          if (auto args = child_texts(e, call->arguments)) {
              callee = e.source.substr(0, callee_span.source_end - root_span.source_begin);
              const auto first = callee.find_first_not_of(" \t\r\n");
              const auto last = callee.find_last_not_of(" \t\r\n");
              callee = first == std::string::npos ? std::string{} : callee.substr(first, last - first + 1);
              arguments = std::move(*args); result = !callee.empty();
          }
        }
      }
    }
  }
  if (mode() == Mode::Shadow) {
    std::string old_name; std::vector<std::string> old_args;
    const bool legacy = moss::parse_simple_call(e.source, old_name, old_args);
    const auto root = unparenthesized(*e.arena, e.root);
    const auto& root_node = e.arena->at(root);
    const bool tree_call_shape = std::holds_alternative<Call>(root_node) ||
        std::holds_alternative<Constructor>(root_node) ||
        std::holds_alternative<Message>(root_node);
    // A legacy trailing-call scan can mistake a call nested inside a larger
    // comparison, arithmetic expression, or multi-stage pipeline for the
    // expression's simple-call shape.
    const bool legacy_noncall_false_positive =
        legacy && !result && !tree_call_shape;
    if (legacy != result && !legacy_noncall_false_positive)
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
  if (e.arena && e.root < e.arena->size()) {
    const auto assign_member = [&](std::string base, const std::string& name,
                                   const std::vector<ExprId>& children,
                                   bool message_form) {
      receiver = message_form ? "message " + base : std::move(base);
      member = name;
      if (outputs_written) *outputs_written = true;
      if (!receiver.empty())
        if (auto args = child_texts(e, children)) {
          arguments = std::move(*args); result = true;
        }
    };
    if (const auto* call = std::get_if<Call>(&e.arena->at(e.root)))
      if (const auto* method = std::get_if<Member>(&e.arena->at(call->callee)))
        if (auto base = e.source_text(method->base))
          assign_member(std::move(*base), method->name, call->arguments, false);
    if (const auto* constructor = std::get_if<Constructor>(&e.arena->at(e.root)))
      if (const auto* method = std::get_if<Member>(&e.arena->at(constructor->type)))
        if (auto base = e.source_text(method->base))
          assign_member(std::move(*base), method->name, constructor->arguments, false);
    if (const auto* message = std::get_if<Message>(&e.arena->at(e.root)))
      if (auto base = e.source_text(message->target))
        assign_member(std::move(*base), message->handler,
                      message->arguments, true);
    if (!result) {
      if (const auto call_id = trailing_call_id(*e.arena, e.root)) {
        const auto* call = std::get_if<Call>(&e.arena->at(*call_id));
        if (const auto* method = std::get_if<Member>(&e.arena->at(call->callee))) {
          const auto root_span = e.arena->span(e.root);
          const auto call_span = e.arena->span(*call_id);
          const auto base_span = e.arena->span(method->base);
          if (call_span.source_end == root_span.source_end &&
              base_span.source_end >= root_span.source_begin &&
              base_span.source_end <= root_span.source_end) {
            const auto length = base_span.source_end - root_span.source_begin;
            std::string base = e.source.substr(0, length);
            const auto first = base.find_first_not_of(" \t\r\n");
            const auto last = base.find_last_not_of(" \t\r\n");
            base = first == std::string::npos ? std::string{} : base.substr(first, last - first + 1);
            assign_member(std::move(base), method->name, call->arguments, false);
          }
        }
      }
    }
  }
  if (mode() == Mode::Shadow) {
    std::string old_receiver, old_member; std::vector<std::string> old_args;
    bool old_outputs = false;
    const bool legacy = moss::parse_member_call(e.source, old_receiver, old_member,
                                                old_args, &old_outputs);
    const auto root = unparenthesized(*e.arena, e.root);
    const bool tree_pipeline = std::holds_alternative<Pipeline>(
        e.arena->at(root));
    const bool legacy_nested_message_false_positive = legacy && !result &&
        !std::holds_alternative<Message>(e.arena->at(root)) &&
        contains_message_node(*e.arena, root);
    const bool legacy_binary_member_false_positive = legacy && !result &&
        std::holds_alternative<Binary>(e.arena->at(root));
    // The legacy text reader may select a dotted call nested in a complete
    // pipeline. The EST root is the independent oracle for this false positive.
    const bool legacy_pipeline_false_positive = legacy && !result && tree_pipeline;
    if (legacy != result && !legacy_pipeline_false_positive &&
        !legacy_nested_message_false_positive &&
        !legacy_binary_member_false_positive) {
      assert(legacy == result);
    }
    if (!legacy_pipeline_false_positive &&
        !legacy_nested_message_false_positive &&
        !legacy_binary_member_false_positive) {
      if (old_outputs) assert(old_receiver == receiver && old_member == member);
      if (outputs_written) assert(old_outputs == *outputs_written);
      if (result) assert(old_args == arguments);
    }
  }
  return result;
}

bool project_bound_method(const ExpressionView& e, std::string& receiver,
                          std::string& method) {
  bool result = false;
  if (e.arena && e.root < e.arena->size())
    if (const auto* member = std::get_if<Member>(&e.arena->at(e.root)))
      if (auto base = e.source_text(member->base)) {
        std::string ignored;
        const bool simple = project_identifier(view(*e.arena, member->base, *base), ignored);
        if (simple && !ignored.empty() && ignored != "_") {
          receiver = std::move(*base); method = member->name; result = true;
        }
      }
  if (mode() == Mode::Shadow) {
    std::string old_receiver, old_method;
    const bool legacy = moss::parse_bound_method_callable(e.source, old_receiver, old_method);
    assert(legacy == result);
    if (result) assert(old_receiver == receiver && old_method == method);
  }
  return result;
}

bool project_member_access(const ExpressionView& e, std::string& base,
                           std::string& member_name) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  const auto root = unparenthesized(*e.arena, e.root);
  const auto* member = std::get_if<Member>(&e.arena->at(root));
  if (!member) return false;
  auto source = e.source_text(member->base);
  if (!source) return false;
  base = std::move(*source);
  member_name = member->name;
  return true;
}

bool project_unary(const ExpressionView& e, std::string& operation,
                   std::string& operand) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  const auto root = unparenthesized(*e.arena, e.root);
  const auto* unary = std::get_if<Unary>(&e.arena->at(root));
  if (!unary) return false;
  auto source = e.source_text(unary->operand);
  if (!source) return false;
  operation = unary->op;
  operand = std::move(*source);
  return true;
}

bool project_message(const ExpressionView& e, std::string& target,
                     std::string& handler,
                     std::vector<std::string>& arguments) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  const auto root = unparenthesized(*e.arena, e.root);
  const auto* message = std::get_if<Message>(&e.arena->at(root));
  if (!message) return false;
  auto target_source = e.source_text(message->target);
  auto argument_sources = child_texts(e, message->arguments);
  if (!target_source || !argument_sources) return false;
  target = std::move(*target_source);
  handler = message->handler;
  arguments = std::move(*argument_sources);
  return true;
}

std::vector<MessageCall> message_calls(const ExpressionView& e) {
  std::vector<MessageCall> result;
  if (!e.arena || e.root >= e.arena->size()) return result;
  std::function<void(ExprId)> visit = [&](ExprId id) {
    if (id >= e.arena->size()) return;
    const auto& node = e.arena->at(id);
    if (const auto* message = std::get_if<Message>(&node)) {
      auto target = e.source_text(message->target);
      auto arguments = child_texts(e, message->arguments);
      if (target && arguments)
        result.push_back({std::move(*target), message->handler,
                          std::move(*arguments)});
      visit(message->target);
      for (const auto argument : message->arguments) visit(argument);
    } else if (const auto* call = std::get_if<Call>(&node)) {
      visit(call->callee);
      for (const auto argument : call->arguments) visit(argument);
    } else if (const auto* unary = std::get_if<Unary>(&node)) {
      visit(unary->operand);
    } else if (const auto* binary = std::get_if<Binary>(&node)) {
      visit(binary->left);
      visit(binary->right);
    } else if (const auto* member = std::get_if<Member>(&node)) {
      visit(member->base);
    } else if (const auto* index = std::get_if<Index>(&node)) {
      visit(index->base);
      visit(index->index);
    } else if (const auto* grouped = std::get_if<Parenthesized>(&node)) {
      visit(grouped->inner);
    } else if (const auto* named = std::get_if<NamedArgument>(&node)) {
      visit(named->value);
    } else if (const auto* collection = std::get_if<Collection>(&node)) {
      for (const auto child : collection->elements) visit(child);
    } else if (const auto* tuple = std::get_if<Tuple>(&node)) {
      for (const auto child : tuple->elements) visit(child);
    } else if (const auto* constructor = std::get_if<Constructor>(&node)) {
      visit(constructor->type);
      for (const auto child : constructor->type_arguments) visit(child);
      for (const auto child : constructor->arguments) visit(child);
    } else if (const auto* pipeline = std::get_if<Pipeline>(&node)) {
      visit(pipeline->input);
      for (const auto child : pipeline->stages) visit(child);
    }
  };
  visit(e.root);
  return result;
}

std::optional<std::uint64_t> string_literal_source_payload_bytes(
    const ExpressionView& e) {
  if (!e.arena || e.root >= e.arena->size()) return std::nullopt;
  const auto root = unparenthesized(*e.arena, e.root);
  const auto* literal = std::get_if<Literal>(&e.arena->at(root));
  if (!literal || literal->spelling.size() < 2 ||
      literal->spelling.front() != '"' || literal->spelling.back() != '"')
    return std::nullopt;
  return static_cast<std::uint64_t>(literal->spelling.size() - 2);
}

bool contains_message(const ExpressionView& e) {
  return e.arena && e.root < e.arena->size() &&
      contains_message_node(*e.arena, e.root);
}

std::string strip_grouping(const ExpressionView& e) {
  std::string result;
  if (e.arena && e.root < e.arena->size() &&
      std::holds_alternative<Tuple>(e.arena->at(e.root)) &&
      e.source.size() >= 2) {
    result = e.source.substr(1, e.source.size() - 2);
  } else {
    result = e.source_text(unparenthesized(*e.arena, e.root)).value_or(e.source);
  }
  if (mode() == Mode::Shadow) {
    const auto legacy = moss::strip_redundant_outer_parentheses(e.source);
    if (result != legacy) std::fprintf(stderr, "EST strip mismatch <%s> tree=<%s> legacy=<%s>\n", e.source.c_str(), result.c_str(), legacy.c_str());
    assert(result == legacy);
  }
  return result;
}

std::string normalize_pipeline_tree(const ExpressionView& e) {
  auto trim_source = [](std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string{};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
  };
  std::string result = trim_source(e.source);
  if (e.arena && e.root < e.arena->size()) {
    const auto root = e.root;
    if (const auto* pipeline = std::get_if<Pipeline>(&e.arena->at(root))) {
      auto input = e.source_text(pipeline->input);
      if (input) {
        result = std::move(*input);
        for (const auto stage_id : pipeline->stages) {
          std::string callee;
          std::vector<std::string> args;
          if (const auto* call = std::get_if<Call>(&e.arena->at(stage_id))) {
            auto name = e.source_text(call->callee);
            auto children = child_texts(e, call->arguments);
            if (!name || !children) { result = trim_source(e.source); break; }
            callee = std::move(*name); args = std::move(*children);
          } else {
            auto name = e.source_text(stage_id);
            if (!name) { result = trim_source(e.source); break; }
            callee = std::move(*name);
          }
          std::string rewritten = callee + "(" + result;
          for (const auto& arg : args) rewritten += ", " + arg;
          rewritten += ")";
          result = std::move(rewritten);
        }
      }
    }
  }
  if (mode() == Mode::Shadow) assert(result == moss::normalize_pipeline(e.source));
  return result;
}

bool project_fileio_open(const ExpressionView& e,
                         std::vector<std::string>& arguments) {
  std::string receiver, method;
  const auto old_arguments = arguments;
  const bool member_call = project_member_call(e, receiver, method, arguments);
  const bool result = member_call && receiver == "FileIO" && method == "open" &&
                      arguments.size() == 2;
  if (mode() == Mode::Shadow) {
    auto old_arguments_copy = old_arguments;
    const bool legacy = moss::parse_fileio_open(e.source, old_arguments_copy);
    // Preserve the legacy helper's partial output on a failed probe; callers
    // ignore the vector when the success flag is false.
    if (!result && !legacy && arguments.empty() && !old_arguments_copy.empty())
      arguments = old_arguments_copy;
    assert(legacy == result && old_arguments_copy == arguments);
  }
  return result;
}

bool project_fileio_chunks_source(const ExpressionView& e, std::string& receiver,
                                  std::string& size_expression) {
  std::string method;
  std::vector<std::string> arguments;
  const bool result = project_member_call(e, receiver, method, arguments) &&
                      method == "chunks" && arguments.size() == 1;
  if (result) size_expression = arguments.front();
  if (mode() == Mode::Shadow) {
    std::string old_receiver, old_size;
    const bool legacy = moss::parse_fileio_chunks_source(e.source, old_receiver, old_size);
    assert(legacy == result);
    if (legacy) assert(old_receiver == receiver && old_size == size_expression);
  }
  return result;
}

bool project_executor_chain(
    const ExpressionView& e,
    std::vector<std::pair<std::string, std::vector<std::string>>>& calls) {
  calls.clear();
  bool result = false;
  if (e.arena && e.root < e.arena->size()) {
    ExprId current = e.root;
    std::vector<std::pair<std::string, std::vector<std::string>>> reverse;
    while (current < e.arena->size()) {
      const auto& node = e.arena->at(current);
      if (const auto* base = std::get_if<Constructor>(&node)) {
        std::string name;
        const bool executor = project_identifier(view(*e.arena, base->type,
            e.source_text(base->type).value_or(std::string{})), name) && name == "Executor";
        if (executor && base->arguments.empty() && base->type_arguments.empty()) {
          std::reverse(reverse.begin(), reverse.end()); calls = std::move(reverse);
          result = true;
          break;
        }
        if (!std::holds_alternative<Member>(e.arena->at(base->type))) break;
      }
      if (const auto* call = std::get_if<Call>(&node)) {
        if (const auto* member = std::get_if<Member>(&e.arena->at(call->callee))) {
          auto arguments = child_texts(e, call->arguments);
          if (!arguments) break;
          reverse.emplace_back(member->name, std::move(*arguments));
          current = member->base;
          continue;
        }
      }
      if (const auto* constructor = std::get_if<Constructor>(&node)) {
        if (const auto* member = std::get_if<Member>(&e.arena->at(constructor->type))) {
          auto arguments = child_texts(e, constructor->arguments);
          if (!arguments) break;
          reverse.emplace_back(member->name, std::move(*arguments));
          current = member->base;
          continue;
        }
      }
      if (const auto* message = std::get_if<Message>(&node)) {
        auto arguments = child_texts(e, message->arguments);
        if (!arguments) break;
        reverse.emplace_back(message->handler, std::move(*arguments));
        current = message->target;
        continue;
      }
      break;
    }
    if (!result) {
      calls = std::move(reverse);
      if (calls.empty()) {
        if (const auto* terminal = trailing_call(*e.arena, e.root)) {
          const Call* current_call = terminal;
          ExprId current_base = current_call->callee;
          while (const auto* method = std::get_if<Member>(&e.arena->at(current_base))) {
            auto arguments = child_texts(e, current_call->arguments);
            if (!arguments) break;
            calls.emplace_back(method->name, std::move(*arguments));
            current_base = method->base;
            current_call = nullptr;
            // Inner calls are visited from the receiver expression.
            while (current_base < e.arena->size()) {
              if (const auto* inner = std::get_if<Call>(&e.arena->at(current_base))) {
                current_call = inner; current_base = inner->callee; break;
              }
              break;
            }
            if (!current_call) break;
          }
        }
      }
    }
  }
  if (mode() == Mode::Shadow) {
    std::vector<std::pair<std::string, std::vector<std::string>>> legacy;
    const bool old = moss::parse_executor_chain(e.source, legacy);
    // Failed legacy probes can leave a partial vector. Consumers ignore it
    // when the boolean result is false, but shadow parity compares outputs.
    if (!result && !old && calls.empty() && !legacy.empty()) calls = legacy;
    assert(old == result && legacy == calls);
  }
  return result;
}

bool project_named_argument(const ExpressionView& e, std::string& name,
                            std::string& value) {
  bool result = false;
  const ExpressionView* view = &e;
  std::optional<ExpressionView> wrapped;
  ExprId argument_id = e.root;
  if (!e.arena || e.root >= e.arena->size() ||
      !std::holds_alternative<NamedArgument>(e.arena->at(e.root))) {
    wrapped = parse_view("__est_named__(" + e.source + ")");
    if (wrapped && wrapped->arena && wrapped->root < wrapped->arena->size())
      if (const auto* call = std::get_if<Call>(&wrapped->arena->at(wrapped->root));
          call && call->arguments.size() == 1) {
        view = &*wrapped; argument_id = call->arguments.front();
      }
  }
  if (view->arena && argument_id < view->arena->size())
    if (const auto* arg = std::get_if<NamedArgument>(&view->arena->at(argument_id)))
      if (auto text = view->source_text(arg->value)) {
        name = arg->name; value = std::move(*text); result = true;
      }
  if (mode() == Mode::Shadow) {
    std::string old_name, old_value;
    const bool legacy = moss::parse_named_argument(e.source, old_name, old_value);
    if (legacy != result) std::fprintf(stderr, "EST named argument mismatch <%s> legacy=%d tree=%d\n", e.source.c_str(), legacy, result);
    assert(legacy == result);
    if (result) assert(old_name == name && old_value == value);
  }
  return result;
}

std::optional<char> project_named_argument_separator(const ExpressionView& e) {
  std::optional<char> result;
  const Arena* arena = e.arena;
  ExprId argument_id = e.root;
  std::optional<ExpressionView> wrapped;
  if (!arena || e.root >= arena->size() ||
      !std::holds_alternative<NamedArgument>(arena->at(e.root))) {
    wrapped = parse_view("__est_named__(" + e.source + ")");
    if (wrapped && wrapped->arena && wrapped->root < wrapped->arena->size())
      if (const auto* call = std::get_if<Call>(&wrapped->arena->at(wrapped->root));
          call && call->arguments.size() == 1) {
        arena = wrapped->arena; argument_id = call->arguments.front();
      }
  }
  if (arena && argument_id < arena->size())
    if (const auto* argument = std::get_if<NamedArgument>(&arena->at(argument_id)))
      result = argument->separator;
  if (mode() == Mode::Shadow) {
    const auto equal = moss::top_level_assignment(e.source);
    const auto colon = moss::top_level_colon(e.source);
    std::optional<char> legacy;
    if (equal != std::string::npos &&
        (colon == std::string::npos || equal < colon)) legacy = '=';
    else if (colon != std::string::npos) legacy = ':';
    assert(result == legacy);
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
    const auto root = unparenthesized(*e.arena, e.root);
    const bool tree_index = std::holds_alternative<Index>(e.arena->at(root));
    // The legacy reader finds an index nested inside a larger expression,
    // such as `class_builder + chars_vec[i]`. The EST root proves that the
    // complete expression is not an index operation.
    const bool legacy_nested_index_false_positive = legacy && !tree_index;
    if (legacy != result && !legacy_nested_index_false_positive)
      assert(legacy == result);
    if (result) assert(old_base == base && old_index == index);
  }
  return result;
}

std::optional<std::vector<std::string>> project_vector_literal(const ExpressionView& e) {
  std::optional<std::vector<std::string>> result;
  if (e.arena && e.root < e.arena->size()) {
    const auto root = unparenthesized(*e.arena, e.root);
    if (const auto* vector = std::get_if<Collection>(&e.arena->at(root));
        vector && vector->delimiter == '[')
      result = child_texts(e, vector->elements);
  }
  if (mode() == Mode::Shadow)
    assert(moss::parse_vector_literal(e.source) == result);
  return result;
}

bool project_enum_case(const ExpressionView& e, std::string& enum_name,
                       std::string& case_name, std::vector<std::string>& fields,
                       bool* called) {
  bool result = false, is_called = false;
  if (called) *called = false;
  if (e.arena && e.root < e.arena->size()) {
    const Node* node = &e.arena->at(unparenthesized(*e.arena, e.root));
    const Member* member = nullptr;
    const std::vector<ExprId>* call_arguments = nullptr;
    if (const auto* ctor = std::get_if<Constructor>(node)) {
      member = std::get_if<Member>(&e.arena->at(ctor->type));
      call_arguments = &ctor->arguments;
    } else if (const auto* call = std::get_if<Call>(node)) {
      member = std::get_if<Member>(&e.arena->at(call->callee));
      call_arguments = &call->arguments;
    } else {
      member = std::get_if<Member>(node);
    }
    if (member)
      if (auto enum_text = e.source_text(member->base)) {
        enum_name = std::move(*enum_text); case_name = member->name;
        const auto& base_node = e.arena->at(member->base);
        const bool simple_enum = std::holds_alternative<Identifier>(base_node) ||
            std::holds_alternative<Placeholder>(base_node) ||
            (std::get_if<Literal>(&base_node) &&
             (std::get<Literal>(base_node).spelling == "true" ||
              std::get<Literal>(base_node).spelling == "false"));
        if (call_arguments) {
          if (auto args = child_texts(e, *call_arguments)) {
            fields = std::move(*args); is_called = true; result = simple_enum;
          }
        } else {
          fields.clear(); result = simple_enum;
        }
      }
  }
  if (called) *called = is_called;
  if (mode() == Mode::Shadow) {
    std::string old_enum, old_case; std::vector<std::string> old_fields;
    bool old_called = false;
    // Grouping is represented by the EST parent relation, so normalize it
    // before asking the legacy enum-case parser for the independent oracle.
    const auto legacy_source =
        moss::strip_redundant_outer_parentheses(e.source);
    const bool legacy = moss::parse_enum_case_expression(legacy_source, old_enum,
        old_case, old_fields, old_called);
    const auto root = unparenthesized(*e.arena, e.root);
    const auto& root_node = e.arena->at(root);
    const auto* call = std::get_if<Call>(&root_node);
    const auto* constructor = std::get_if<Constructor>(&root_node);
    const ExprId callee = call ? call->callee :
        constructor ? constructor->type : UINT32_MAX;
    const bool tree_case_shape = std::holds_alternative<Member>(root_node) ||
        (callee != UINT32_MAX &&
         std::holds_alternative<Member>(e.arena->at(callee)));
    // The legacy helper can select a dotted call nested inside a larger
    // expression. A non-member EST root proves that the expression itself is
    // not an enum case.
    const bool legacy_noncase_false_positive = legacy && !result &&
        !tree_case_shape;
    if (legacy != result && !legacy_noncase_false_positive)
      assert(legacy == result);
    if (result) assert(old_enum == enum_name && old_case == case_name &&
                       old_fields == fields && old_called == is_called);
  }
  return result;
}

bool project_binary(const ExpressionView& e, const std::vector<std::string>& operators,
                    std::pair<std::string, std::string>& operands,
                    std::string& operation) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  const auto root = e.root;
  const auto* tuple_root = std::get_if<Tuple>(&e.arena->at(root));
  const Binary* selected = nullptr;
  std::uint32_t selected_position = 0;
  std::function<void(ExprId)> visit = [&](ExprId id) {
    const auto* binary = std::get_if<Binary>(&e.arena->at(id));
    if (!binary) return;  // Grouping and all non-binary nodes stop top-level search.
    const bool selected_operator = std::any_of(
        operators.begin(), operators.end(), [&](const std::string& candidate) {
          const auto first = candidate.find_first_not_of(" \t\r\n");
          if (first == std::string::npos) return false;
          const auto last = candidate.find_last_not_of(" \t\r\n");
          return binary->op == candidate.substr(first, last - first + 1);
        });
    if (selected_operator) {
      const auto position = e.arena->span(binary->left).source_end;
      if (!selected || position > selected_position) {
        selected = binary; selected_position = position;
      }
    }
    visit(binary->left);
    visit(binary->right);
  };
  if (tuple_root) {
    for (const auto child : tuple_root->elements) visit(child);
  } else {
    visit(root);
  }
  if (!selected) return false;
  const auto root_span = e.arena->span(root);
  const auto body_begin = tuple_root ? e.arena->span(tuple_root->elements.front()).source_begin
                                     : root_span.source_begin;
  const auto body_end = tuple_root ? static_cast<std::uint32_t>(e.source.size() - 1)
                                   : root_span.source_end;
  const auto left_end = e.arena->span(selected->left).source_end;
  const auto right_begin = e.arena->span(selected->right).source_begin;
  if (left_end < body_begin || right_begin < left_end ||
      right_begin > body_end) return false;
  const auto gap_begin = left_end;
  const auto gap_length = right_begin - left_end;
  if (gap_begin > e.source.size() || gap_length > e.source.size() - gap_begin)
    return false;
  const auto gap = std::string_view(e.source).substr(gap_begin, gap_length);
  const auto operator_offset = gap.find(selected->op);
  if (operator_offset == std::string_view::npos) return false;
  const auto source_begin = body_begin;
  const auto split = gap_begin + operator_offset;
  const auto root_end = body_end;
  if (root_end > e.source.size() || split + selected->op.size() > root_end)
    return false;
  auto trimmed = [](std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return std::string{};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
  };
  operands = {trimmed(std::string_view(e.source).substr(source_begin, split - source_begin)),
              trimmed(std::string_view(e.source).substr(split + selected->op.size(),
                                                        root_end - split - selected->op.size()))};
  operation = selected->op;
  return true;
}

std::optional<std::vector<std::string>> project_delimited_children(
    const ExpressionView& e, char delimiter) {
  if (delimiter != ',' || !e.arena || e.root >= e.arena->size()) return std::nullopt;
  const auto& node = e.arena->at(e.root);
  if (const auto* tuple = std::get_if<Tuple>(&node)) {
    const auto whole = e.source_text(e.root);
    if (whole && whole->size() >= 2 && whole->front() == '(' &&
        whole->back() == ')')
      return std::vector<std::string>{*whole};
    return child_texts(e, tuple->elements);
  }
  auto root = e.source_text(e.root);
  if (root && root->empty()) return std::vector<std::string>{};
  if (root) return std::vector<std::string>{std::move(*root)};
  return std::nullopt;
}

std::optional<std::vector<std::string>> project_tuple_elements(
    const ExpressionView& e) {
  if (!e.arena || e.root >= e.arena->size()) return std::nullopt;
  const auto root = unparenthesized(*e.arena, e.root);
  const auto* tuple = std::get_if<Tuple>(&e.arena->at(root));
  return tuple ? child_texts(e, tuple->elements) : std::nullopt;
}

std::optional<std::vector<std::string>> parse_delimited_children(
    std::string_view source, char delimiter) {
  if (delimiter != ',') return std::nullopt;
  auto wrapped = parse_view("(" + std::string(source) + ")");
  if (!wrapped || !wrapped->arena || wrapped->root >= wrapped->arena->size())
    return std::nullopt;
  const auto* tuple = std::get_if<Tuple>(&wrapped->arena->at(wrapped->root));
  if (!tuple) return std::nullopt;
  return child_texts(*wrapped, tuple->elements);
}

void assert_delimited_parity(
    const ExpressionView& e, char delimiter,
    const std::optional<std::vector<std::string>>& tree) {
  if (mode() != Mode::Shadow) return;
  const auto legacy = moss::split_top_level(e.source, delimiter);
  assert(tree && *tree == legacy);
}

bool project_identifier(const ExpressionView& e, std::string& name) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  const auto* id = std::get_if<Identifier>(&e.arena->at(e.root));
  if (!id) return false;
  name = id->name; return true;
}

bool project_root_identifier(const ExpressionView& e, std::string& name) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  ExprId root = unparenthesized(*e.arena, e.root);
  while (root < e.arena->size()) {
    const auto& node = e.arena->at(root);
    if (const auto* identifier = std::get_if<Identifier>(&node)) {
      name = identifier->name;
      return true;
    }
    if (const auto* member = std::get_if<Member>(&node)) {
      root = unparenthesized(*e.arena, member->base);
      continue;
    }
    if (const auto* index = std::get_if<Index>(&node)) {
      root = unparenthesized(*e.arena, index->base);
      continue;
    }
    return false;
  }
  return false;
}

bool is_identifier(const ExpressionView& e) {
  std::string name;
  bool result = project_identifier(e, name);
  if (!result && e.arena && e.root < e.arena->size()) {
    if (std::holds_alternative<Placeholder>(e.arena->at(e.root))) result = true;
    if (const auto* literal = std::get_if<Literal>(&e.arena->at(e.root)))
      result = literal->spelling == "true" || literal->spelling == "false";
  }
  if (mode() == Mode::Shadow) {
    const bool legacy = moss::plain_identifier(e.source);
    assert(legacy == result);
  }
  return result;
}

bool identifier_named(const ExpressionView& e, std::string_view name) {
  std::string actual;
  bool result = false;
  if (e.arena && e.root < e.arena->size()) {
    const auto root = unparenthesized(*e.arena, e.root);
    const auto source = e.source_text(root);
    result = source && project_identifier(view(*e.arena, root, *source), actual) &&
        actual == name;
  }
  if (mode() == Mode::Shadow)
    assert(result == (moss::trim(e.source) == name ||
                      strip_grouping(e) == name));
  return result;
}

bool expressions_equal(const ExpressionView& left, const ExpressionView& right) {
  const bool result = structurally_equal(left, right);
  if (mode() == Mode::Shadow) {
    const bool legacy = moss::trim(left.source) == moss::trim(right.source);
    if (legacy != result)
      std::fprintf(stderr, "tree equality mismatch left=<%s> right=<%s> tree=%d legacy=%d\n", left.source.c_str(), right.source.c_str(), result, legacy);
    assert(result == legacy);
  }
  return result;
}

bool contains_member_call(const ExpressionView& expression,
                          std::string_view receiver,
                          std::string_view member_name) {
  if (!expression.arena || expression.root >= expression.arena->size())
    return false;
  auto expected = parse_view(receiver);
  if (!expected) return false;
  for (ExprId id = 0; id < expression.arena->size(); ++id) {
    const auto* call = std::get_if<Call>(&expression.arena->at(id));
    if (!call) continue;
    const auto callee = unparenthesized(*expression.arena, call->callee);
    const auto* access = std::get_if<Member>(&expression.arena->at(callee));
    if (!access || access->name != member_name) continue;
    auto source = expression.source_text(access->base);
    if (!source) continue;
    auto actual = parse_view(*source);
    if (actual && structurally_equal(*actual, *expected)) return true;
  }
  return false;
}

bool empty_vector_literal(const ExpressionView& e) {
  bool result = false;
  if (e.arena && e.root < e.arena->size()) {
    const auto root = unparenthesized(*e.arena, e.root);
    if (const auto* collection = std::get_if<Collection>(&e.arena->at(root)))
      result = collection->delimiter == '[' && collection->elements.empty();
  }
  if (mode() == Mode::Shadow) assert((moss::trim(e.source) == "[]") == result);
  return result;
}

bool member_expression(const ExpressionView& e) {
  bool result = false;
  if (e.arena && e.root < e.arena->size()) {
    const auto& node = e.arena->at(e.root);
    result = std::holds_alternative<Member>(node);
    if (const auto* call = std::get_if<Call>(&node))
      result = std::holds_alternative<Member>(e.arena->at(call->callee));
  }
  if (mode() == Mode::Shadow)
    assert((moss::trim(e.source).find('.') != std::string::npos) == result);
  return result;
}

bool empty_constructor_named(const ExpressionView& e, std::string_view name) {
  bool result = false;
  if (e.arena && e.root < e.arena->size()) {
    const auto root = unparenthesized(*e.arena, e.root);
    const auto& node = e.arena->at(root);
    if (const auto* constructor = std::get_if<Constructor>(&node)) {
      std::string type;
      result = project_identifier(view(*e.arena, constructor->type,
          e.source_text(constructor->type).value_or(std::string{})), type) &&
          type == name && constructor->arguments.empty();
    } else if (const auto* call = std::get_if<Call>(&node)) {
      std::string callee;
      result = project_identifier(view(*e.arena, call->callee,
          e.source_text(call->callee).value_or(std::string{})), callee) &&
          callee == name && call->arguments.empty();
    }
  }
  if (mode() == Mode::Shadow) {
    const auto source = moss::trim(e.source);
    if (source == std::string(name) + "()")
      assert(result);
    else if (source.find('(') == 0)
      assert(result == (strip_grouping(e) == std::string(name) + "()"));
    else
      assert(!result);
  }
  return result;
}

std::optional<std::string> project_empty_constructor_type(
    const ExpressionView& e) {
  if (!e.arena || e.root >= e.arena->size()) return std::nullopt;
  const auto root = unparenthesized(*e.arena, e.root);
  if (const auto* constructor = std::get_if<Constructor>(&e.arena->at(root));
      constructor && constructor->arguments.empty()) {
    auto type = e.source_text(constructor->type);
    if (!type) return std::nullopt;
    if (!constructor->type_arguments.empty()) {
      *type += "[";
      for (std::size_t index = 0;
           index < constructor->type_arguments.size(); ++index) {
        if (index) *type += ", ";
        auto argument = e.source_text(constructor->type_arguments[index]);
        if (!argument) return std::nullopt;
        *type += *argument;
      }
      *type += "]";
    }
    return type;
  }
  return std::nullopt;
}

LiteralClass root_literal_class(const ExpressionView& e) {
  if (!e.arena || e.root >= e.arena->size()) return LiteralClass::None;
  const auto root = unparenthesized(*e.arena, e.root);
  if (auto decoded = numeric_literal_value(e))
    return decoded->kind == NumericLiteral::Kind::Int
        ? LiteralClass::Integer : LiteralClass::Float;
  const auto* literal = std::get_if<Literal>(&e.arena->at(root));
  if (!literal) return LiteralClass::None;
  return literal->literal_class;
}

bool decimal_integer_literal(const ExpressionView& e, bool allow_sign,
                             bool allow_underscores) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  const auto root = unparenthesized(*e.arena, e.root);
  ExprId literal_id = root;
  for (;;) {
    if (const auto* unary = std::get_if<Unary>(&e.arena->at(literal_id))) {
      if (!allow_sign || (unary->op != "+" && unary->op != "-")) return false;
      literal_id = unparenthesized(*e.arena, unary->operand);
      continue;
    }
    break;
  }
  const auto* literal = std::get_if<Literal>(&e.arena->at(literal_id));
  if (!literal || literal->literal_class != LiteralClass::Integer) return false;
  return allow_underscores ? literal->decimal_integer_separators
                           : literal->decimal_integer_digits;
}

std::optional<DecodedNumber> numeric_literal_value(const ExpressionView& e) {
  if (!e.arena || e.root >= e.arena->size()) return std::nullopt;
  ExprId current = unparenthesized(*e.arena, e.root);
  bool negative = false;
  while (const auto* unary = std::get_if<Unary>(&e.arena->at(current))) {
    if (unary->op != "+" && unary->op != "-") return std::nullopt;
    if (unary->op == "-") negative = !negative;
    current = unparenthesized(*e.arena, unary->operand);
  }
  const auto* literal = std::get_if<Literal>(&e.arena->at(current));
  if (!literal || (literal->literal_class != LiteralClass::Integer &&
                   literal->literal_class != LiteralClass::Float))
    return std::nullopt;
  if (literal->literal_class == LiteralClass::Float)
    return DecodedNumber{NumericLiteral::Kind::Float, 0,
        negative ? -literal->numeric.floating : literal->numeric.floating};
  if (!int_literal_in_range(literal->numeric, negative)) return std::nullopt;
  const auto magnitude = literal->numeric.magnitude;
  const std::int64_t integer = negative
      ? magnitude == kNumericIntMinMagnitude
          ? std::numeric_limits<std::int64_t>::min()
          : -static_cast<std::int64_t>(magnitude)
      : static_cast<std::int64_t>(magnitude);
  return DecodedNumber{NumericLiteral::Kind::Int, integer, 0.0};
}

bool placeholder_expression(const ExpressionView& e) {
  const bool result = e.arena && e.root < e.arena->size() &&
      std::holds_alternative<Placeholder>(e.arena->at(e.root));
  if (mode() == Mode::Shadow) assert((moss::trim(e.source) == "_") == result);
  return result;
}

bool expression_mentions(const ExpressionView& e, std::string_view name) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  bool found = false; mentions_node(*e.arena, e.root, name, found); return found;
}

bool contains_identifier(const ExpressionView& e, std::string_view name) {
  if (!e.arena || e.root >= e.arena->size()) return false;
  bool found = false;
  identifier_occurs_node(*e.arena, e.root, name, found);
  return found;
}

bool structurally_equal(const ExpressionView& left, const ExpressionView& right) {
  return left.arena && right.arena && left.root < left.arena->size() &&
      right.root < right.arena->size() &&
      equal_node(*left.arena, left.root, *right.arena, right.root);
}

}  // namespace moss::est
