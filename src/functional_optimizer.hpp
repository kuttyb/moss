#pragma once

#include "ast.hpp"

#include <iosfwd>

namespace moss {

struct OptimizationPlan {
  bool optimizations_enabled = false;
};

// Mutates only the optimizer-owned functional plan on the shared Program.
void optimize_program(Program& program, bool enabled);
void dump_functional_ir(std::ostream& out, const Program& program,
                        bool decisions_only);

}
