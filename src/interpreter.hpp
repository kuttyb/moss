#pragma once

#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast.hpp"

namespace moss {

// Fast Debug is deliberately an execution backend over the checked Moss AST.
// It does not parse, resolve, or type-check anything itself.  The checker has
// already rejected unsupported ownership/concurrency patterns before this
// class is constructed.
class FastInterpreter {
 public:
  struct TraceEvent {
    std::string kind;
    std::string function;
    std::string source_file;
    std::string semantic_identity;
    int line = 0;
    std::string detail;
    std::string instance, specialization, handler, path, before, after;
  };

  struct Options {
    bool trace = false;
    std::string source_file;
  };

  class RuntimeError : public std::runtime_error {
   public:
    RuntimeError(int line, std::string message)
        : std::runtime_error(std::move(message)), line_(line) {}
    int line() const { return line_; }

   private:
    int line_;
  };

  explicit FastInterpreter(const Program& program)
      : program_(program) {}
  FastInterpreter(const Program& program, Options options)
      : program_(program), options_(options) {}

  const std::vector<TraceEvent>& trace() const { return trace_; }

  void write_trace(std::ostream& out) const {
    for (const auto& event : trace_) {
      out << "{\"event\":\"" << escape(event.kind)
          << "\",\"function\":\"" << escape(event.function)
          << "\",\"source_file\":\"" << escape(event.source_file)
          << "\",\"semantic_identity\":\""
          << escape(event.semantic_identity)
          << "\",\"line\":" << event.line;
      if (!event.detail.empty())
        out << ",\"detail\":\"" << escape(event.detail) << "\"";
      for (const auto& field : {std::make_pair("instance", event.instance),
          std::make_pair("specialization", event.specialization), std::make_pair("handler", event.handler),
          std::make_pair("path", event.path), std::make_pair("before", event.before), std::make_pair("after", event.after)})
        if (!field.second.empty()) out << ",\"" << field.first << "\":\"" << escape(field.second) << "\"";
      out << "}\n";
    }
  }

  void run_main(std::ostream& output) {
    if (!program_.main)
      throw RuntimeError(0, "Fast Debug requires a Moss main procedure");
    prepare_domains();
    Frame frame;
    frame.function = "main";
    frame.source_file = program_.main->source_file;
    frame.semantic_identity = "main@" + std::to_string(program_.main->line);
    execute(program_.main->body, frame, output);
  }

  void run_tests(std::ostream& output, const std::string& filter = {}) {
    size_t discovered = 0;
    for (const auto& test : program_.tests) {
      if (!filter.empty() && test.name.find(filter) == std::string::npos &&
          test.semantic_identity.find(filter) == std::string::npos)
        continue;
      ++discovered;
      Frame frame;
      frame.function = "test:" + test.name;
      frame.source_file = test.source_file;
      frame.semantic_identity = test.semantic_identity;
      execute(test.body, frame, output);
      output << "PASS " << test.name << "\n";
    }
    if (!discovered)
      throw RuntimeError(0, filter.empty() ? "no Moss tests were found"
                                          : "no Moss tests matched filter '" + filter + "'");
    output << discovered << " passed\n";
  }

 private:
  struct StructValue;
  struct DomainValue;

  struct Value {
    enum class Kind { Unit, Bool, Int, Float, String, Struct, Vector, Queue, Map, DomainHandle } kind = Kind::Unit;
    bool boolean = false;
    std::int64_t integer = 0;
    double floating = 0.0;
    std::string string;
    DomainValue* domain = nullptr;
    std::shared_ptr<StructValue> object;
    std::shared_ptr<std::vector<Value>> vector;
    std::shared_ptr<std::vector<Value>> queue;
    std::shared_ptr<std::vector<std::pair<Value, Value>>> map;
    std::string element_type;

    static Value unit();
    static Value boolean_value(bool value);
    static Value int_value(std::int64_t value);
    static Value float_value(double value);
    static Value string_value(std::string value);
    static Value struct_value(std::string type);
    static Value vector_value(std::vector<Value> values, std::string element_type = {});
    static Value queue_value();
    static Value map_value();
    bool truthy() const;
    std::string display() const;
  };

  struct StructValue {
    std::string type;
    std::unordered_map<std::string, Value> fields;
  };

  struct DomainValue {
    const ConcreteDomainInstance* concrete = nullptr;
    const Domain* definition = nullptr;
    std::unordered_map<std::string, Value> state;
    std::unordered_map<std::string, DomainValue*> routes;
    bool initialized = false;
  };
  struct Place { Value* value = nullptr; DomainValue* domain = nullptr; std::string path; };
  struct Frame {
    std::string function;
    std::string source_file;
    std::string semantic_identity;
    std::unordered_map<std::string, Value> locals;
    std::unordered_map<std::string, Place> aliases;
    DomainValue* domain = nullptr;
    std::string handler;
    bool handler_scope = false;
  };

  struct Flow {
    bool returned = false;
    Value value;
  };

  const Program& program_;
  Options options_;
  std::vector<TraceEvent> trace_;
  std::map<std::string, std::unique_ptr<DomainValue>> instances_;

  static std::string escape(const std::string& value) {
    std::string result;
    for (char c : value) {
      if (c == '\\' || c == '"') result.push_back('\\');
      if (c == '\n') result += "\\n";
      else if (c == '\r') result += "\\r";
      else if (c == '\t') result += "\\t";
      else result.push_back(c);
    }
    return result;
  }

