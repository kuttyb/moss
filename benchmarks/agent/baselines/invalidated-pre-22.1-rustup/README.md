# Invalidated Phase 22.2C provisional series

This provisional series is not part of the canonical baseline and none of its
metrics are used for Phase 22.2C results. The sanitized agent `HOME` did not
contain rustup configuration, while `rustc` on `PATH` was a rustup shim. Agents
therefore received `BUILD_BACKEND_ERROR` on six Margo invocations across AB024,
AB025, AB026, AB029, and AB030 even though the authoritative host-side validator
could build the same workspaces.

The defect was discovered after all sessions completed. The whole series was
invalidated, the sandbox was corrected to expose a pre-resolved native `rustc`,
and all 30 tasks were rerun in fresh contexts as one replacement series. The
retained protocol and summary document why provisional numbers must not be used.
Raw provisional task workspaces remain disposable and are not checked in.
