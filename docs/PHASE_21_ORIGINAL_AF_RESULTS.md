# Phase 21 original external A–F acceptance runs

## Provenance and revision

L7 supplied a single text bundle described as a byte-for-byte transcription of
the README and six `.moss` files in `p21_open_at_7775049.zip`. We recreated
those seven files under `tests/tooling/fixtures/p21_open_at_7775049/` without
editing their contents. The ZIP itself was unavailable, so these hashes verify
the supplied transcription and committed files, not an independently inspected
archive. The separate `p21_review_repros.zip` remains unavailable; its older
cases retain reconstructed provenance in the findings register.

The tested compiler source is implementation
`c1ef3ebb13d7c74c3ed86a45d45e536d527161da`. The worktree HEAD before
this evidence update was `c59ede0c5995397c9509e29e644fac0ab4b577d4`;
the intervening commits changed documentation only. These are engineering
tests, not an independent Architect L6 acceptance vote.

| Supplied file | SHA-256 |
| --- | --- |
| `README.md` | `7800cce4680124f511f8460e639630c37a716a2ef0927592e2a5781e633c2c8e` |
| `A_trailing_method_raise_lost.moss` | `1d3bc3590f3234e7ef8e5d3d72ae8eb7a18f3163414cd1c00b5ed150be9c7586` |
| `B_negated_float_echo.moss` | `5b66d7de216cccb3ab036931a49f9f6ee090f0d9eb715753f39dfc3c68e58e01` |
| `C_grouped_bare_reraise.moss` | `31343f8a09160ce7ea5f4ad99d267cc60f3cdf2748bcb0b0148723a27ac01165` |
| `D_chunk_placeholder_mapper_raise.moss` | `e9ca2461592532ea35d578601addfa5325914d82d3367b978f3611fdc0264d0f` |
| `E_chunk_reduce_seed_raise.moss` | `5aa80caba3732d6b86e00db212c81d64ba8b43ab158fdaa92efba0bdc22c197b` |
| `F_sqrt_float_local.moss` | `089bec461549b45c0d192ba8a7627edd4159adb020fc75bd090db7798418cd41` |

No ABI-v9 migration variant was needed. In particular, the original D/E
legacy `FileError` declarations were accepted unchanged and their typed
FileIO effects resolved to canonical `std.FileError` identities.

## Exact command forms and isolated inputs

Commands below were run from the integration worktree unless a case working
directory is specified. Here `R` is the absolute integration worktree path,
`F=$R/tests/tooling/fixtures/p21_open_at_7775049`, and `W=$R/tmp`. Substituting
these variables yields the executed absolute-path commands. Every `moss`
command used `$R/moss`; `rustc` resolved from `PATH`.

```sh
R=/home/owner/daji/code/moss/tmp/moss-worktrees/21-integration
F=$R/tests/tooling/fixtures/p21_open_at_7775049
W=$R/tmp
$R/moss check "$F/CASE.moss" --json
$R/moss effects TARGET --source "$F/CASE.moss" --json
$R/moss "$F/CASE.moss" -O0 -o "$W/OUTPUT_O0.rs"
$R/moss "$F/CASE.moss" -Oshared-memory -o "$W/OUTPUT_O3.rs"
$R/moss run --interp "$F/CASE.moss"
```

For A–C, `OUTPUT` was `phase21-original-abc/{A,B,C}`. Successful B/C
generated Rust was compiled with
`rustc --edition=2021 "$W/phase21-original-abc/CASE_O0.rs" -o
"$W/phase21-original-abc/CASE_O0"` and the O3 equivalent with trailing `-O`.
Those binaries were run from `$W/phase21-original-abc`.

For D–F, `OUTPUT` was `p21_original_def/CASE/CASE` and the per-case working
directory was `$W/p21_original_def/CASE`. The generated Rust was compiled by
`rustc --edition=2021 -Dwarnings -C opt-level=0
"$W/p21_original_def/CASE/CASE_O0.rs" -o
"$W/p21_original_def/CASE/CASE_O0"`; the O3 command used
`-C opt-level=3` and the corresponding `_O3` paths. The binaries and Fast
Debug commands were run from that same per-case directory. D's `p4.bin`
contained exactly `ABABC` (five bytes); E's `p6.bin` contained exactly
`ABCD` (four bytes). The equivalent shell setup is
`printf %s ABABC > "$W/p21_original_def/D/p4.bin"` and
`printf %s ABCD > "$W/p21_original_def/E/p6.bin"`.

