#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/phase5_tooling}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

[ -f "$test_build/phase45_shared_traversal_optimized.mossmap" ] || run_phase4_differential phase45_shared_traversal tests/phase45_shared_traversal.moss '6 4 3'
# Phase 5 tooling shares one deterministic compiler-owned provenance map.
tooling_o0_rs="$test_build/phase5_tooling_o0.rs"
tooling_o0_map="$test_build/phase5_tooling_o0.mossmap"
tooling_opt_rs="$test_build/phase5_tooling_opt.rs"
tooling_opt_map="$test_build/phase5_tooling_opt.mossmap"
"$compiler" -O0 tests/phase5_tooling.moss -o "$tooling_o0_rs"
cp "$tooling_o0_map" "$test_build/phase5_tooling_first.mossmap"
"$compiler" -O0 tests/phase5_tooling.moss -o "$tooling_o0_rs"
cmp "$tooling_o0_map" "$test_build/phase5_tooling_first.mossmap" >/dev/null ||
  fail "identical builds produced different Moss debug maps"
"$compiler" -O tests/phase5_tooling.moss -o "$tooling_opt_rs"
rustc -D warnings "$tooling_o0_rs" -o "$test_build/phase5_tooling_o0"
rustc -D warnings "$tooling_opt_rs" -o "$test_build/phase5_tooling_opt"
[ "$("$test_build/phase5_tooling_o0")" = "$("$test_build/phase5_tooling_opt")" ] ||
  fail "Phase 5 fixture differed between -O0 and optimized execution"
for tooling_map in "$tooling_o0_map" "$tooling_opt_map"; do
  grep -F '"format": "moss-debug-map"' "$tooling_map" >/dev/null ||
    fail "tooling map omitted its format contract"
  grep -F '"construct_kind": "function"' "$tooling_map" >/dev/null ||
    fail "tooling map omitted function provenance"
  grep -F '"construct_kind": "method"' "$tooling_map" >/dev/null ||
    fail "tooling map omitted method provenance"
  grep -F '"construct_kind": "handler"' "$tooling_map" >/dev/null ||
    fail "tooling map omitted handler provenance"
  grep -F '"construct_kind": "functional_pipeline"' "$tooling_map" >/dev/null ||
    fail "tooling map omitted functional pipeline provenance"
  grep -F '"semantic_identity": "fn:normalize@7"' "$tooling_map" >/dev/null ||
    fail "tooling map did not retain a deterministic source/provenance identity"
  grep -F '"start_line": 7' "$tooling_map" >/dev/null ||
    fail "tooling map omitted real Moss source lines"
  if grep -Eq 'transient_id|"ir_id"|"pipeline_id"' "$tooling_map"; then
    fail "tooling map exposed a transient compiler IR identity"
  fi
done
grep -F 'fn:summarize<vector[int]>@12:expression:0:stage:1' \
  "$tooling_opt_map" >/dev/null ||
  fail "optimized tooling map lost fused map-stage provenance"
grep -F 'fn:summarize<vector[int]>@12:expression:0:stage:2' \
  "$tooling_opt_map" >/dev/null ||
  fail "optimized tooling map lost fused filter-stage provenance"
grep -F 'fn:summarize<vector[int]>@12:expression:0:stage:3' \
  "$tooling_opt_map" >/dev/null ||
  fail "optimized tooling map lost fused reduction provenance"

tools/moss-build-debug tests/phase5_tooling.moss \
  -o "$test_build/phase5_tooling_debug"
[ "$("$test_build/phase5_tooling_debug")" = "$(printf '6\n7\n5')" ] ||
  fail "Phase 5 debug build changed Moss behavior"
grep -F '"debug_build": true' "$test_build/phase5_tooling_debug.mossmap" >/dev/null ||
  fail "debug map did not identify its debug-oriented build"
if command -v nm >/dev/null 2>&1; then
  nm "$test_build/phase5_tooling_debug" |
    grep -F 'moss__function__normalize__' >/dev/null ||
    fail "debug build omitted the stable native function symbol"
fi

