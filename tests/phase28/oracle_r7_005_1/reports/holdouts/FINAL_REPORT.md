# Final reviewer-controlled holdout results

Compiler SHA: `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc`

Overall: **NO-GO: two approved-contract holdouts fail on frozen R7-008**

| Holdout | Expected | Observed | Verdict |
|---|---|---|---|
| primitive Vector loop element owner preservation | ACCEPT; stdout 8\n8\n8\n9 in Fast Debug/native debug/native -O | FAIL at checker: OWNERSHIP_VECTOR_ELEMENT_CONSUME at the loop body push; runtime engines not run because checker rejects | FAIL |
| nonprimitive collection-element control | REJECT OWNERSHIP_COLLECTION_ELEMENT_CONSUME | REJECT OWNERSHIP_COLLECTION_ELEMENT_CONSUME | PASS |
| primitive indexed element/repeated owner use supplementary probe | ACCEPT; stdout 8\n8\n9 in Fast Debug/native debug/native -O | CHECK, Fast Debug, native debug, native -O all PASS with stdout 8\n8\n9 | PASS |
| Map.get write-through | ACCEPT; stdout new\nnew in Fast Debug/native debug/native -O | Checker accepts, but Fast Debug/native debug/native -O each print new\nold | FAIL |
| same generic callable at distinct specializations | ACCEPT; stdout 41\nready in all execution modes | CHECK, Fast Debug, native debug, native -O all PASS with stdout 41\nready | PASS |
| function-name shadowing and restored outer function | ACCEPT; stdout 99\n7\n7\n7 in all execution modes | CHECK, Fast Debug, native debug, native -O all PASS with stdout 99\n7\n7\n7 | PASS |

Expected contracts were recorded before execution in `HOLDOUT_PLAN.md`. Exact source hashes are in `results/final_summary.json`. All source files and generated compiler artifacts are under this repository worktree’s `tmp/phase28-r7-005-1-holdouts/`. No checked-in Team B files were edited.
