#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale.h>
#include <string>
#include <string_view>

namespace moss {

enum class NumericParseError { Invalid, Overflow };

inline std::string_view numeric_builtin_base(std::string_view callee) {
  for (std::string_view name : {"parse_int", "parse_float", "to_int"}) {
    if (callee == name ||
        (callee.size() > name.size() + 2 &&
         callee.substr(callee.size() - name.size() - 2) ==
             std::string("__") + std::string(name)))
      return name;
  }
  return {};
}

inline std::string numeric_builtin_error_type(std::string_view callee) {
  const std::string_view base = numeric_builtin_base(callee);
  if (base.empty()) return {};
  return std::string(callee.substr(0, callee.size() - base.size())) +
      (base == "to_int" ? "ConversionError" : "ParseError");
}

inline bool numeric_ascii_space(char c) {
  return c == ' ' || (c >= '\t' && c <= '\r');
}

inline bool numeric_ascii_digit(char c) { return c >= '0' && c <= '9'; }

inline std::string_view numeric_trim(std::string_view text) {
  while (!text.empty() && numeric_ascii_space(text.front())) text.remove_prefix(1);
  while (!text.empty() && numeric_ascii_space(text.back())) text.remove_suffix(1);
  return text;
}

inline bool numeric_digits(std::string_view text, size_t& at,
                           std::string& normalized) {
  if (at == text.size() || !numeric_ascii_digit(text[at])) return false;
  normalized += text[at++];
  while (at < text.size()) {
    if (numeric_ascii_digit(text[at])) normalized += text[at++];
    else if (text[at] == '_' && at + 1 < text.size() &&
             numeric_ascii_digit(text[at + 1])) {
      normalized += text[at + 1];
      at += 2;
    } else break;
  }
  return true;
}

inline bool numeric_decimal(std::string_view input, bool floating,
                            std::string& normalized) {
  std::string_view text = numeric_trim(input);
  size_t at = 0;
  normalized.clear();
  if (at < text.size() && (text[at] == '+' || text[at] == '-'))
    normalized += text[at++];
  bool whole = numeric_digits(text, at, normalized);
  bool fraction = false;
  if (floating && at < text.size() && text[at] == '.') {
    normalized += text[at++];
    fraction = numeric_digits(text, at, normalized);
  }
  if (!whole && !fraction) return false;
  if (floating && at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
    normalized += 'e';
    ++at;
    if (at < text.size() && (text[at] == '+' || text[at] == '-'))
      normalized += text[at++];
    if (!numeric_digits(text, at, normalized)) return false;
  }
  return at == text.size();
}

inline bool numeric_parse_int(std::string_view text, std::int64_t& value,
                              NumericParseError& error) {
  std::string normalized;
  if (!numeric_decimal(text, false, normalized)) {
    error = NumericParseError::Invalid;
    return false;
  }
  std::string_view digits(normalized);
  if (!digits.empty() && digits.front() == '+') digits.remove_prefix(1);
  auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (parsed.ec == std::errc{}) return true;
  error = NumericParseError::Overflow;
  return false;
}

inline bool numeric_parse_float(std::string_view text, double& value,
                                NumericParseError& error) {
  text = numeric_trim(text);
  if (text == "inf") { value = std::numeric_limits<double>::infinity(); return true; }
  if (text == "-inf") { value = -std::numeric_limits<double>::infinity(); return true; }
  if (text == "NaN") { value = std::numeric_limits<double>::quiet_NaN(); return true; }
  std::string normalized;
  if (!numeric_decimal(text, true, normalized)) {
    error = NumericParseError::Invalid;
    return false;
  }
  std::string_view digits(normalized);
  if (!digits.empty() && digits.front() == '+') digits.remove_prefix(1);
  auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(),
                                value, std::chars_format::general);
  if (parsed.ec == std::errc{}) return true;
  if (parsed.ec == std::errc::result_out_of_range) {
    // from_chars reports a range error for both overflow and values that
    // round to zero. A fixed C numeric locale preserves the exact decimal
    // rounding and IEEE overflow/underflow result without process locale.
    static locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
    if (!c_locale) { error = NumericParseError::Invalid; return false; }
    value = strtod_l(normalized.c_str(), nullptr, c_locale);
    return true;
  }
  error = NumericParseError::Invalid;
  return false;
}

inline const char* numeric_runtime_rust() {
  return R"RUST(
fn __moss_numeric_trim(text: &str) -> &str {
    text.trim_matches(|c: char| matches!(c, ' ' | '\t' | '\n' | '\x0b' | '\x0c' | '\r'))
}

fn __moss_numeric_digits(bytes: &[u8], at: &mut usize, output: &mut String) -> bool {
    if *at >= bytes.len() || !bytes[*at].is_ascii_digit() { return false; }
    output.push(bytes[*at] as char);
    *at += 1;
    while *at < bytes.len() && (bytes[*at].is_ascii_digit() ||
          (bytes[*at] == b'_' && *at + 1 < bytes.len() &&
           bytes[*at + 1].is_ascii_digit())) {
        if bytes[*at].is_ascii_digit() {
            output.push(bytes[*at] as char);
            *at += 1;
        } else if bytes[*at] == b'_' && *at + 1 < bytes.len() &&
                  bytes[*at + 1].is_ascii_digit() {
            output.push(bytes[*at + 1] as char);
            *at += 2;
        }
    }
    true
}

fn __moss_numeric_decimal(text: &str, floating: bool) -> Option<String> {
    let bytes = __moss_numeric_trim(text).as_bytes();
    let mut at = 0;
    let mut output = String::new();
    if at < bytes.len() && (bytes[at] == b'+' || bytes[at] == b'-') {
        output.push(bytes[at] as char);
        at += 1;
    }
    let whole = __moss_numeric_digits(bytes, &mut at, &mut output);
    let mut fraction = false;
    if floating && at < bytes.len() && bytes[at] == b'.' {
        output.push('.');
        at += 1;
        fraction = __moss_numeric_digits(bytes, &mut at, &mut output);
    }
    if !whole && !fraction { return None; }
    if floating && at < bytes.len() && (bytes[at] == b'e' || bytes[at] == b'E') {
        output.push('e');
        at += 1;
        if at < bytes.len() && (bytes[at] == b'+' || bytes[at] == b'-') {
            output.push(bytes[at] as char);
            at += 1;
        }
        if !__moss_numeric_digits(bytes, &mut at, &mut output) { return None; }
    }
    if at == bytes.len() { Some(output) } else { None }
}

fn __moss_parse_int(text: &str) -> Result<i64, u8> {
    let normalized = __moss_numeric_decimal(text, false).ok_or(0_u8)?;
    normalized.parse::<i64>().map_err(|_| 1_u8)
}

fn __moss_parse_float(text: &str) -> Result<f64, u8> {
    let token = __moss_numeric_trim(text);
    match token {
        "inf" => return Ok(f64::INFINITY),
        "-inf" => return Ok(f64::NEG_INFINITY),
        "NaN" => return Ok(f64::NAN),
        _ => {}
    }
    let normalized = __moss_numeric_decimal(token, true).ok_or(0_u8)?;
    normalized.parse::<f64>().map_err(|_| 0_u8)
}

fn __moss_to_int(value: f64) -> Result<i64, u8> {
    if !value.is_finite() { return Err(0); }
    let truncated = value.trunc();
    if truncated < -9223372036854775808.0 || truncated >= 9223372036854775808.0 {
        return Err(1);
    }
    Ok(truncated as i64)
}
)RUST";
}

}  // namespace moss