"$compiler" agent bootstrap --json >"$test_build/phase6_bootstrap.first"
"$compiler" agent bootstrap --json >"$test_build/phase6_bootstrap.second"
cmp "$test_build/phase6_bootstrap.first" \
    "$test_build/phase6_bootstrap.second" >/dev/null ||
  fail "Phase 6A agent bootstrap was not deterministic"
grep -F '"protocol_version": 1' \
  "$test_build/phase6_bootstrap.first" >/dev/null ||
  fail "Phase 6A bootstrap omitted the versioned protocol envelope"
"$compiler" check tests/phase6_agent_api.moss --json \
  >"$test_build/phase6_check.json"
grep -F '"command": "check"' "$test_build/phase6_check.json" >/dev/null ||
  fail "Phase 6A structured check command did not execute"

if command -v python3 >/dev/null 2>&1; then
  export PYTHONDONTWRITEBYTECODE=1
  python3 tests/tooling/check_agent_api.py "$compiler"
  python3 tests/tooling/check_project_workflow.py "$compiler" "$test_build"
  python3 tests/tooling/check_multifile_project.py "$compiler"
  python3 tests/tooling/check_phase8_separate_compilation.py "$compiler"
  python3 tests/tooling/check_phase6_ai_native.py "$compiler" "$test_build"
  python3 tests/tooling/check_debug_map.py \
    "$tooling_o0_map" "$tooling_opt_map" \
    "$test_build/phase5_tooling_debug.mossmap" \
    tests/phase5_tooling.moss \
    "$test_build/phase45_shared_traversal_optimized.mossmap" \
    tests/phase45_shared_traversal.moss
  python3 tools/moss_lldb.py resolve \
    "$test_build/phase5_tooling_debug.mossmap" \
    tests/phase5_tooling.moss 8 >"$test_build/phase5_resolve.json"
  grep -F '"generated_line"' "$test_build/phase5_resolve.json" >/dev/null ||
    fail "shared map resolver did not translate a Moss source line"
else
  echo 'skipping optional Moss LLDB/map resolver tests (python3 not found)'
fi

if command -v emacs >/dev/null 2>&1; then
  emacs --batch -Q -L editors/emacs -l moss-mode-tests \
    -f ert-run-tests-batch-and-exit
else
  echo 'skipping optional Emacs moss-mode tests (emacs not found)'
fi

tooling_objdump=""
if command -v llvm-objdump >/dev/null 2>&1; then
  tooling_objdump=$(command -v llvm-objdump)
elif command -v objdump >/dev/null 2>&1; then
  tooling_objdump=$(command -v objdump)
fi
if [ -n "$tooling_objdump" ] && command -v nm >/dev/null 2>&1; then
  tooling_symbol=$(nm "$test_build/phase5_tooling_debug" |
    awk '/moss__function__normalize__/ { print $3; exit }')
  [ -n "$tooling_symbol" ] || fail "could not locate Phase 5 native symbol"
  case "$tooling_objdump" in
    *llvm-objdump)
      "$tooling_objdump" --demangle --source --line-numbers \
        "--disassemble-symbols=$tooling_symbol" \
        "$test_build/phase5_tooling_debug" \
        >"$test_build/phase5_tooling.asm" ;;
    *)
      "$tooling_objdump" --demangle --source --line-numbers \
        "--disassemble=$tooling_symbol" \
        "$test_build/phase5_tooling_debug" \
        >"$test_build/phase5_tooling.asm" ;;
  esac
  grep -F "$tooling_symbol" "$test_build/phase5_tooling.asm" >/dev/null ||
    fail "objdump could not disassemble the mapped Moss function symbol"
  tooling_fused_symbol=$(nm "$test_build/phase5_tooling_opt" |
    awk '/moss__function__summarize__/ { print $3; exit }')
  [ -n "$tooling_fused_symbol" ] ||
    fail "optimized fused pipeline has no mapped native function symbol"
  case "$tooling_objdump" in
    *llvm-objdump)
      "$tooling_objdump" --demangle --source --line-numbers \
        "--disassemble-symbols=$tooling_fused_symbol" \
        "$test_build/phase5_tooling_opt" \
        >"$test_build/phase5_tooling_fused.asm" ;;
    *)
      "$tooling_objdump" --demangle --source --line-numbers \
        "--disassemble=$tooling_fused_symbol" \
        "$test_build/phase5_tooling_opt" \
        >"$test_build/phase5_tooling_fused.asm" ;;
  esac
  grep -F "$tooling_fused_symbol" "$test_build/phase5_tooling_fused.asm" >/dev/null ||
    fail "objdump could not resolve the fused pipeline's mapped symbol"
