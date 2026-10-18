# Team I source-free harness execution

I-002 replaces the prep harness's source-backed consumer checks with actual
compiled-provider paths. No compiler or ABI/runtime implementation is changed.

Each provider is built by Margo in a fresh in-repository temporary package.
The harness requires real `.mossi` and `.rlib` outputs and exact schema/native
ABI 10, copies native artifacts, verifies no `.moss` source is copied, and
physically deletes the entire provider package. Copied consumer manifests remove
path dependency tables so Margo cannot rebuild providers from source; explicit
`MOSS_MODULE_PATH` roots select only staged compiled artifacts. Every consumer
build/run records its real command, working directory, profile, exit status,
stdout and stderr in the scenario JSON. Artifact byte hashes, source-removal facts,
compiler hash, backend identity, candidate SHA and dirty-checkout status are also
reported. These execution records remain evidence of the command paths actually
reached, including failures; they are not an automatic 62-gate PASS producer.

Scenarios 2, 3 and 5 compile and execute Int/String consumers after provider
removal. Scenario 4 independently compiles ConsumerA and ConsumerB against the
same removed provider, then deletes both consumer package trees and compiles/runs
a third crate that exchanges their provider-owned nominal values. Scenario 6
adds multi-statement generics and nested private helpers before actual source
removal and execution. Scenario 7 requires an exact `METHOD_NOT_FOUND` diagnostic,
nonzero consumer primary and provider-related structured source spans, and the
required method identity. Scenario 8 checks a valid exported enum ownership
fixture; its original struct-match fixture failed with `MATCH_REQUIRES_ENUM`
before reaching the intended ownership test. Metadata mutation checks reject
no-op mutations and require exact structured ABI diagnostic codes. Scenario 11
replaces compiled provider artifacts at the same selected path after removing
both provider trees, then requires the unchanged warmed consumer to reject the
new required `extra` field. A generic cache miss or source-backed layout failure
cannot establish this scenario.

Executable `--all`, `--scenario` and `--cache-test` return nonzero for every
FAIL/BLOCKED/incomplete result. `--self-test` remains an integrity check and
explicitly labels its synthetic exit-code/diagnostic unit probes; it does not
claim any production source-free proof.

At the prep target `fbd2f729caf1a96c3ffe184068e88ba6b09288cc`, the rewritten debug
acceptance run reports **0 PASS, 1 BLOCKED, 10 FAIL**, exit 1. Generic provider
compilation and production ABI-v10 emission are still missing from that target.
The previous run's 1 PASS / 10 BLOCKED and exit 0 is retained in developer feedback
`P25-I-ABI-CROSSCRATE-002`; no acceptance ledger entry is upgraded by this change.

Next: merge the newest approved integration baseline into this branch, rebuild,
and rerun live scenarios in both native profiles. Coordinate exact diagnostic and
private-helper artifact requirements with the sole compiler writer. Missing
compiler facilities remain failed acceptance. Open a draft integration PR and
request L6 review only after required automated checks pass on its latest head.
