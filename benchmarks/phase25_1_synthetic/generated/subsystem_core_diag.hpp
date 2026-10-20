#pragma once
#include "pch.hpp"
#include "common_types.hpp"
#include "eti_declarations.hpp"

// Core Infrastructure, StageTimers & Diagnostic Descriptors
namespace synth::core_diag {

struct SubsystemState {
    uint64_t step_count = 0;
    uint64_t checksum = 14695981039346656037ULL;
    std::vector<AstNode> active_nodes;
    std::vector<SymbolFact> active_symbols;
    std::vector<TypeRecord> active_types;
    std::map<std::string, SymbolFact> symbol_map;
    std::map<int32_t, AstNode> indexed_ast_map;
    std::unordered_map<std::string, TypeRecord> type_map;
    std::unordered_map<std::string, ValueObject> value_map;
    std::vector<GraphEdge> edges;
    std::vector<IndexEntry> index_entries;
};

uint64_t run_core_diag_pipeline(SubsystemState& state, int workload_size);
uint64_t core_diag_worker_fn_0(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_1(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_2(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_3(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_4(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_5(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_6(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_7(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_8(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_9(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_10(SubsystemState& state, int depth);
uint64_t core_diag_worker_fn_11(SubsystemState& state, int depth);
} // namespace synth::core_diag
