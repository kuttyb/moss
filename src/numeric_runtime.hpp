#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace moss {

enum class NumericParseError { Invalid, Overflow };

std::string_view numeric_builtin_base(std::string_view callee);
std::string numeric_builtin_error_type(std::string_view callee);
bool numeric_ascii_space(char c);
bool numeric_ascii_digit(char c);
std::string_view numeric_trim(std::string_view text);
bool numeric_digits(std::string_view text, size_t& at, std::string& normalized);
bool numeric_decimal(std::string_view input, bool floating,
                     std::string& normalized);
bool numeric_parse_int(std::string_view text, std::int64_t& value,
                       NumericParseError& error);
bool numeric_parse_float(std::string_view text, double& value,
                         NumericParseError& error);
const char* numeric_runtime_rust();

}  // namespace moss