  void emit(const std::string& kind, const Frame& frame, int line,
            std::string detail = {}) {
    if (!options_.trace) return;
    TraceEvent event;
    event.kind = kind; event.function = frame.function; event.source_file = frame.source_file.empty() ? options_.source_file : frame.source_file;
    event.semantic_identity = frame.semantic_identity; event.line = line;
    event.detail = detail.substr(0, 256);
    if (frame.domain) { event.instance = frame.domain->concrete->identity;
      event.specialization = frame.domain->concrete->specialization; event.handler = frame.handler; }
    trace_.push_back(std::move(event));
  }

  static std::string trim_copy(std::string value) {
    auto is_space = [](unsigned char c) { return std::isspace(c); };
    size_t begin = 0;
    while (begin < value.size() && is_space(static_cast<unsigned char>(value[begin]))) ++begin;
    size_t end = value.size();
    while (end > begin && is_space(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
  }

  static bool identifier(const std::string& value) {
    if (value.empty() || !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_')) return false;
    for (size_t i = 1; i < value.size(); ++i)
      if (!(std::isalnum(static_cast<unsigned char>(value[i])) || value[i] == '_')) return false;
    return true;
  }

  static bool outer_parentheses(const std::string& value) {
    if (value.size() < 2 || value.front() != '(' || value.back() != ')') return false;
    int depth = 0; bool quoted = false; bool escaped = false;
    for (size_t i = 0; i < value.size(); ++i) {
      char c = value[i];
      if (quoted) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == '"') quoted = false;
        continue;
      }
      if (c == '"') quoted = true;
      else if (c == '(') ++depth;
      else if (c == ')' && --depth == 0 && i + 1 != value.size()) return false;
    }
    return depth == 0;
  }

  static std::optional<std::pair<std::string, std::string>> split_operator(
      const std::string& input, const std::vector<std::string>& operators) {
    int parens = 0, brackets = 0, braces = 0; bool quoted = false; bool escaped = false;
    for (size_t pos = input.size(); pos-- > 0;) {
      char c = input[pos];
      if (quoted) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == '"') quoted = false;
        continue;
      }
      if (c == '"') { quoted = true; continue; }
      if (c == ')') ++parens; else if (c == '(') --parens;
      else if (c == ']') ++brackets; else if (c == '[') --brackets;
      else if (c == '}') ++braces; else if (c == '{') --braces;
      if (parens || brackets || braces) continue;
      for (const auto& op : operators) {
        if (pos + op.size() <= input.size() && input.compare(pos, op.size(), op) == 0) {
          if (op == "-" || op == "+") {
            size_t before = pos;
            while (before > 0 && std::isspace(static_cast<unsigned char>(input[before - 1])))
              --before;
            if (before == 0) continue;
            char previous = input[before - 1];
            if (previous == '(' || previous == '[' || previous == '{' ||
                previous == ',' || previous == ':' || previous == '+' ||
                previous == '-' || previous == '*' || previous == '/' ||
                previous == '%' || previous == '=' || previous == '<' ||
                previous == '>' || previous == '!' || previous == '|')
              continue;
          }
          return std::make_pair(trim_copy(input.substr(0, pos)),
                                trim_copy(input.substr(pos + op.size())));
        }
      }
    }
    return std::nullopt;
  }

