# Team C validation-fix verification — `f0d22c13`

**Scoped verdict: PASS** for the WP1 suite routing and the Phase 27 `.mossi` validation fix at exact source SHA `f0d22c1315f70450d23953224932795b433b75cd`. This is not whole Phase 28 approval.

## Exact source and compilers

- Worktree: `tmp/phase28-r7-010-validation-regressions`, exact HEAD `f0d22c1315f70450d23953224932795b433b75cd`.
- `build/compiler/moss-dev` SHA-256: `26f157f25dc619184ae1d3fcaf52065d5e32720e2727276904e1544c0be9243d`.
- `build/compiler/moss-opt` SHA-256: `7d736336eccf2fb3149a6380df1fa8408d38bce9d9a768f1da31431fc0a7950d`.
- Tracked source diff is empty after verification. Tests and code were not edited.

## WP1 routing and Default/Shadow result

The exact WP1 gate passed when invoked through the same filtered parallel runner in both modes:

```sh
python3 tests/run_parallel.py --jobs 1 \
  --filter check_phase25_r2_wp1_call_chain \
  --output-base tmp/team-c-f0d22-validation/default
MOSS_EST=shadow python3 tests/run_parallel.py --jobs 1 \
  --filter check_phase25_r2_wp1_call_chain \
  --output-base tmp/team-c-f0d22-validation/shadow
```

Each selected 1 of 167 jobs and reported **1 PASS / 0 FAIL / 0 SKIP**. Per-job stdout SHA-256 is `a8b4de7c27fe64441aea58df1e9eae2d74185180da2ec885081cb372ef04baf0` in both modes.

`tests/run_parallel.py` includes `check_phase25_r2_wp1_call_chain` in `lightweight`, whose `Job.use_opt_compiler` is false. The runner's default development binary is `build/compiler/moss-dev`; `run_single_job` chooses that compiler and sets the job's `MOSS` path to a launcher that execs it. Thus the registered WP1 corruption gate is routed through moss-dev in both Default and Shadow. The first filtered-runner attempt stopped at its harness preflight because `moss-opt` was absent; after building the required optimized compiler, both filtered runs passed. The WP1 job itself remains on moss-dev.

The registered WP1 script's pass covers source-free imported ABI-v10 Debug/Release and source-backed ABI-v9 Debug/Release controls, poisoned legacy generic bodies, 12 hostile ABI mutations, five checker hooks per mode, six native consumer fact hooks per mode, six Fast Debug fact/body-binding hooks per mode, and the crossed-specialization check where perturbing only `Vector[Int]` leaves the `Vector[Bool]` consumer intact.

## Phase 27 `.mossi` validation-fix result

Command:

```sh
python3 tests/tooling/check_phase27_est_mossi.py build/compiler/moss-dev
```

Result: exit 0, `phase27 .mossi EST passed`; generic Section 1 and Section 2 EST dumps were observed. The checker now selects the exact `est_est_library__generic` local specialized ReturnValue fact and tests both:

- `MOSS_TEST_DROP_LOCAL_SPECIALIZED_RETURN_EST_FACT`
- `MOSS_TEST_PERTURB_LOCAL_SPECIALIZED_RETURN_EST_FACT`

Both must fail with the diagnostic `exact local specialized ReturnValue EST type fact is missing or contradictory`.

The test builds library and app `.mossi` in Default and Shadow, compares raw bytes exactly, and retains the ABI-v9 generic-body and Section 2 checks. For the run's saved package artifacts, library SHA-256 was `2aa6d32962de97cc73614152cd56a234f0bafdb8daffaec720b4f3d9c4285291` and app SHA-256 was `e601bf92b51078b9d7ae2d478ab79fcc97bcbb40593048c406d141fe89068ded`; the test confirmed matching Default/Shadow bytes. An independent `moss-dev debug` execution over the saved provider artifact returned `4\n9` and emitted the imported generic and concrete EST bodies.

## Limit

This verifies only the requested validation-routing and `.mossi` gate checkpoint. It does not certify full suites, Phase 28 overall, or runtime clone provenance.
