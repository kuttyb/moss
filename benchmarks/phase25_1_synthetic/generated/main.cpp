#include "pch.hpp"
#include "common_types.hpp"
#include "subsystem_core_diag.hpp"
#include "subsystem_parser.hpp"
#include "subsystem_abi_sourcefree.hpp"
#include "subsystem_checker_core.hpp"
#include "subsystem_checker_expr.hpp"
#include "subsystem_checker_ownership.hpp"
#include "subsystem_generator.hpp"
#include "subsystem_semantic_queries.hpp"
#include "subsystem_project_engine.hpp"
#include "subsystem_tooling_driver.hpp"

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

    synth::core_diag::SubsystemState s_core_diag;
    uint64_t r_core_diag = synth::core_diag::run_core_diag_pipeline(s_core_diag, workload);
    combine(total_checksum, r_core_diag);
    combine(total_checksum, s_core_diag.checksum);

    synth::parser::SubsystemState s_parser;
    uint64_t r_parser = synth::parser::run_parser_pipeline(s_parser, workload);
    combine(total_checksum, r_parser);
    combine(total_checksum, s_parser.checksum);

    synth::abi_sourcefree::SubsystemState s_abi_sourcefree;
    uint64_t r_abi_sourcefree = synth::abi_sourcefree::run_abi_sourcefree_pipeline(s_abi_sourcefree, workload);
    combine(total_checksum, r_abi_sourcefree);
    combine(total_checksum, s_abi_sourcefree.checksum);

    synth::checker_core::SubsystemState s_checker_core;
    uint64_t r_checker_core = synth::checker_core::run_checker_core_pipeline(s_checker_core, workload);
    combine(total_checksum, r_checker_core);
    combine(total_checksum, s_checker_core.checksum);

    synth::checker_expr::SubsystemState s_checker_expr;
    uint64_t r_checker_expr = synth::checker_expr::run_checker_expr_pipeline(s_checker_expr, workload);
    combine(total_checksum, r_checker_expr);
    combine(total_checksum, s_checker_expr.checksum);

    synth::checker_ownership::SubsystemState s_checker_ownership;
    uint64_t r_checker_ownership = synth::checker_ownership::run_checker_ownership_pipeline(s_checker_ownership, workload);
    combine(total_checksum, r_checker_ownership);
    combine(total_checksum, s_checker_ownership.checksum);

    synth::generator::SubsystemState s_generator;
    uint64_t r_generator = synth::generator::run_generator_pipeline(s_generator, workload);
    combine(total_checksum, r_generator);
    combine(total_checksum, s_generator.checksum);

    synth::semantic_queries::SubsystemState s_semantic_queries;
    uint64_t r_semantic_queries = synth::semantic_queries::run_semantic_queries_pipeline(s_semantic_queries, workload);
    combine(total_checksum, r_semantic_queries);
    combine(total_checksum, s_semantic_queries.checksum);

    synth::project_engine::SubsystemState s_project_engine;
    uint64_t r_project_engine = synth::project_engine::run_project_engine_pipeline(s_project_engine, workload);
    combine(total_checksum, r_project_engine);
    combine(total_checksum, s_project_engine.checksum);

    synth::tooling_driver::SubsystemState s_tooling_driver;
    uint64_t r_tooling_driver = synth::tooling_driver::run_tooling_driver_pipeline(s_tooling_driver, workload);
    combine(total_checksum, r_tooling_driver);
    combine(total_checksum, s_tooling_driver.checksum);


    std::cout << "SYNTHETIC_WORKLOAD_CHECKSUM: " << std::hex << total_checksum << std::dec << std::endl;
    return 0;
}
