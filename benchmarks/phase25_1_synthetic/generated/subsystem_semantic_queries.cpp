#include "subsystem_semantic_queries.hpp"

// Implementation for Semantic Queries, Fact Indexing & Agent APIs
namespace synth::semantic_queries {

uint64_t semantic_queries_worker_fn_0(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 0;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 0;
        n.kind = static_cast<NodeKind>((i + 0) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 0;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_1(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 1;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 1;
        n.kind = static_cast<NodeKind>((i + 1) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 1;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_2(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 2;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 2;
        n.kind = static_cast<NodeKind>((i + 2) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 2;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_3(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 3;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 3;
        n.kind = static_cast<NodeKind>((i + 3) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 3;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_4(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 4;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 4;
        n.kind = static_cast<NodeKind>((i + 4) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 4;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_5(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 5;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 5;
        n.kind = static_cast<NodeKind>((i + 5) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 5;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_6(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 6;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 6;
        n.kind = static_cast<NodeKind>((i + 6) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 6;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_7(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 7;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 7;
        n.kind = static_cast<NodeKind>((i + 7) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 7;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_8(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 8;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 8;
        n.kind = static_cast<NodeKind>((i + 8) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 8;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_9(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 9;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 9;
        n.kind = static_cast<NodeKind>((i + 9) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 9;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_10(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 10;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 10;
        n.kind = static_cast<NodeKind>((i + 10) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 10;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_11(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 11;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 11;
        n.kind = static_cast<NodeKind>((i + 11) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 11;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_12(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 12;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 12;
        n.kind = static_cast<NodeKind>((i + 12) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 12;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_13(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 13;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 13;
        n.kind = static_cast<NodeKind>((i + 13) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 13;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_14(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 14;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 14;
        n.kind = static_cast<NodeKind>((i + 14) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 14;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_15(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 15;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 15;
        n.kind = static_cast<NodeKind>((i + 15) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 15;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_16(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 16;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 16;
        n.kind = static_cast<NodeKind>((i + 16) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 16;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_17(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 17;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 17;
        n.kind = static_cast<NodeKind>((i + 17) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 17;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_18(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 18;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 18;
        n.kind = static_cast<NodeKind>((i + 18) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 18;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_19(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 19;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 19;
        n.kind = static_cast<NodeKind>((i + 19) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 19;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_20(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 20;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 20;
        n.kind = static_cast<NodeKind>((i + 20) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 20;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_21(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 21;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 21;
        n.kind = static_cast<NodeKind>((i + 21) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 21;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_22(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 22;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 22;
        n.kind = static_cast<NodeKind>((i + 22) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 22;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_23(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 23;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 23;
        n.kind = static_cast<NodeKind>((i + 23) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 23;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_24(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 24;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 24;
        n.kind = static_cast<NodeKind>((i + 24) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 24;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_25(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 25;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 25;
        n.kind = static_cast<NodeKind>((i + 25) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 25;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_26(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 26;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 26;
        n.kind = static_cast<NodeKind>((i + 26) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 26;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t semantic_queries_worker_fn_27(SubsystemState& state, int depth) {
    state.step_count++;
    uint64_t local_hash = 0xbf58476d1ce4e5b9ULL ^ (static_cast<uint64_t>(depth) << 16) ^ 27;
    std::vector<AstNode> nodes;
    std::vector<AstStmt> stmts;
    std::map<std::string, SymbolFact> local_symbols;
    std::map<int32_t, AstNode> local_id_map;
    std::unordered_map<std::string, TypeRecord> local_types;
    std::set<std::string> names;
    std::vector<ValueObject> values;
    std::vector<GraphEdge> local_edges;
    std::vector<IndexEntry> local_index;
    std::vector<std::pair<int32_t, std::string>> pairs;
    std::map<int32_t, std::vector<std::string>> tag_group_map;
    std::unordered_map<std::string, std::set<int32_t>> scope_id_sets;

    const int iterations = (depth % 4) + 8;
    for (int i = 0; i < iterations; ++i) {
        AstNode n;
        n.id = i * 100 + 27;
        n.kind = static_cast<NodeKind>((i + 27) % 14);
        n.name = "semantic_queries_sym_" + std::to_string(n.id);
        n.int_val = static_cast<int64_t>(i) * 31 + 27;
        n.float_val = static_cast<double>(n.int_val) * 0.125;
        n.child_ids.push_back(i);
        n.child_ids.push_back(i + 1);
        nodes.push_back(n);
        names.insert(n.name);
        pairs.emplace_back(n.id, n.name);
        local_id_map[n.id] = n;
        tag_group_map[i % 5].push_back(n.name);

        SymbolFact sym;
        sym.symbol_id = n.id;
        sym.identifier = n.name;
        sym.scope = "semantic_queries::scope_" + std::to_string(i);
        sym.type = static_cast<TypeKind>((i + 7) % 12);
        sym.is_mutable = (i % 2 == 0);
        sym.is_exported = (i % 3 == 0);
        sym.tags.push_back("tag_" + std::to_string(i));
        sym.attributes["vis"] = "public";
        sym.attributes["phase"] = "25_1b";
        local_symbols[sym.identifier] = sym;
        scope_id_sets[sym.scope].insert(sym.symbol_id);

        TypeRecord tr;
        tr.type_id = i * 10 + 7;
        tr.name = "Type_" + std::to_string(tr.type_id);
        tr.kind = sym.type;
        tr.fields["f0"] = i;
        tr.fields["f1"] = i + 1;
        local_types[tr.name] = tr;

        ValueObject val;
        val.type = tr.kind;
        val.int_data = n.int_val;
        val.str_data = n.name;
        val.list_data.push_back(n.int_val);
        val.map_data[n.name] = n.int_val;
        values.push_back(val);

        GraphEdge edge;
        edge.from_node = i;
        edge.to_node = (i + 1) % iterations;
        edge.weight = i + 5;
        edge.label = "edge_" + std::to_string(i);
        local_edges.push_back(edge);

        IndexEntry ie;
        ie.key = n.name;
        ie.doc_id = i;
        ie.frequency = static_cast<uint32_t>(i + 1);
        ie.positions.push_back(static_cast<uint32_t>(i * 4));
        local_index.push_back(ie);
    }

    int64_t accum_val = 0;
    for (const auto& nd : nodes) {
        accum_val += nd.int_val;
        if (nd.kind == NodeKind::BinaryOp) {
            accum_val = (accum_val * 3) ^ 0x5a5a;
        } else if (nd.kind == NodeKind::UnaryOp) {
            accum_val = ~accum_val;
        } else if (nd.kind == NodeKind::FunctionDecl) {
            accum_val = (accum_val << 2) + nd.id;
        } else if (nd.kind == NodeKind::DomainDecl) {
            accum_val = (accum_val ^ 0x1f1f) * 7;
        } else {
            accum_val ^= nd.id + 17;
        }
    }
    hash_combine(local_hash, static_cast<uint64_t>(accum_val));

    for (size_t j = 0; j < values.size(); ++j) {
        auto& val = values[j];
        val.int_data = (val.int_data * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        val.str_data = std::string("proc_") + std::to_string(j) + "_" + val.str_data;
        if (j % 2 == 0) {
            val.map_data[std::string("k_") + std::to_string(j)] = val.int_data;
            val.list_data.push_back(val.int_data);
        } else {
            val.map_data[std::string("alt_") + std::to_string(j)] = val.int_data ^ 0x3f;
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const AstNode& a, const AstNode& b) {
        if (a.int_val != b.int_val) return a.int_val > b.int_val;
        return a.name < b.name;
    });

    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto found_node = std::find_if(nodes.begin(), nodes.end(), [](const AstNode& item) {
        return item.int_val % 7 == 0;
    });
    if (found_node != nodes.end()) {
        hash_combine(local_hash, static_cast<uint64_t>(found_node->id));
    }

    auto filtered_nodes = filter_and_sort<AstNode>(nodes, [](const AstNode& x) {
        return x.kind != NodeKind::Literal;
    });
    hash_combine(local_hash, filtered_nodes.size());

    size_t matching_syms = count_matching(local_symbols, [](const auto& kv) {
        return kv.second.is_mutable;
    });
    hash_combine(local_hash, matching_syms);

    std::sort(local_edges.begin(), local_edges.end());
    std::sort(local_index.begin(), local_index.end());

    for (const auto& pair : local_symbols) {
        hash_combine(local_hash, static_cast<uint64_t>(pair.second.symbol_id));
        auto it = local_types.find("Type_" + std::to_string(pair.second.symbol_id % 20));
        if (it != local_types.end()) {
            hash_combine(local_hash, static_cast<uint64_t>(it->second.type_id));
        }
        if (local_id_map.find(pair.second.symbol_id) != local_id_map.end()) {
            hash_combine(local_hash, 0x12345678ULL);
        }
    }

    switch (depth % 6) {
        case 0:
            for (auto& v : values) {
                v.int_data += 10;
                state.value_map[v.str_data] = v;
            }
            break;
        case 1:
            for (const auto& ed : local_edges) {
                state.edges.push_back(ed);
                hash_combine(local_hash, static_cast<uint64_t>(ed.weight));
            }
            break;
        case 2:
            for (const auto& idx : local_index) {
                state.index_entries.push_back(idx);
                hash_combine(local_hash, idx.frequency);
            }
            break;
        case 3:
            for (const auto& nd : nodes) {
                state.active_nodes.push_back(nd);
            }
            break;
        case 4:
            for (const auto& kv : local_id_map) {
                state.indexed_ast_map[kv.first] = kv.second;
            }
            break;
        default:
            for (const auto& kv : local_symbols) {
                state.symbol_map[kv.first] = kv.second;
            }
            break;
    }

    hash_combine(state.checksum, local_hash);
    return local_hash;
}

uint64_t run_semantic_queries_pipeline(SubsystemState& state, int workload_size) {
    uint64_t acc = 0;
    acc ^= semantic_queries_worker_fn_0(state, workload_size + 0);
    acc ^= semantic_queries_worker_fn_1(state, workload_size + 1);
    acc ^= semantic_queries_worker_fn_2(state, workload_size + 2);
    acc ^= semantic_queries_worker_fn_3(state, workload_size + 3);
    acc ^= semantic_queries_worker_fn_4(state, workload_size + 4);
    acc ^= semantic_queries_worker_fn_5(state, workload_size + 5);
    acc ^= semantic_queries_worker_fn_6(state, workload_size + 6);
    acc ^= semantic_queries_worker_fn_7(state, workload_size + 7);
    acc ^= semantic_queries_worker_fn_8(state, workload_size + 8);
    acc ^= semantic_queries_worker_fn_9(state, workload_size + 9);
    acc ^= semantic_queries_worker_fn_10(state, workload_size + 10);
    acc ^= semantic_queries_worker_fn_11(state, workload_size + 11);
    acc ^= semantic_queries_worker_fn_12(state, workload_size + 12);
    acc ^= semantic_queries_worker_fn_13(state, workload_size + 13);
    acc ^= semantic_queries_worker_fn_14(state, workload_size + 14);
    acc ^= semantic_queries_worker_fn_15(state, workload_size + 15);
    acc ^= semantic_queries_worker_fn_16(state, workload_size + 16);
    acc ^= semantic_queries_worker_fn_17(state, workload_size + 17);
    acc ^= semantic_queries_worker_fn_18(state, workload_size + 18);
    acc ^= semantic_queries_worker_fn_19(state, workload_size + 19);
    acc ^= semantic_queries_worker_fn_20(state, workload_size + 20);
    acc ^= semantic_queries_worker_fn_21(state, workload_size + 21);
    acc ^= semantic_queries_worker_fn_22(state, workload_size + 22);
    acc ^= semantic_queries_worker_fn_23(state, workload_size + 23);
    acc ^= semantic_queries_worker_fn_24(state, workload_size + 24);
    acc ^= semantic_queries_worker_fn_25(state, workload_size + 25);
    acc ^= semantic_queries_worker_fn_26(state, workload_size + 26);
    acc ^= semantic_queries_worker_fn_27(state, workload_size + 27);
    return acc;
}

} // namespace synth::semantic_queries
