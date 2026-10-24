#pragma once

#include "ast.hpp"

#include <set>
#include <string>
#include <vector>
#include <cstdint>

namespace moss {

std::string trim(std::string value);
std::string rtrim(std::string value);
bool starts_with(const std::string& value, const std::string& prefix);
bool ends_with(const std::string& value, const std::string& suffix);
std::vector<std::string> split_top_level(const std::string& value, char delimiter);
std::string canonical_type_name(std::string type);
void normalize_standard_error_lines(
    std::vector<Line>& lines,
    const std::set<std::string>& forced_aliases = {});
bool parse_simple_call(const std::string& text, std::string& callee,
                       std::vector<std::string>& arguments);
bool plain_identifier(const std::string& value);
bool parse_member_call(const std::string& text, std::string& receiver,
                       std::string& handler,
                       std::vector<std::string>& arguments);
std::size_t matching_paren(const std::string& text, std::size_t open);
std::size_t top_level_assignment(const std::string& text);
std::size_t top_level_colon(const std::string& text);
std::size_t parse_rust_escape(const std::string& line, std::size_t at,
                              std::uint32_t& codepoint);

}