  static std::string operator_between(const std::string& whole,
                                      const std::string& left,
                                      const std::string& right,
                                      const std::vector<std::string>& operators) {
    int parens = 0, brackets = 0, braces = 0; bool quoted = false; bool escaped = false;
    for (size_t pos = 0; pos < whole.size(); ++pos) {
      char c = whole[pos];
      if (quoted) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == '"') quoted = false;
        continue;
      }
      if (c == '"') { quoted = true; continue; }
      if (c == '(') ++parens; else if (c == ')') --parens;
      else if (c == '[') ++brackets; else if (c == ']') --brackets;
      else if (c == '{') ++braces; else if (c == '}') --braces;
      if (parens || brackets || braces) continue;
      for (const auto& op : operators) {
        if (pos + op.size() > whole.size() || whole.compare(pos, op.size(), op) != 0) continue;
        if (trim_copy(whole.substr(0, pos)) == left &&
            trim_copy(whole.substr(pos + op.size())) == right)
          return op;
      }
    }
    return {};
  }

  static bool parse_call(const std::string& input, std::string& callee,
                         std::vector<std::string>& args) {
    auto value = trim_copy(input);
    auto lp = value.find('(');
    if (lp == std::string::npos || value.back() != ')') return false;
    int depth = 0; bool quoted = false;
    for (size_t i = lp; i < value.size(); ++i) {
      if (value[i] == '"') quoted = !quoted;
      if (quoted) continue;
      if (value[i] == '(') ++depth;
      else if (value[i] == ')' && --depth == 0 && i + 1 != value.size()) return false;
    }
    if (depth != 0) return false;
    callee = trim_copy(value.substr(0, lp));
    if (callee.empty()) return false;
    auto inside = value.substr(lp + 1, value.size() - lp - 2);
    args.clear();
    int p = 0, b = 0, s = 0; size_t start = 0; quoted = false;
    for (size_t i = 0; i < inside.size(); ++i) {
      char c = inside[i];
      if (c == '"') quoted = !quoted;
      if (quoted) continue;
      if (c == '(') ++p; else if (c == ')') --p;
      else if (c == '[') ++b; else if (c == ']') --b;
      else if (c == '{') ++s; else if (c == '}') --s;
      else if (c == ',' && p == 0 && b == 0 && s == 0) {
        args.push_back(trim_copy(inside.substr(start, i - start))); start = i + 1;
      }
    }
    if (!trim_copy(inside.substr(start)).empty()) args.push_back(trim_copy(inside.substr(start)));
    return true;
  }

  static std::optional<std::pair<std::string, std::string>> top_level_member(
      const std::string& input) {
    int p = 0, b = 0, s = 0; bool quoted = false;
    for (size_t i = input.size(); i-- > 0;) {
      char c = input[i];
      if (c == '"') quoted = !quoted;
      if (quoted) continue;
      if (c == ')') ++p; else if (c == '(') --p;
      else if (c == ']') ++b; else if (c == '[') --b;
      else if (c == '}') ++s; else if (c == '{') --s;
      else if (c == '.' && p == 0 && b == 0 && s == 0)
        return std::make_pair(trim_copy(input.substr(0, i)), trim_copy(input.substr(i + 1)));
    }
    return std::nullopt;
  }

  const Function* function(const std::string& name) const {
    for (const auto& candidate : program_.functions)
      if (candidate.name == name) return &candidate;
    return nullptr;
  }

  const ObjectType* object_type(const std::string& name) const {
    for (const auto& candidate : program_.objects)
      if (candidate.name == name) return &candidate;
    return nullptr;
  }

  const Method* method(const std::string& owner, const std::string& name) const {
    auto object = object_type(owner);
    if (!object) return nullptr;
    for (const auto& candidate : object->methods)
      if (candidate.name == name) return &candidate;
    return nullptr;
  }

  static std::int64_t wrapping_add(std::int64_t left, std::int64_t right) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) +
                                     static_cast<std::uint64_t>(right));
  }
  static std::int64_t wrapping_sub(std::int64_t left, std::int64_t right) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) -
                                     static_cast<std::uint64_t>(right));
  }
  static std::int64_t wrapping_mul(std::int64_t left, std::int64_t right) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) *
                                     static_cast<std::uint64_t>(right));
  }

  static std::vector<std::string> split_pipeline(const std::string& input) {
    std::vector<std::string> result;
    int parens = 0, brackets = 0; bool quoted = false; size_t start = 0;
    for (size_t i = 0; i + 1 < input.size(); ++i) {
      char c = input[i];
      if (c == '"') quoted = !quoted;
      if (quoted) continue;
      if (c == '(') ++parens; else if (c == ')') --parens;
      else if (c == '[') ++brackets; else if (c == ']') --brackets;
      if (!parens && !brackets && c == '|' && input[i + 1] == '>') {
        result.push_back(trim_copy(input.substr(start, i - start)));
        start = i + 2; ++i;
      }
    }
    if (start) result.push_back(trim_copy(input.substr(start)));
    return result;
  }

  Value invoke_pipeline_callable(const std::string& callable,
                                 const std::vector<Value>& values,
                                 Frame& frame, int line, std::ostream& output) {
    Frame callback = frame;
    std::vector<std::string> names;
    for (size_t i = 0; i < values.size(); ++i) {
      std::string name = "__moss_pipeline_arg_" + std::to_string(i);
      callback.locals[name] = values[i]; names.push_back(name);
    }
    if (values.size() == 1) callback.locals["_"] = values.front();
    if (auto fn = function(trim_copy(callable)))
      return call(*fn, names, callback, line, output);
    if (callable.find('_') != std::string::npos)
      return eval(callable, callback, line, output);
    std::string invocation = callable + "(";
    for (size_t i = 0; i < names.size(); ++i) {
      if (i) invocation += ", ";
      invocation += names[i];
    }
    return eval(invocation + ")", callback, line, output);
  }

  std::optional<std::string> checked_pipeline_stage_output_type(
      size_t stage_index, const Frame& frame, int line) const {
    for (const auto& pipeline : program_.functional_pipelines) {
      if (pipeline.line != line || pipeline.context != frame.function ||
          stage_index >= pipeline.nodes.size())
        continue;
      return pipeline.nodes[stage_index].output_type;
    }
    return std::nullopt;
  }

  static std::optional<std::string> checked_vector_element_type(
      const std::string& type) {
    if (type.rfind("vector[", 0) != 0 || type.size() <= 8 || type.back() != ']')
      return std::nullopt;
    return type.substr(7, type.size() - 8);
  }

  Value eval_pipeline(const std::vector<std::string>& stages, Frame& frame,
                      int line, std::ostream& output) {
    if (stages.size() < 2) return Value::unit();
    Value current = eval(stages.front(), frame, line, output);
    for (size_t stage_index = 1; stage_index < stages.size(); ++stage_index) {
      std::string callee; std::vector<std::string> args;
      if (!parse_call(stages[stage_index], callee, args)) callee = stages[stage_index];
      if (callee == "sum") {
        if (current.kind != Value::Kind::Vector) throw RuntimeError(line, "pipeline sum requires a Vector");
        Value total = (current.element_type == "Float" || current.element_type == "float")
            ? Value::float_value(0.0) : Value::int_value(0);
        for (const auto& value : *current.vector) {
          if (value.kind == Value::Kind::Float || total.kind == Value::Kind::Float) {
            double left = total.kind == Value::Kind::Float ? total.floating : total.integer;
            double right = value.kind == Value::Kind::Float ? value.floating : value.integer;
            total = Value::float_value(left + right);
          } else total = Value::int_value(wrapping_add(total.integer, value.integer));
        }
        current = total;
      } else if (callee == "count") {
        if (current.kind != Value::Kind::Vector) throw RuntimeError(line, "pipeline count requires a Vector");
        current = Value::int_value(static_cast<std::int64_t>(current.vector->size()));
      } else if (callee == "map" || callee == "filter") {
        if (current.kind != Value::Kind::Vector || args.size() != 1)
          throw RuntimeError(line, "invalid checked pipeline stage '" + callee + "'");
        std::vector<Value> values;
        for (const auto& value : *current.vector) {
          Value transformed = invoke_pipeline_callable(args.front(), {value}, frame, line, output);
          if (callee == "map" || transformed.truthy()) values.push_back(callee == "map" ? transformed : value);
        }
        std::string element_type = current.element_type;
        if (callee == "map") {
          auto output_type = checked_pipeline_stage_output_type(stage_index, frame, line);
          auto output_element = output_type ? checked_vector_element_type(*output_type) : std::nullopt;
          if (!output_element)
            throw RuntimeError(line, "internal error: missing checked map output type");
          element_type = *output_element;
        }
        current = Value::vector_value(std::move(values),
                                      std::move(element_type));
      } else if (callee == "reduce") {
        if (current.kind != Value::Kind::Vector || args.size() != 2)
          throw RuntimeError(line, "invalid checked pipeline stage 'reduce'");
        Value reduced = eval(args[0], frame, line, output);
        for (const auto& value : *current.vector)
          reduced = invoke_pipeline_callable(args[1], {reduced, value}, frame, line, output);
        current = reduced;
      } else if (callee == "any" || callee == "all") {
        if (current.kind != Value::Kind::Vector || args.size() != 1)
          throw RuntimeError(line, "invalid checked pipeline stage '" + callee + "'");
        bool result = callee == "all";
        for (const auto& value : *current.vector) {
          bool predicate = invoke_pipeline_callable(args.front(), {value}, frame, line, output).truthy();
          if (callee == "any" && predicate) result = true;
          if (callee == "all" && !predicate) result = false;
        }
        current = Value::boolean_value(result);
      } else throw RuntimeError(line, "unsupported checked pipeline stage '" + callee + "'");
    }
    return current;
  }

