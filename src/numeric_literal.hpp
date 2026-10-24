#pragma once

// The single Moss numeric-literal specification (Phase 25 S-A).
//
// scan_numeric_literal() classifies and decodes one numeric literal token. It
// is the only numeric-literal grammar in the compiler:
//   * the lexer (canonicalize_source_literals in moss.cpp) uses it to validate
//     every literal before checking and to rewrite it to a canonical spelling;
//   * Fast Debug literal evaluation uses decode_numeric_literal_expression();
//   * native lowering uses rust_numeric_literal() to emit `16_i64` /
//     `2.5_f64` from the decoded value, never from Moss spelling.
//
// Accepted forms (moss-0.1):
//   decimal Int    digits            1000, 1_000, 007
//   hex Int        0x hexdigits      0x10, 0xff_ff, 0xFF (lowercase `0x` only)
//   decimal Float  digits . digits [exp] | digits . [exp] | . digits [exp]
//                  | digits exp      2.5, 1_0.5, 1., 1.e5, .5, 1e5, 1E-3, 1.5e+10
//                  (exp is e or E, an optional sign, then digits; `1.name` is
//                  member access on the Int 1, not a Float)
// A digit separator `_` must sit between two digits of the same group (integer
// part, fraction, exponent, or hexadecimal body). Int literals denote a
// magnitude in 0 ... 2^63-1; the magnitude 2^63 is admitted only as the
// operand of unary minus (Int minimum). Float literals that round to infinity
// are rejected; underflow rounds to the nearest representable value (possibly
// 0.0), exactly as IEEE binary64 parsing does. Binary/octal prefixes, an
// uppercase `0X` prefix, hexadecimal fractions, and type suffixes such as
// `3i64` or `1.5f32` are rejected.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace moss {

struct NumericLiteral {
  enum class Kind { Int, Float };
  bool ok = false;
  Kind kind = Kind::Int;
  // Int: magnitude, saturated at 2^64-1 when larger. Float: decoded value.
  std::uint64_t magnitude = 0;
  bool magnitude_overflow = false;
  double floating = 0.0;
  size_t end = 0;          // index one past the token in the scanned text
  std::string spelling;    // the token as written
  std::string canonical;   // canonical Moss spelling (decimal; no separators)
  std::string code;        // stable diagnostic code when !ok
  std::string message;     // precise diagnostic message when !ok
};

inline constexpr std::uint64_t kNumericIntMaxMagnitude = 9223372036854775807ULL;
inline constexpr std::uint64_t kNumericIntMinMagnitude = 9223372036854775808ULL;

struct DecodedNumber {
  NumericLiteral::Kind kind = NumericLiteral::Kind::Int;
  std::int64_t integer = 0;
  double floating = 0.0;
};

NumericLiteral scan_numeric_literal(std::string_view text, size_t start);
bool numeric_literal_starts_at(std::string_view line, size_t at);
bool inside_numeric_literal(std::string_view text, size_t pos);
bool int_literal_in_range(const NumericLiteral& lit, bool negated);
std::optional<DecodedNumber> decode_numeric_literal_expression(std::string_view e);
std::string rust_numeric_literal(const DecodedNumber& value);
std::optional<std::string> rust_numeric_literal(std::string_view e);

}  // namespace moss
