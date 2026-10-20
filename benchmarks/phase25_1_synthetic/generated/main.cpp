#include "pch.hpp"
#include "common_types.hpp"
#include "subsystem_parsing.hpp"
#include "subsystem_sema.hpp"
#include "subsystem_types.hpp"
#include "subsystem_graph.hpp"
#include "subsystem_transform.hpp"
#include "subsystem_indexing.hpp"
#include "subsystem_serialize.hpp"
#include "subsystem_runtime.hpp"

int main(int argc, char** argv) {
    int workload = 5;
    if (argc > 1) {
        workload = std::atoi(argv[1]);
        if (workload <= 0) workload = 5;
    }

    uint64_t total_checksum = 0xcbf29ce484222325ULL;
    auto combine = [](uint64_t& h, uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };

    synth::parsing::SubsystemState s_parsing;
    uint64_t r_parsing = synth::parsing::run_parsing_pipeline(s_parsing, workload);
    combine(total_checksum, r_parsing);
    combine(total_checksum, s_parsing.checksum);

    synth::sema::SubsystemState s_sema;
    uint64_t r_sema = synth::sema::run_sema_pipeline(s_sema, workload);
    combine(total_checksum, r_sema);
    combine(total_checksum, s_sema.checksum);

    synth::types::SubsystemState s_types;
    uint64_t r_types = synth::types::run_types_pipeline(s_types, workload);
    combine(total_checksum, r_types);
    combine(total_checksum, s_types.checksum);

    synth::graph::SubsystemState s_graph;
    uint64_t r_graph = synth::graph::run_graph_pipeline(s_graph, workload);
    combine(total_checksum, r_graph);
    combine(total_checksum, s_graph.checksum);

    synth::transform::SubsystemState s_transform;
    uint64_t r_transform = synth::transform::run_transform_pipeline(s_transform, workload);
    combine(total_checksum, r_transform);
    combine(total_checksum, s_transform.checksum);

    synth::indexing::SubsystemState s_indexing;
    uint64_t r_indexing = synth::indexing::run_indexing_pipeline(s_indexing, workload);
    combine(total_checksum, r_indexing);
    combine(total_checksum, s_indexing.checksum);

    synth::serialize::SubsystemState s_serialize;
    uint64_t r_serialize = synth::serialize::run_serialize_pipeline(s_serialize, workload);
    combine(total_checksum, r_serialize);
    combine(total_checksum, s_serialize.checksum);

    synth::runtime::SubsystemState s_runtime;
    uint64_t r_runtime = synth::runtime::run_runtime_pipeline(s_runtime, workload);
    combine(total_checksum, r_runtime);
    combine(total_checksum, s_runtime.checksum);

    std::cout << "SYNTHETIC_WORKLOAD_CHECKSUM: " << std::hex << total_checksum << std::dec << std::endl;
    return 0;
}
