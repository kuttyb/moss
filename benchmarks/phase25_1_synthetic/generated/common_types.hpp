#pragma once
#include "pch.hpp"

namespace synth {

enum class NodeKind : uint16_t {
    Literal,
    Identifier,
    BinaryOp,
    UnaryOp,
    Block,
    FunctionDecl,
    VarDecl,
    CallExpr,
    IfStmt,
    ReturnStmt,
    LoopStmt,
    MatchStmt
};

enum class TypeKind : uint16_t {
    Unknown,
    Void,
    Int32,
    Int64,
    Float64,
    String,
    Bool,
    List,
    Map,
    Function,
    Custom
};

struct SourceLocation {
    uint32_t line = 0;
    uint32_t column = 0;
    uint32_t file_id = 0;
};

struct AstNode {
    NodeKind kind = NodeKind::Literal;
    int32_t id = 0;
    std::string name;
    int64_t int_val = 0;
    double float_val = 0.0;
    std::string str_val;
    std::vector<int32_t> child_ids;
    SourceLocation loc;

    bool operator==(const AstNode& other) const {
        return id == other.id && kind == other.kind && name == other.name;
    }
    bool operator<(const AstNode& other) const {
        if (id != other.id) return id < other.id;
        return name < other.name;
    }
};

struct AstStmt {
    int32_t stmt_id = 0;
    NodeKind kind = NodeKind::Block;
    std::vector<AstNode> nodes;
    std::map<std::string, int32_t> metadata;

    bool operator<(const AstStmt& other) const {
        return stmt_id < other.stmt_id;
    }
};

struct SymbolFact {
    int32_t symbol_id = 0;
    std::string identifier;
    std::string scope;
    TypeKind type = TypeKind::Unknown;
    bool is_mutable = false;
    bool is_exported = false;
    std::vector<std::string> tags;
    std::map<std::string, std::string> attributes;

    bool operator<(const SymbolFact& other) const {
        if (symbol_id != other.symbol_id) return symbol_id < other.symbol_id;
        return identifier < other.identifier;
    }
};

struct TypeRecord {
    int32_t type_id = 0;
    std::string name;
    TypeKind kind = TypeKind::Unknown;
    std::vector<int32_t> param_types;
    std::unordered_map<std::string, int32_t> fields;
    bool is_concrete = true;

    bool operator==(const TypeRecord& other) const {
        return type_id == other.type_id && name == other.name;
    }
    bool operator<(const TypeRecord& other) const {
        if (type_id != other.type_id) return type_id < other.type_id;
        return name < other.name;
    }
};

struct GraphEdge {
    int32_t from_node = 0;
    int32_t to_node = 0;
    int32_t weight = 1;
    std::string label;

    bool operator<(const GraphEdge& other) const {
        if (from_node != other.from_node) return from_node < other.from_node;
        if (to_node != other.to_node) return to_node < other.to_node;
        return label < other.label;
    }
};

struct IndexEntry {
    std::string key;
    int32_t doc_id = 0;
    uint32_t frequency = 0;
    std::vector<uint32_t> positions;

    bool operator<(const IndexEntry& other) const {
        if (key != other.key) return key < other.key;
        return doc_id < other.doc_id;
    }
};

struct ValueObject {
    TypeKind type = TypeKind::Int32;
    int64_t int_data = 0;
    std::string str_data;
    std::vector<int64_t> list_data;
    std::map<std::string, int64_t> map_data;

    bool operator==(const ValueObject& other) const {
        return type == other.type && int_data == other.int_data && str_data == other.str_data;
    }
};

template<typename T>
std::vector<T> filter_and_sort(const std::vector<T>& input, std::function<bool(const T&)> predicate) {
    std::vector<T> result;
    result.reserve(input.size());
    for (const auto& item : input) {
        if (predicate(item)) {
            result.push_back(item);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

template<typename Container, typename Pred>
size_t count_matching(const Container& c, Pred pred) {
    return std::count_if(c.begin(), c.end(), pred);
}

} // namespace synth
