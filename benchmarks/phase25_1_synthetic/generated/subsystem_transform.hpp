#pragma once
#include "pch.hpp"
#include "common_types.hpp"
#include "eti_declarations.hpp"

namespace synth::transform {

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

uint64_t run_transform_pipeline(SubsystemState& state, int workload_size);
uint64_t transform_worker_fn_0(SubsystemState& state, int depth);
uint64_t transform_worker_fn_1(SubsystemState& state, int depth);
uint64_t transform_worker_fn_2(SubsystemState& state, int depth);
uint64_t transform_worker_fn_3(SubsystemState& state, int depth);
uint64_t transform_worker_fn_4(SubsystemState& state, int depth);
uint64_t transform_worker_fn_5(SubsystemState& state, int depth);
uint64_t transform_worker_fn_6(SubsystemState& state, int depth);
uint64_t transform_worker_fn_7(SubsystemState& state, int depth);
uint64_t transform_worker_fn_8(SubsystemState& state, int depth);
uint64_t transform_worker_fn_9(SubsystemState& state, int depth);
uint64_t transform_worker_fn_10(SubsystemState& state, int depth);
uint64_t transform_worker_fn_11(SubsystemState& state, int depth);
uint64_t transform_worker_fn_12(SubsystemState& state, int depth);
uint64_t transform_worker_fn_13(SubsystemState& state, int depth);
uint64_t transform_worker_fn_14(SubsystemState& state, int depth);
uint64_t transform_worker_fn_15(SubsystemState& state, int depth);
uint64_t transform_worker_fn_16(SubsystemState& state, int depth);
uint64_t transform_worker_fn_17(SubsystemState& state, int depth);
uint64_t transform_worker_fn_18(SubsystemState& state, int depth);
uint64_t transform_worker_fn_19(SubsystemState& state, int depth);
uint64_t transform_worker_fn_20(SubsystemState& state, int depth);
uint64_t transform_worker_fn_21(SubsystemState& state, int depth);
uint64_t transform_worker_fn_22(SubsystemState& state, int depth);
uint64_t transform_worker_fn_23(SubsystemState& state, int depth);
uint64_t transform_worker_fn_24(SubsystemState& state, int depth);
uint64_t transform_worker_fn_25(SubsystemState& state, int depth);
uint64_t transform_worker_fn_26(SubsystemState& state, int depth);
uint64_t transform_worker_fn_27(SubsystemState& state, int depth);
uint64_t transform_worker_fn_28(SubsystemState& state, int depth);
uint64_t transform_worker_fn_29(SubsystemState& state, int depth);
uint64_t transform_worker_fn_30(SubsystemState& state, int depth);
uint64_t transform_worker_fn_31(SubsystemState& state, int depth);
uint64_t transform_worker_fn_32(SubsystemState& state, int depth);
uint64_t transform_worker_fn_33(SubsystemState& state, int depth);
} // namespace synth::transform