The semantic targets were `via_local` for A, `main` for B, `grouped_reraise`
for C, `Worker.A` and `checked` for D, `Worker.Init` and `seed` for E, and
`root` and `main` for F. `main` does not retain a target-local effect summary
in the B/F query API; that is recorded as `not_available`, not as an empty
effect set.

## Results on the unchanged programs

All successful check and effect queries exited 0 and returned JSON `ok:true`.
All successful generator, `rustc`, executable and Fast Debug stages exited 0;
their stderr was empty unless noted. The JSON check output for B, D, E and F
contained no diagnostics.

| Case | `moss check --json` | `moss effects --json` | Native O0 and O3 | Fast Debug | Verdict |
| --- | --- | --- | --- | --- | --- |
| **A · EXT-03** | Exit 1, `MOSS_COMPILE_ERROR` at line 16: `main may not let typed errors escape; handle every raised variant with try/recover`. | The unchanged whole program also exits 1 with that same diagnostic, so its query cannot return `via_local` facts. A separate scratch companion changing only `main` to `try: echo via_local()` / `recover E.A: echo "caught"` checks and reports `raise_set=["enum:E:A"]`, `may_panic=false`, `unresolved=false`. | Both generator modes exit 1 with the same line-16 diagnostic. No Rust file or executable is produced. | Exit 1 with the same diagnostic before execution. | **PASS:** the original unhandled program is rejected; the scoped effect probe confirms the precise variant. |
| **B · EXT-12** | Exit 0, no diagnostics. | Query `main` exits 0, with `effect_summary.status=not_available` for that target. | Both generator, rustc and run stages exit 0. Each prints `-2.0\n-1.0\n-2.0\n-2.0\n-1.0\n-0.0\n-1e16\n`. | Exit 0, same bytes. | **PASS:** exact canonical expression text and native/Debug parity. |
| **C · EXT-17** | Exit 0 with one `RECOVER_UNREACHABLE_VARIANT` warning for `E.B` at line 11. | Exit 0; `grouped_reraise` has exactly `raise_set=["enum:E:A"]`, `may_panic=false`, `unresolved=false`. | Both generator, rustc and run stages exit 0. Each prints `outer A\n`. Generator stderr repeats the warning. | Exit 0, prints `outer A\n`. | **PASS:** grouped bare re-raise preserves the reachable A identity. |
| **D · EXT-10** | Exit 0, no diagnostics. | Exit 0; `Worker.A` raises `enum:FormatError:Bad` plus the six canonical `enum:std__FileError:*` variants; `checked` raises only `enum:FormatError:Bad`. The handler has `may_panic=true` from FileIO. | Both generator, rustc and run stages exit 0. Each prints `-1\n`. | Exit 1 with the documented Phase 20 diagnostic `moss:22: interpreter error: Fast Debug FileIO execution is intentionally unsupported in Phase 20; run this program with the native backend`. | **PASS** on supported native execution; Fast Debug reports its intentional FileIO limit. |
| **E · EXT-10** | Exit 0, no diagnostics. | Exit 0; `Worker.Init` raises `enum:SetupError:Bad` plus the six canonical `enum:std__FileError:*` variants; `seed` raises only `enum:SetupError:Bad`. The handler has `may_panic=true` from FileIO. | Both generator, rustc and run stages exit 0. Each prints `-1\n`, including the unchanged Executor setup and `join()`. | Exit 1 with the documented Phase 20 diagnostic `moss:30: interpreter error: Fast Debug FileIO execution is intentionally unsupported in Phase 20; run this program with the native backend`. | **PASS** on supported native execution; Fast Debug reports its intentional FileIO limit. |
| **F · Float local `sqrt`** | Exit 0, no diagnostics. | Exit 0; `root` has `raise_set=[]`, `may_panic=false`, `unresolved=false`; `main` summary is not retained. | Both generator, rustc and run stages exit 0. Each prints `4.0\n4.0\n1.4142135623730951\n`. | Exit 0, same bytes. | **PASS:** Float locals and helper calls emit a concrete native type. |

For D/E, the canonical FileIO variants observed in order were `Full`, `IO`,
`InUse`, `NotFound`, `NotRegularFile`, and `PermissionDenied`. Their native
`-Dwarnings` builds establish that the original sources now lower to valid
Rust; the exact Root `on_fail` arm produced `-1` on both optimization modes.
This behavioral evidence does not by itself establish the general O1, O4 or
O9 invariant; those arguments remain in `PHASE_21_FINAL_PROOF_AUDIT.md` for
independent Architect assessment.
