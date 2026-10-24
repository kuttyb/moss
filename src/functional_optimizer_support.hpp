#pragma once

#include <initializer_list>
#include <string_view>

#include "functional_ir.hpp"

namespace moss {

TypeRef parsed_type(const std::string& spelling);
bool primitive_term(const TypeRef& type, std::string_view name);
bool applied_builtin(const TypeRef& type,
                     std::initializer_list<std::string_view> names);
bool functional_terminal_kind(FunctionalNodeKind kind);

}