#include "interpreter_domains.inc"

  Value eval(const std::string& expression, Frame& frame, int line,
             std::ostream& output) {
    std::string e = trim_copy(expression);
    while (outer_parentheses(e)) e = trim_copy(e.substr(1, e.size() - 2));
    if (e.empty()) return Value::unit();
    if (auto stages = split_pipeline(e); !stages.empty())
      return eval_pipeline(stages, frame, line, output);
    if (e.rfind("message ", 0) == 0) return message(e.substr(8), frame, line, output);
    if (auto place = locate(e, frame, line, output); place.value) {
      if (place.domain) state_event("state_read", frame, line, place);
      else emit("LocalRead", frame, line, e);
      return *place.value;
    }
    if (frame.handler_scope && frame.domain && frame.domain->routes.count(e)) {
      Value value; value.kind = Value::Kind::DomainHandle; value.domain = frame.domain->routes.at(e); return value;
    }
    if (e == "true") return Value::boolean_value(true);
    if (e == "false") return Value::boolean_value(false);
    if (e == "not" || e == "!") return Value::boolean_value(false);
    if (e.rfind("not ", 0) == 0) return Value::boolean_value(!eval(e.substr(4), frame, line, output).truthy());
    if (e.rfind("assert ", 0) == 0) {
      auto condition = e.substr(7);
      if (!eval(condition, frame, line, output).truthy()) {
        emit("AssertionFailure", frame, line, condition);
        throw RuntimeError(line, "assertion failed: " + condition);
      }
      return Value::unit();
    }
    if (e.front() == '"' && e.back() == '"') return Value::string_value(decode_string(e.substr(1, e.size() - 2)));

    char* end = nullptr;
    const auto numeric = e.c_str();
    long long integer = std::strtoll(numeric, &end, 10);
    if (end && *end == '\0' && e.find_first_of(".eE") == std::string::npos)
      return Value::int_value(static_cast<std::int64_t>(integer));
    char* float_end = nullptr;
    double floating = std::strtod(numeric, &float_end);
    if (float_end && *float_end == '\0' && e.find_first_of(".eE") != std::string::npos)
      return Value::float_value(floating);

    if (auto binary = split_operator(e, {"==", "!=", "<=", ">=", "<", ">"}))
      return compare(binary->first, binary->second, e, frame, line, output);
    if (auto binary = split_operator(e, {"+", "-"}))
      return arithmetic(binary->first, binary->second, e, frame, line, output);
    if (auto binary = split_operator(e, {"*", "/", "%"}))
      return arithmetic(binary->first, binary->second, e, frame, line, output);
    if (!e.empty() && e.front() == '-' && e.size() > 1) {
      Value value = eval(e.substr(1), frame, line, output);
      if (value.kind == Value::Kind::Int) return Value::int_value(wrapping_sub(0, value.integer));
      if (value.kind == Value::Kind::Float) return Value::float_value(-value.floating);
    }
    if (e.front() == '[' && e.back() == ']') {
      std::vector<Value> values;
      auto inside = e.substr(1, e.size() - 2);
      for (const auto& part : split_arguments(inside)) values.push_back(eval(part, frame, line, output));
      return Value::vector_value(std::move(values));
    }

    std::string callee; std::vector<std::string> args;
    if (parse_call(e, callee, args)) {
      if (callee.find('.') == std::string::npos) {
        if (args.empty() && callee.rfind("Vector[", 0) == 0 &&
            callee.size() > 8 && callee.back() == ']')
          return Value::vector_value({}, callee.substr(7, callee.size() - 8));
        if (callee == "Queue" && args.empty()) return Value::queue_value();
        if (callee == "Map" && args.empty()) return Value::map_value();
        if (callee == "assert") {
          if (args.size() != 1) throw RuntimeError(line, "assert expects one argument");
          if (!eval(args.front(), frame, line, output).truthy()) {
            emit("AssertionFailure", frame, line, args.front());
            throw RuntimeError(line, "assertion failed: " + args.front());
          }
          return Value::unit();
        }
        if (callee == "assertEqual") {
          if (args.size() != 2) throw RuntimeError(line, "assertEqual expects two arguments");
          auto actual = eval(args[0], frame, line, output);
          auto expected = eval(args[1], frame, line, output);
          if (!equal(actual, expected)) {
            emit("AssertionFailure", frame, line, e);
            throw RuntimeError(line, "assertEqual failed: actual=" + actual.display() +
                               ", expected=" + expected.display());
          }
          return Value::unit();
        }
        if (callee == "sqrt") {
          if (args.size() != 1) throw RuntimeError(line, "sqrt expects one argument");
          auto value = eval(args.front(), frame, line, output);
          return Value::float_value(std::sqrt(value.kind == Value::Kind::Int ? value.integer : value.floating));
        }
        if (auto fn = function(callee)) return call(*fn, args, frame, line, output);
        if (auto object = object_type(callee)) return construct(*object, args, frame, line, output);
        throw RuntimeError(line, "unsupported or unresolved callable '" + callee + "'");
      }
    }

    if (auto member = top_level_member(e)) {
      if (member->second.find('(') != std::string::npos) {
        std::string method_name; std::vector<std::string> method_args;
        if (parse_call(member->second, method_name, method_args)) {
          auto receiver = eval(member->first, frame, line, output);
          if (receiver.kind == Value::Kind::Vector || receiver.kind == Value::Kind::Queue) {
            auto values = receiver.kind == Value::Kind::Vector ? receiver.vector : receiver.queue;
            if (method_name == "push" && method_args.size() == 1) {
              values->push_back(eval(method_args.front(), frame, line, output));
              return Value::unit();
            }
            if (method_name == "pop" && method_args.empty()) {
              if (values->empty()) throw RuntimeError(line, "pop from empty collection");
              Value result = receiver.kind == Value::Kind::Queue ? values->front() : values->back();
              if (receiver.kind == Value::Kind::Queue) values->erase(values->begin());
              else values->pop_back();
              return result;
            }
          }
          if (receiver.kind == Value::Kind::Map) {
            if (method_name == "get" && method_args.size() == 2) {
              Value key = eval(method_args[0], frame, line, output);
              Value fallback = eval(method_args[1], frame, line, output);
              for (const auto& entry : *receiver.map)
                if (equal(entry.first, key)) return independent(entry.second);
              return independent(fallback);
            }
            if ((method_name == "keys" || method_name == "values") && method_args.empty()) {
              std::vector<Value> values;
              for (const auto& entry : *receiver.map)
                values.push_back(independent(method_name == "keys" ? entry.first : entry.second));
              return Value::vector_value(std::move(values));
            }
          }
          if (receiver.kind != Value::Kind::Struct || !receiver.object)
            throw RuntimeError(line, "method receiver is not a Moss object");
          auto target = method(receiver.object->type, method_name);
          if (!target) throw RuntimeError(line, "unresolved method '" + method_name + "'");
          return call_method(*target, receiver, method_args, frame, line, output,
                             locate(member->first, frame, line, output));
        }
      }
      auto receiver = eval(member->first, frame, line, output);
      if (receiver.kind != Value::Kind::Struct || !receiver.object)
        throw RuntimeError(line, "field receiver is not a Moss object");
      auto field = receiver.object->fields.find(member->second);
      if (field == receiver.object->fields.end()) throw RuntimeError(line, "unknown field '" + member->second + "'");
      return field->second;
    }

    if (identifier(e)) {
      auto found = frame.locals.find(e);
      if (found != frame.locals.end()) {
        emit("LocalRead", frame, line, e);
        return found->second;
      }
      throw RuntimeError(line, "unknown local '" + e + "'");
    }
    throw RuntimeError(line, "unsupported expression '" + e + "'");
  }

  static std::string decode_string(const std::string& value) {
    std::string result;
    bool escaped = false;
    for (char c : value) {
      if (escaped) {
        if (c == 'n') result.push_back('\n'); else if (c == 'r') result.push_back('\r');
        else if (c == 't') result.push_back('\t'); else result.push_back(c);
        escaped = false;
      } else if (c == '\\') escaped = true;
      else result.push_back(c);
    }
    return result;
  }

  static std::vector<std::string> split_arguments(const std::string& input) {
    std::vector<std::string> result; int p = 0, b = 0, s = 0; bool quoted = false; size_t start = 0;
    for (size_t i = 0; i < input.size(); ++i) {
      char c = input[i]; if (c == '"') quoted = !quoted;
      if (quoted) continue;
      if (c == '(') ++p; else if (c == ')') --p; else if (c == '[') ++b; else if (c == ']') --b;
      else if (c == '{') ++s; else if (c == '}') --s;
      else if (c == ',' && p == 0 && b == 0 && s == 0) {
        result.push_back(trim_copy(input.substr(start, i - start))); start = i + 1;
      }
    }
    auto tail = trim_copy(input.substr(start)); if (!tail.empty()) result.push_back(tail);
    return result;
  }

  Value compare(const std::string& left_text, const std::string& right_text,
                const std::string& whole, Frame& frame, int line,
                std::ostream& output) {
    std::string op = operator_between(
        whole, left_text, right_text,
        {"==", "!=", "<=", ">=", "<", ">"});
    auto left = eval(left_text, frame, line, output), right = eval(right_text, frame, line, output);
    if (op == "==") return Value::boolean_value(equal(left, right));
    if (op == "!=") return Value::boolean_value(!equal(left, right));
    double l = left.kind == Value::Kind::Int ? left.integer : left.floating;
    double r = right.kind == Value::Kind::Int ? right.integer : right.floating;
    if (op == "<") return Value::boolean_value(l < r);
    if (op == ">") return Value::boolean_value(l > r);
    if (op == "<=") return Value::boolean_value(l <= r);
    return Value::boolean_value(l >= r);
  }

  Value arithmetic(const std::string& left_text, const std::string& right_text,
                   const std::string& whole, Frame& frame, int line,
                   std::ostream& output) {
    std::string op = operator_between(whole, left_text, right_text,
                                      {"+", "-", "*", "/", "%"});
    auto left = eval(left_text, frame, line, output), right = eval(right_text, frame, line, output);
    if (op == "+" && left.kind == Value::Kind::String && right.kind == Value::Kind::String)
      return Value::string_value(left.string + right.string);
    if (left.kind == Value::Kind::Int && right.kind == Value::Kind::Int) {
      if (op == "+") return Value::int_value(wrapping_add(left.integer, right.integer));
      if (op == "-") return Value::int_value(wrapping_sub(left.integer, right.integer));
      if (op == "*") return Value::int_value(wrapping_mul(left.integer, right.integer));
      if (right.integer == 0)
        throw RuntimeError(line, op == "%" ? "integer remainder by zero"
                                                   : "integer division by zero");
      if (left.integer == std::numeric_limits<std::int64_t>::min() && right.integer == -1)
        return Value::int_value(op == "%" ? 0 : std::numeric_limits<std::int64_t>::min());
      if (op == "%") return Value::int_value(left.integer % right.integer);
      return Value::int_value(left.integer / right.integer);
    }
    double l = left.kind == Value::Kind::Int ? left.integer : left.floating;
    double r = right.kind == Value::Kind::Int ? right.integer : right.floating;
    if (op == "+") return Value::float_value(l + r);
    if (op == "-") return Value::float_value(l - r);
    if (op == "*") return Value::float_value(l * r);
    if (op == "%") throw RuntimeError(line, "integer remainder requires Int operands");
    if (r == 0.0) throw RuntimeError(line, "floating-point division by zero");
    return Value::float_value(l / r);
  }

  static bool equal(const Value& left, const Value& right) {
    if (left.kind == Value::Kind::Int && right.kind == Value::Kind::Float)
      return static_cast<double>(left.integer) == right.floating;
    if (left.kind == Value::Kind::Float && right.kind == Value::Kind::Int)
      return left.floating == static_cast<double>(right.integer);
    if (left.kind != right.kind) return false;
    switch (left.kind) {
      case Value::Kind::DomainHandle: return left.domain == right.domain;
      case Value::Kind::Unit: return true;
      case Value::Kind::Bool: return left.boolean == right.boolean;
      case Value::Kind::Int: return left.integer == right.integer;
      case Value::Kind::Float: return left.floating == right.floating;
      case Value::Kind::String: return left.string == right.string;
      case Value::Kind::Struct:
        if (!left.object || !right.object || left.object->type != right.object->type ||
            left.object->fields.size() != right.object->fields.size()) return false;
        for (const auto& entry : left.object->fields) {
          auto other = right.object->fields.find(entry.first);
          if (other == right.object->fields.end() || !equal(entry.second, other->second)) return false;
        }
        return true;
      case Value::Kind::Vector:
        if (!left.vector || !right.vector || left.vector->size() != right.vector->size()) return !left.vector && !right.vector;
        for (size_t i = 0; i < left.vector->size(); ++i) if (!equal((*left.vector)[i], (*right.vector)[i])) return false;
        return true;
      case Value::Kind::Queue:
        if (!left.queue || !right.queue || left.queue->size() != right.queue->size()) return !left.queue && !right.queue;
        for (size_t i = 0; i < left.queue->size(); ++i) if (!equal((*left.queue)[i], (*right.queue)[i])) return false;
        return true;
      case Value::Kind::Map:
        if (!left.map || !right.map || left.map->size() != right.map->size()) return !left.map && !right.map;
        for (const auto& entry : *left.map) {
          bool found = false;
          for (const auto& other : *right.map)
            if (equal(entry.first, other.first) && equal(entry.second, other.second)) { found = true; break; }
          if (!found) return false;
        }
        return true;
    }
    return false;
  }

  Value construct(const ObjectType& object, const std::vector<std::string>& args,
                  Frame& frame, int line, std::ostream& output) {
    Value result = Value::struct_value(object.name);
    for (const auto& field : object.fields) {
      if (!field.init.empty()) result.object->fields[field.name] = eval(field.init, frame, field.line, output);
      else result.object->fields[field.name] = default_semantic_value(field.type, frame, line, output);
    }
    for (const auto& argument : args) {
      auto equal_sign = argument.find_first_of(":=");
      if (equal_sign == std::string::npos) throw RuntimeError(line, "object constructors require named fields");
      auto name = trim_copy(argument.substr(0, equal_sign));
      auto field = result.object->fields.find(name);
      if (field == result.object->fields.end()) throw RuntimeError(line, "unknown field '" + name + "'");
      field->second = eval(argument.substr(equal_sign + 1), frame, line, output);
    }
    return result;
  }

  Value call(const Function& target, const std::vector<std::string>& arguments,
             Frame& caller, int line, std::ostream& output) {
    if (arguments.size() != target.params.size())
      throw RuntimeError(line, "wrong number of arguments for '" + target.name + "'");
    Frame frame;
    frame.function = target.name;
    frame.source_file = target.source_file;
    frame.semantic_identity = "fn:" + target.name + "@" +
        std::to_string(target.line);
    bind_parameters(target.params, target.parameter_effects, arguments, caller, frame, line, output);
    emit("FunctionEnter", frame, target.line);
    Flow flow = execute(target.body, frame, output);
    Value result = flow.returned ? flow.value :
        (target.result_expression ? eval(*target.result_expression, frame, target.result_line, output) : Value::unit());
    if (!flow.returned)
      emit("Return", frame, target.result_line ? target.result_line : target.line,
           result.display());
    emit("FunctionExit", frame, target.result_line ? target.result_line : target.line);
    return result;
  }

  Value call_method(const Method& target, Value receiver,
                    const std::vector<std::string>& arguments, Frame& caller,
                    int line, std::ostream& output, Place origin) {
    if (arguments.size() != target.params.size()) throw RuntimeError(line, "wrong number of method arguments");
    Frame frame;
    frame.function = target.owner + "." + target.name;
    frame.source_file = target.source_file;
    frame.semantic_identity = "method:" + target.owner + "." +
        target.name + "@" + std::to_string(target.line);
    frame.locals["self"] = receiver;
    for (auto& field : receiver.object->fields)
      frame.aliases[field.first] = {&field.second, origin.domain,
          origin.domain ? origin.path + "." + field.first : ""};
    if (origin.domain) frame.aliases["self"] = {&frame.locals.at("self"), origin.domain, origin.path};
    if (target.receiver_effect == Effect::Consume && origin.value) {
      if (origin.domain) state_event("state_consume", caller, line, origin, summary(*origin.value), "<moved>");
      *origin.value = Value::unit();
    }
    bind_parameters(target.params, target.parameter_effects, arguments, caller, frame, line, output);
    emit("MethodEnter", frame, target.line);
    Flow flow = execute(target.body, frame, output);
    Value result = flow.returned ? flow.value :
        (target.result_expression ? eval(*target.result_expression, frame, target.result_line, output) : Value::unit());
    if (!flow.returned)
      emit("Return", frame, target.result_line ? target.result_line : target.line,
           result.display());
    emit("MethodExit", frame, target.result_line ? target.result_line : target.line);
    return result;
  }

  void assign(const std::string& target, Value value, Frame& frame, int line,
              std::ostream& output) {
    auto name = trim_copy(target);
    auto bracket = name.find('[');
    if (bracket != std::string::npos && name.back() == ']') {
      Value receiver = eval(name.substr(0, bracket), frame, line, output);
      if (receiver.kind == Value::Kind::Map && receiver.map) {
        Value key = eval(name.substr(bracket + 1, name.size() - bracket - 2), frame, line, output);
        for (auto& entry : *receiver.map) {
          if (equal(entry.first, key)) { entry.second = std::move(value); return; }
        }
        receiver.map->push_back({std::move(key), std::move(value)});
        return;
      }
    }
    auto place = locate(name, frame, line, output);
    if (place.value) {
      std::string before = summary(*place.value);
      *place.value = std::move(value);
      if (place.domain) state_event("state_write", frame, line, place, before, summary(*place.value));
      else emit("LocalWrite", frame, line, name);
    } else if (identifier(name)) {
      frame.locals[name] = std::move(value); emit("LocalWrite", frame, line, name);
    } else throw RuntimeError(line, "unsupported assignment target '" + name + "'");
  }

  Flow execute(const std::vector<Stmt>& statements, Frame& frame,
               std::ostream& output, size_t begin = 0, size_t end = SIZE_MAX) {
    if (end == SIZE_MAX) end = statements.size();
    size_t index = begin;
    while (index < end) {
      const Stmt& statement = statements[index];
      if (statement.kind == Stmt::Kind::If) {
        size_t body_end = index + 1;
        while (body_end < end && statements[body_end].indent > statement.indent) ++body_end;
        size_t after = body_end, else_end = body_end;
        if (body_end < end && statements[body_end].kind == Stmt::Kind::Else &&
            statements[body_end].indent == statement.indent) {
          else_end = body_end + 1;
          while (else_end < end && statements[else_end].indent > statement.indent) ++else_end;
          after = else_end;
        }
        bool condition = eval(statement.a, frame, statement.line, output).truthy();
        emit("BranchTaken", frame, statement.line, condition ? "then" : "else");
        Flow flow = condition
            ? execute(statements, frame, output, index + 1, body_end)
            : (after != body_end ? execute(statements, frame, output, body_end + 1, else_end) : Flow{});
        if (flow.returned) return flow;
        index = after;
        continue;
      }
      if (statement.kind == Stmt::Kind::Else) { ++index; continue; }
      if (statement.kind == Stmt::Kind::While) {
        size_t body_end = index + 1;
        while (body_end < end && statements[body_end].indent > statement.indent) ++body_end;
        size_t iterations = 0;
        while (eval(statement.a, frame, statement.line, output).truthy()) {
          if (++iterations > 10000000) throw RuntimeError(statement.line, "interpreter loop exceeded safety limit");
          emit("LoopIteration", frame, statement.line, std::to_string(iterations));
          Flow flow = execute(statements, frame, output, index + 1, body_end);
          if (flow.returned) return flow;
        }
        index = body_end;
        continue;
      }
      Flow flow;
      if (compose(statement, frame, output)) { ++index; continue; }
      switch (statement.kind) {
        case Stmt::Kind::Let:
        case Stmt::Kind::Var:
          frame.locals[statement.a] = eval(statement.b, frame, statement.line, output);
          emit("LocalWrite", frame, statement.line, statement.a);
          break;
        case Stmt::Kind::Assign:
          assign(statement.a, eval(statement.b, frame, statement.line, output), frame, statement.line, output);
          break;
        case Stmt::Kind::Echo:
          for (size_t i = 0; i < statement.args.size(); ++i) {
            if (i) output << " ";
            output << eval(statement.args[i], frame, statement.line, output).display();
          }
          output << "\n";
          break;
        case Stmt::Kind::Call:
        case Stmt::Kind::Raw:
          if (!statement.text.empty()) eval(statement.text, frame, statement.line, output);
          break;
        case Stmt::Kind::Return:
          flow.returned = true;
          flow.value = statement.a.empty() ? Value::unit() : eval(statement.a, frame, statement.line, output);
          emit("Return", frame, statement.line, flow.value.display());
          return flow;
        case Stmt::Kind::Message: {
          std::string invocation = statement.a + "." + statement.b + "(";
          for (size_t i = 0; i < statement.args.size(); ++i) { if (i) invocation += ", "; invocation += statement.args[i]; }
          Value result = message(invocation + ")", frame, statement.line, output);
          if (!statement.message_result.empty()) assign(statement.message_result, std::move(result), frame, statement.line, output);
          break;
        }
        case Stmt::Kind::Reply:
          flow.returned = true;
          flow.value = independent(eval(statement.a, frame, statement.line, output));
          emit("reply", frame, statement.line, summary(flow.value));
          return flow;
        case Stmt::Kind::For:
          throw RuntimeError(statement.line, "Fast Debug does not support for iteration yet");
        case Stmt::Kind::If:
        case Stmt::Kind::Else:
        case Stmt::Kind::While:
          break;
      }
      ++index;
    }
    return {};
  }
};

