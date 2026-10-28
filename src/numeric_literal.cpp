#include "numeric_literal.hpp"
#include "ascii.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdlib>

namespace moss {

namespace numeric_literal_detail {
bool ident_char(char c) {
  return ascii_is_alnum(static_cast<unsigned char>(c)) || c == '_';
}
bool hex_digit(char c) { return ascii_is_hex_digit(static_cast<unsigned char>(c)) != 0; }
bool dec_digit(char c) { return ascii_is_digit(static_cast<unsigned char>(c)) != 0; }
// A digit group with separators only between digits: D+(_D+)*.
template <class Digit>
bool valid_group(std::string_view group, Digit digit) {
  if (group.empty() || group.front() == '_' || group.back() == '_') return false;
  for (size_t i = 0; i < group.size(); ++i) {
    if (group[i] == '_') {
      if (group[i + 1] == '_') return false;
      continue;
    }
    if (!digit(group[i])) return false;
  }
  return true;
}
std::string strip(std::string_view group) {
  std::string out;
  for (char c : group) if (c != '_') out.push_back(c);
  return out;
}
bool type_suffix(std::string_view s) {
  if (!s.empty() && s.front() == '_') s.remove_prefix(1);
  static const char* const suffixes[] = {"i8", "i16", "i32", "i64", "i128", "isize",
                                         "u8", "u16", "u32", "u64", "u128", "usize",
                                         "f32", "f64"};
  for (const char* suffix : suffixes) if (s == suffix) return true;
  return false;
}
}  // namespace numeric_literal_detail

// Scan the numeric literal that starts at text[start]. The caller guarantees
// text[start] is a decimal digit, or '.' followed by a decimal digit (a
// leading-dot Float). On success `ok` is true; an Int magnitude above 2^63-1
// is still reported as ok with `magnitude` set so that the caller can apply
// the unary-minus Int-minimum rule (see int_literal_in_range()).
NumericLiteral scan_numeric_literal(std::string_view text, size_t start) {
  using namespace numeric_literal_detail;
  NumericLiteral lit;
  size_t i = start;
  auto fail = [&](size_t end, std::string code, std::string message) {
    lit.ok = false;
    lit.end = end;
    lit.spelling = std::string(text.substr(start, end - start));
    lit.code = std::move(code);
    lit.message = "numeric literal '" + lit.spelling + "' " + message;
    return lit;
  };
  auto ident_end = [&](size_t from) {
    while (from < text.size() && ident_char(text[from])) ++from;
    return from;
  };
  // Radix prefixes.
  if (text[i] == '0' && i + 1 < text.size() &&
      std::string_view("xXbBoO").find(text[i + 1]) != std::string_view::npos) {
    const char prefix = text[i + 1];
    size_t end = ident_end(i + 2);
    if (end < text.size() && text[end] == '.' && end + 1 < text.size() &&
        dec_digit(text[end + 1]))
      end = ident_end(end + 1);
    if (prefix == 'b' || prefix == 'B')
      return fail(end, "UNSUPPORTED_NUMERIC_LITERAL",
                  "uses a binary prefix, which Moss v0.1 does not support; write a decimal "
                  "or lowercase 0x hexadecimal Int literal");
    if (prefix == 'o' || prefix == 'O')
      return fail(end, "UNSUPPORTED_NUMERIC_LITERAL",
                  "uses an octal prefix, which Moss v0.1 does not support; write a decimal "
                  "or lowercase 0x hexadecimal Int literal");
    if (prefix == 'X')
      return fail(end, "INVALID_NUMERIC_LITERAL",
                  "uses an uppercase hexadecimal prefix; write the prefix as lowercase '0x' "
                  "(hexadecimal digits may be either case)");
    std::string_view body = text.substr(i + 2, end - (i + 2));
    if (body.find('.') != std::string_view::npos)
      return fail(end, "UNSUPPORTED_NUMERIC_LITERAL",
                  "is a hexadecimal fraction; hexadecimal literals are Int-only");
    size_t digits_end = 0;
    while (digits_end < body.size() && (hex_digit(body[digits_end]) || body[digits_end] == '_'))
      ++digits_end;
    std::string_view rest = body.substr(digits_end);
    if (!rest.empty()) {
      if (type_suffix(rest))
        return fail(end, "UNSUPPORTED_NUMERIC_LITERAL",
                    "has a type suffix; Moss literals have no suffixes (Int is always 64-bit)");
      return fail(end, "INVALID_NUMERIC_LITERAL",
                  "contains '" + std::string(1, rest.front()) +
                  "', which is not a hexadecimal digit");
    }
    if (body.empty() || strip(body).empty())
      return fail(end, "INVALID_NUMERIC_LITERAL", "has no hexadecimal digits after '0x'");
    if (!valid_group(body, hex_digit))
      return fail(end, "INVALID_DIGIT_SEPARATOR",
                  "misplaces '_'; a digit separator must sit between two digits");
    std::uint64_t value = 0;
    for (char c : strip(body)) {
      const unsigned d = dec_digit(c) ? static_cast<unsigned>(c - '0')
          : static_cast<unsigned>(ascii_to_lower(static_cast<unsigned char>(c)) - 'a' + 10);
      if (value > (UINT64_MAX >> 4)) { lit.magnitude_overflow = true; value = UINT64_MAX; break; }
      value = (value << 4) | d;
    }
    lit.ok = true;
    lit.kind = NumericLiteral::Kind::Int;
    lit.magnitude = value;
    lit.end = end;
    lit.spelling = std::string(text.substr(start, end - start));
    lit.canonical = lit.magnitude_overflow ? lit.spelling : std::to_string(value);
    return lit;
  }
  // Decimal: integer part [. fraction] [exponent].
  auto group_end = [&](size_t from) {
    while (from < text.size() && (dec_digit(text[from]) || text[from] == '_')) ++from;
    return from;
  };
  const size_t int_begin = i;
  i = group_end(i);
  std::string_view int_part = text.substr(int_begin, i - int_begin);
  bool point = false;
  std::string_view frac_part;
  if (i < text.size() && text[i] == '.') {
    const char next = i + 1 < text.size() ? text[i + 1] : '\0';
    size_t underscores = i + 1;
    while (underscores < text.size() && text[underscores] == '_') ++underscores;
    if (dec_digit(next)) {
      point = true;
      size_t frac_begin = i + 1;
      i = group_end(frac_begin);
      frac_part = text.substr(frac_begin, i - frac_begin);
    } else if (next == '_' && underscores < text.size() && dec_digit(text[underscores])) {
      // `1._5`: a separator directly after the decimal point.
      return fail(group_end(i + 1), "INVALID_DIGIT_SEPARATOR",
                  "misplaces '_'; a digit separator must sit between two digits");
    } else if ((next == 'e' || next == 'E') && i + 2 < text.size() &&
               (dec_digit(text[i + 2]) ||
                ((text[i + 2] == '+' || text[i + 2] == '-') && i + 3 < text.size() &&
                 dec_digit(text[i + 3])))) {
      // `1.e5`: a trailing point followed by an exponent.
      point = true;
      ++i;
    } else if (!ident_char(next) && next != '.') {
      // `1.`: a trailing-point Float. `1.name` stays member access on `1`.
      point = true;
      ++i;
    }
  }
  bool exponent = false;
  std::string exp_sign;
  std::string_view exp_part;
  if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
    size_t at = i + 1;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
      exp_sign = std::string(1, text[at]);
      ++at;
    }
    if (at < text.size() && dec_digit(text[at])) {
      exponent = true;
      i = group_end(at);
      exp_part = text.substr(at, i - at);
    } else {
      size_t end = ident_end(i);
      if (!exp_sign.empty() && end == i + 1) end = at;
      return fail(end, "INVALID_NUMERIC_LITERAL",
                  "has an exponent marker without exponent digits");
    }
  }
  // Anything identifier-like glued to the literal is a suffix.
  const size_t end = ident_end(i);
  if (end != i) {
    std::string_view suffix = text.substr(i, end - i);
    if (type_suffix(suffix))
      return fail(end, "UNSUPPORTED_NUMERIC_LITERAL",
                  "has a type suffix; Moss literals have no suffixes (Int and Float are "
                  "always 64-bit)");
    return fail(end, "INVALID_NUMERIC_LITERAL",
                "is followed by '" + std::string(suffix) +
                "'; a numeric literal cannot be joined to a name");
  }
  if ((!int_part.empty() && !valid_group(int_part, dec_digit)) ||
      (point && !frac_part.empty() && !valid_group(frac_part, dec_digit)) ||
      (exponent && !valid_group(exp_part, dec_digit)))
    return fail(end, "INVALID_DIGIT_SEPARATOR",
                "misplaces '_'; a digit separator must sit between two digits");
  lit.end = end;
  lit.spelling = std::string(text.substr(start, end - start));
  if (!point && !exponent) {
    std::uint64_t value = 0;
    for (char c : strip(int_part)) {
      const unsigned d = static_cast<unsigned>(c - '0');
      if (value > (UINT64_MAX - d) / 10) { lit.magnitude_overflow = true; value = UINT64_MAX; break; }
      value = value * 10 + d;
    }
    lit.ok = true;
    lit.kind = NumericLiteral::Kind::Int;
    lit.magnitude = value;
    lit.canonical = lit.magnitude_overflow ? strip(int_part) : std::to_string(value);
    return lit;
  }
  std::string canonical = int_part.empty() ? "0" : strip(int_part);
  if (point) canonical += "." + (frac_part.empty() ? std::string("0") : strip(frac_part));
  if (exponent) canonical += "e" + exp_sign + strip(exp_part);
  errno = 0;
  char* parsed_end = nullptr;
  const double value = std::strtod(canonical.c_str(), &parsed_end);
  if (std::isinf(value))
    return fail(end, "FLOAT_LITERAL_OUT_OF_RANGE",
                "is too large for Float (IEEE binary64; the largest finite value is about "
                "1.7976931348623157e308)");
  lit.ok = true;
  lit.kind = NumericLiteral::Kind::Float;
  lit.floating = value;
  lit.canonical = canonical;
  return lit;
}

