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
}

bool project_simple_call(const ExpressionView& e, std::string& callee,
                         std::vector<std::string>& arguments) {
  bool result = false;
  if (e.arena && e.root < e.arena->size()) {
    if (const auto* call = std::get_if<Call>(&e.arena->at(e.root))) {
      if (auto name = e.source_text(call->callee))
        if (auto args = child_texts(e, call->arguments)) {
          callee = std::move(*name); arguments = std::move(*args); result = true;
        }
    } else if (const auto* constructor =
                   std::get_if<Constructor>(&e.arena->at(e.root))) {
      if (auto name = e.source_text(constructor->type))
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
        if (const auto* stage = std::get_if<Call>(&e.arena->at(pipeline->stages.front())))
          if (auto input = e.source_text(pipeline->input))
            if (auto name = e.source_text(stage->callee))
              if (auto args = child_texts(e, stage->arguments)) {
                callee = *input + " |> " + *name;
                arguments = std::move(*args); result = true;
              }
    }
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
    if (const auto* message = std::get_if<Message>(&e.arena->at(e.root)))
      if (auto base = e.source_text(message->target))
        assign_member(std::move(*base), message->handler,
                      message->arguments, true);
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

std::string strip_grouping(const ExpressionView& e) {
  std::string result = e.source_text(unparenthesized(*e.arena, e.root)).value_or(e.source);
  if (mode() == Mode::Shadow)
    assert(result == moss::strip_redundant_outer_parentheses(e.source));
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
        }
        break;
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
      if (const auto* message = std::get_if<Message>(&node)) {
        auto arguments = child_texts(e, message->arguments);
        if (!arguments) break;
        reverse.emplace_back(message->handler, std::move(*arguments));
        current = message->target;
        continue;
      }
      break;
    }
    if (!result) calls = std::move(reverse);
  }
  if (mode() == Mode::Shadow) {
    std::vector<std::pair<std::string, std::vector<std::string>>> legacy;
    const bool old = moss::parse_executor_chain(e.source, legacy);
    assert(old == result && legacy == calls);
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

std::optional<char> project_named_argument_separator(const ExpressionView& e) {
  std::optional<char> result;
  if (e.arena && e.root < e.arena->size())
    if (const auto* argument = std::get_if<NamedArgument>(&e.arena->at(e.root)))
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
    const bool legacy = moss::parse_enum_case_expression(e.source, old_enum,
        old_case, old_fields, old_called);
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
  const Binary* selected = nullptr;
  std::uint32_t selected_position = 0;
  std::function<void(ExprId)> visit = [&](ExprId id) {
    const auto* binary = std::get_if<Binary>(&e.arena->at(id));
    if (!binary) return;  // Grouping and all non-binary nodes stop top-level search.
    if (std::find(operators.begin(), operators.end(), binary->op) != operators.end()) {
      const auto position = e.arena->span(binary->left).source_end;
      if (!selected || position > selected_position) {
        selected = binary; selected_position = position;
      }
    }
    visit(binary->left);
    visit(binary->right);
  };
  visit(root);
  if (!selected) return false;
  const auto root_span = e.arena->span(root);
  const auto left_end = e.arena->span(selected->left).source_end;
  const auto right_begin = e.arena->span(selected->right).source_begin;
  if (left_end < root_span.source_begin || right_begin < left_end ||
      right_begin > root_span.source_end) return false;
  const auto gap_begin = left_end - root_span.source_begin;
  const auto gap_length = right_begin - left_end;
  if (gap_begin > e.source.size() || gap_length > e.source.size() - gap_begin)
    return false;
  const auto gap = std::string_view(e.source).substr(gap_begin, gap_length);
  const auto operator_offset = gap.find(selected->op);
  if (operator_offset == std::string_view::npos) return false;
  const auto split = gap_begin + operator_offset;
  const auto root_end = root_span.source_end - root_span.source_begin;
  if (root_end > e.source.size() || split + selected->op.size() > root_end)
    return false;
  auto trimmed = [](std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return std::string{};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
  };
  operands = {trimmed(std::string_view(e.source).substr(0, split)),
              trimmed(std::string_view(e.source).substr(split + selected->op.size(),
                                                        root_end - split - selected->op.size()))};
  operation = selected->op;
  return true;
}

std::optional<std::vector<std::string>> project_delimited_children(
    const ExpressionView& e, char delimiter) {
  if (delimiter != ',' || !e.arena || e.root >= e.arena->size()) return std::nullopt;
  const auto& node = e.arena->at(e.root);
  if (const auto* tuple = std::get_if<Tuple>(&node))
    return child_texts(e, tuple->elements);
  auto root = e.source_text(e.root);
  if (root && root->empty()) return std::vector<std::string>{};
  if (root) return std::vector<std::string>{std::move(*root)};
  return std::nullopt;
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
