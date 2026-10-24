#pragma once

#include "functional_optimizer.hpp"

#include <string>
#include <vector>

namespace moss {

enum class ProgramGenerationMode { Application, Tests, Benchmarks };

std::string generate_native(
    const Program& program, const OptimizationPlan& plan,
    bool debug_build = false,
    ProgramGenerationMode mode = ProgramGenerationMode::Application,
    std::vector<std::string> rust_dependencies = {},
    const Program* resolution_program = nullptr,
    bool project_module_artifact = false,
    bool emit_static_specializations = true,
    bool public_specializations = false);

}