// Whether a numeric literal token begins at line[at] (lexer context): a digit,
// or a leading-point Float such as `.5`, not continuing a name or member path.
// `x.5`, `f().5`, and `v[0].5` remain member syntax for the parser to diagnose.
bool numeric_literal_starts_at(std::string_view line, size_t at) {
  using namespace numeric_literal_detail;
  if (at > 0 && (ident_char(line[at - 1]) || line[at - 1] == '.')) return false;
  if (dec_digit(line[at])) return true;
  return line[at] == '.' && at + 1 < line.size() && dec_digit(line[at + 1]) &&
      (at == 0 || std::string_view(")]}'\"").find(line[at - 1]) == std::string_view::npos);
}

// Whether text[pos] (for example the sign in `1e-3`) lies inside a valid
// numeric literal token, so that an operator splitter must not split there.
bool inside_numeric_literal(std::string_view text, size_t pos) {
  using namespace numeric_literal_detail;
  size_t start = pos;
  while (start > 0 && (ident_char(text[start - 1]) || text[start - 1] == '.' ||
                       ((text[start - 1] == '+' || text[start - 1] == '-') && start >= 2 &&
                        (text[start - 2] == 'e' || text[start - 2] == 'E'))))
    --start;
  for (; start < pos; ++start) {
    if (!numeric_literal_starts_at(text, start)) continue;
    NumericLiteral lit = scan_numeric_literal(text, start);
    return lit.ok && lit.end > pos;
  }
  return false;
}

