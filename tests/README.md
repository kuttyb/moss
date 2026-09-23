# Moss v0.1 regression corpus

Run `make check` from the repository root. Tests exercise one current language:
static composition and `domainroutes`, synchronous `message`, terminating
`reply`, immutable payloads, and checked READ/WRITE/CONSUME effects.

Start with these groups:

- **Synchronous calls and values:** `phase106_sync_smoke.moss`,
  `phase106_reply_control.moss`, `message_unit_result.moss`,
  `payload_reply_record.moss`, and `payload_reply_primitive.moss`.
- **Topology and routing:** `phase106b_topology.moss`,
  `specialized_domain_routes.moss`, `domain_name_collision_routes.moss`,
  `negative/domain_route_cycle.moss`, `negative/self_send_rejected.moss`,
  and `negative/same_domain_handler_message.moss`.
- **Message diagnostics:** `negative/message_unknown_receiver.moss`,
  `negative/message_unknown_handler.moss`, `negative/message_wrong_arity.moss`,
  and `negative/message_argument_type.moss`.
- **Capability closure and ownership:** `tooling/check_domain_handle_closure.py`,
  the alias/consume negatives, and `negative/phase26_payload_*.moss`.
- **Borrowed synchronous payload lowering:** `phase151_borrowed_message_payloads.moss`,
  `phase151_exported_payload.moss`, and
  `tooling/check_phase151_borrowed_payloads.py` prove that internal `_shared`
  calls borrow stable owned/state views while exported bridges retain ownership.
- **Synchronization planning and production 2PL:**
  `tooling/check_synchronization_plan.py`, `tooling/check_handler_2pl.py`,
  and the D.1/E/F/F.1 structure, module, and native execution checks in `run.sh`.
- **Fast Debug and tooling:** interpreter/native differentials, module/project
  closure, structured traces, semantic queries, source maps, and Emacs tests.
- **Agent benchmark:** `tooling/check_agent_benchmark.py` checks the fixed 30-task
  corpus, metadata failures, public list/show JSON, reference solutions, isolated
  execution, and allowed-path enforcement.

Top-level `.moss` fixtures are normally executable positives; `negative/`
contains diagnostic cases. Some specialized suites also keep an explicitly
named failing fixture beside their harness. `run.sh` is authoritative about
the expected outcome. Numbered fixture names identify regression checkpoints,
not alternative source-language modes.

## Source hygiene

`tooling/check_retired_syntax.py --self-test` runs at the start of `make check`.
It scans `.moss` sources under `examples/`, `tests/`, and `benchmarks/`, including
nested projects and comments, while excluding generated build/cache directories.
Only `negative/await_retired.moss` and `negative/spawn_retired.moss` are allowlisted,
each for exactly one intentional migration token. They assert rejection, not
compatibility acceptance. Historical design material belongs in clearly labeled
documentation, not executable examples.