else
  echo 'skipping optional Moss disassembly test (objdump or nm not found)'
fi

tooling_lldb_dap=""
if command -v lldb-dap >/dev/null 2>&1; then
  tooling_lldb_dap=$(command -v lldb-dap)
else
  tooling_lldb_dap_version=-1
  for tooling_lldb_dap_candidate in /usr/bin/lldb-dap-*; do
    if [ -x "$tooling_lldb_dap_candidate" ]; then
      tooling_lldb_dap_candidate_version=${tooling_lldb_dap_candidate##*-}
      case "$tooling_lldb_dap_candidate_version" in
        ''|*[!0-9]*) continue ;;
      esac
      if [ "$tooling_lldb_dap_candidate_version" -gt "$tooling_lldb_dap_version" ]; then
        tooling_lldb_dap=$tooling_lldb_dap_candidate
        tooling_lldb_dap_version=$tooling_lldb_dap_candidate_version
      fi
    fi
  done
fi

tooling_process_tracing_denied() {
  grep -E \
    'Connection shut down by remote side while waiting for reply to initial handshake packet|Operation not permitted|ptrace' \
    "$@" >/dev/null
}

if command -v lldb >/dev/null 2>&1; then
  if lldb --batch \
    -o "command script import tools/moss_lldb.py" \
    -o "target create $test_build/phase5_tooling_debug" \
    -o "moss-map-load $test_build/phase5_tooling_debug.mossmap" \
    -o "moss-break $(pwd)/tests/phase5_tooling.moss:8" \
    -o run -o "frame variable value" -o moss-where -o moss-stack \
    -o "moss-stack --all" \
    >"$test_build/phase5_lldb.stdout" \
    2>"$test_build/phase5_lldb.stderr"; then
    grep -F 'stop reason = breakpoint' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "LLDB did not stop at the translated Moss breakpoint"
    grep -F 'phase5_tooling.moss:8' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "LLDB did not present the mapped Moss source location"
    grep -F 'value' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "LLDB could not inspect the ordinary Moss function local"
    grep -F '[function] fn:normalize@7' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "moss-where did not translate the selected native frame"
    grep -F '#0 fn:normalize@7 at ' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "moss-stack did not present the mapped Moss frame"
    grep -F 'std::rt::lang_start' "$test_build/phase5_lldb.stdout" >/dev/null ||
      fail "moss-stack --all did not expose runtime helper frames"
  elif tooling_process_tracing_denied \
    "$test_build/phase5_lldb.stdout" "$test_build/phase5_lldb.stderr"; then
    echo 'skipping optional live Moss LLDB CLI test (process tracing unavailable)'
  else
    fail "LLDB is installed but the live Moss command-bridge integration failed"
  fi
else
  echo 'skipping optional Moss LLDB CLI test (lldb not found)'
fi

if command -v python3 >/dev/null 2>&1 && [ -n "$tooling_lldb_dap" ]; then
  if PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_lldb_dap.py \
      "$tooling_lldb_dap" \
      "$test_build/phase5_tooling_debug" \
      "$test_build/phase5_tooling_debug.mossmap" \
      tests/phase5_tooling.moss \
      >"$test_build/phase5_lldb_dap.stdout" \
      2>"$test_build/phase5_lldb_dap.stderr"; then
    grep -F 'lldb-dap Moss debugging passed:' \
      "$test_build/phase5_lldb_dap.stdout" >/dev/null ||
      fail "lldb-dap closeout test did not reach clean process termination"
  elif tooling_process_tracing_denied \
    "$test_build/phase5_lldb_dap.stdout" \
    "$test_build/phase5_lldb_dap.stderr"; then
    echo 'skipping optional live Moss lldb-dap test (process tracing unavailable)'
  else
    fail "lldb-dap is installed but the real Moss DAP integration failed"
  fi
else
  echo 'skipping optional real Moss DAP test (lldb-dap or python3 not found)'
fi