// Whether a successfully scanned Int fits, given whether it is the operand of
// unary minus.
bool int_literal_in_range(const NumericLiteral& lit, bool negated) {
  if (lit.kind != NumericLiteral::Kind::Int) return true;
  if (lit.magnitude_overflow) return false;
  return lit.magnitude <= kNumericIntMaxMagnitude ||
      (negated && lit.magnitude == kNumericIntMinMagnitude);
}

// Decode an expression that is exactly one numeric literal with an optional
// leading '+' or '-' (no space after the sign). Returns nullopt for anything
// else, including invalid or out-of-range literals.
std::optional<DecodedNumber> decode_numeric_literal_expression(std::string_view e) {
  while (!e.empty() && ascii_is_space(static_cast<unsigned char>(e.front()))) e.remove_prefix(1);
  while (!e.empty() && ascii_is_space(static_cast<unsigned char>(e.back()))) e.remove_suffix(1);
  bool negated = false;
  if (!e.empty() && (e.front() == '+' || e.front() == '-')) {
    negated = e.front() == '-';
    e.remove_prefix(1);
  }
  if (e.empty() ||
      !(numeric_literal_detail::dec_digit(e.front()) ||
        (e.front() == '.' && e.size() > 1 && numeric_literal_detail::dec_digit(e[1]))))
    return std::nullopt;
  NumericLiteral lit = scan_numeric_literal(e, 0);
  if (!lit.ok || lit.end != e.size() || !int_literal_in_range(lit, negated)) return std::nullopt;
  DecodedNumber out;
  out.kind = lit.kind;
  if (lit.kind == NumericLiteral::Kind::Int) {
    out.integer = negated ? static_cast<std::int64_t>(0ULL - lit.magnitude)
                          : static_cast<std::int64_t>(lit.magnitude);
  } else {
    out.floating = negated ? -lit.floating : lit.floating;
  }
  return out;
}

// Canonical Rust spelling of a decoded literal value.
std::string rust_numeric_literal(const DecodedNumber& value) {
  if (value.kind == NumericLiteral::Kind::Int) {
    if (value.integer < 0)
      return "-" + std::to_string(0ULL - static_cast<std::uint64_t>(value.integer)) + "_i64";
    return std::to_string(value.integer) + "_i64";
  }
  char buffer[64];
  const double magnitude = std::fabs(value.floating);
  auto result = std::to_chars(buffer, buffer + sizeof buffer, magnitude);
  std::string text(buffer, result.ptr);
  if (text.find_first_of(".e") == std::string::npos) text += ".0";
  return (std::signbit(value.floating) ? "-" : "") + text + "_f64";
}

// Canonical Rust spelling of an expression that is exactly one (optionally
// signed) numeric literal; nullopt otherwise.
std::optional<std::string> rust_numeric_literal(std::string_view e) {
  auto decoded = decode_numeric_literal_expression(e);
  if (!decoded) return std::nullopt;
  return rust_numeric_literal(*decoded);
}


}  // namespace moss
