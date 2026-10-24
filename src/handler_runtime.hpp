#pragma once

namespace moss {
// Production contains typed locks and borrows only. The compiler consumes the
// SynchronizationPlan; no physical plan/leaf/guard maps are emitted.
const char* handler_runtime_rust();
} // namespace moss