inline FastInterpreter::Value FastInterpreter::Value::unit() { return {}; }
inline FastInterpreter::Value FastInterpreter::Value::boolean_value(bool value) {
  Value result; result.kind = Kind::Bool; result.boolean = value; return result;
}
inline FastInterpreter::Value FastInterpreter::Value::int_value(std::int64_t value) {
  Value result; result.kind = Kind::Int; result.integer = value; return result;
}
inline FastInterpreter::Value FastInterpreter::Value::float_value(double value) {
  Value result; result.kind = Kind::Float; result.floating = value; return result;
}
inline FastInterpreter::Value FastInterpreter::Value::string_value(std::string value) {
  Value result; result.kind = Kind::String; result.string = std::move(value); return result;
}
inline FastInterpreter::Value FastInterpreter::Value::struct_value(std::string type) {
  Value result; result.kind = Kind::Struct;
  result.object = std::make_shared<StructValue>();
  result.object->type = std::move(type);
  return result;
}
inline FastInterpreter::Value FastInterpreter::Value::vector_value(std::vector<Value> values,
                                                                     std::string element_type) {
  Value result; result.kind = Kind::Vector;
  result.vector = std::make_shared<std::vector<Value>>(std::move(values));
  result.element_type = std::move(element_type);
  return result;
}
inline FastInterpreter::Value FastInterpreter::Value::queue_value() {
  Value result; result.kind = Kind::Queue;
  result.queue = std::make_shared<std::vector<Value>>();
  return result;
}
inline FastInterpreter::Value FastInterpreter::Value::map_value() {
  Value result; result.kind = Kind::Map;
  result.map = std::make_shared<std::vector<std::pair<Value, Value>>>();
  return result;
}
inline bool FastInterpreter::Value::truthy() const {
  if (kind == Kind::Bool) return boolean;
  if (kind == Kind::Int) return integer != 0;
  if (kind == Kind::Float) return floating != 0.0;
  if (kind == Kind::String) return !string.empty();
  if (kind == Kind::Vector) return vector && !vector->empty();
  if (kind == Kind::Queue) return queue && !queue->empty();
  if (kind == Kind::Map) return map && !map->empty();
  return kind != Kind::Unit;
}
inline std::string FastInterpreter::Value::display() const {
  std::ostringstream out;
  switch (kind) {
    case Kind::DomainHandle: return domain ? domain->concrete->identity : "<domain>";
    case Kind::Unit: return "()";
    case Kind::Bool: return boolean ? "true" : "false";
    case Kind::Int: return std::to_string(integer);
    case Kind::Float: out << std::setprecision(15) << floating; return out.str();
    case Kind::String: return string;
    case Kind::Struct: return object ? object->type : "<struct>";
    case Kind::Vector:
      out << "[";
      if (vector) for (size_t i = 0; i < vector->size(); ++i) {
        if (i) out << ", ";
        out << (*vector)[i].display();
      }
      out << "]";
      return out.str();
    case Kind::Queue:
      out << "Queue(" << (queue ? std::to_string(queue->size()) : "0") << ")";
      return out.str();
    case Kind::Map:
      out << "Map(" << (map ? std::to_string(map->size()) : "0") << ")";
      return out.str();
  }
  return "()";
}

}  // namespace moss
