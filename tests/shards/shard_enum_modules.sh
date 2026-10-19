#!/bin/sh
set -eu

compiler=${1:-./moss}
test_build=${2:-build/tests/enum_modules}
mkdir -p "$test_build"

. "$(dirname "$0")/../lib_test.sh"

# Phase 15.15: Unicode String methods and Map.delete must survive native lowering.
run_case phase15_15_string_map tests/phase15_15_string_map.moss 'string/map ok'
run_case phase15_15_enum_positive tests/phase15_15_enum_positive.moss "$(printf '42\n0\nhelper\nbad\n7\n3\n7\n3\n7\n11\n42\nowned')"
grep -F '"construct_kind": "enum"' "$test_build/phase15_15_enum_positive.mossmap" >/dev/null ||
  fail "enum declaration missing from debug map"
grep -F '"construct_kind": "enum_case"' "$test_build/phase15_15_enum_positive.mossmap" >/dev/null ||
  fail "enum case missing from debug map"
run_case phase15_15_enum_message tests/phase15_15_enum_message.moss "$(printf '3\n3\nmessage\n3')"
[ "$("$compiler" run --interp tests/phase15_15_enum_positive.moss)" = "$(printf '42\n0\nhelper\nbad\n7\n3\n7\n3\n7\n11\n42\nowned')" ] ||
  fail "enum standalone Fast Debug parity"
[ "$("$compiler" run --interp tests/phase15_15_enum_message.moss)" = "$(printf '3\n3\nmessage\n3')" ] ||
  fail "enum message Fast Debug parity"
python3 tests/tooling/check_enum_semantics.py || fail "enum semantic rejections"
enum_module_native=$(cd tests/tooling/fixtures/enum_modules && ../../../../margo run)
enum_module_debug=$(cd tests/tooling/fixtures/enum_modules && ../../../../margo debug)
enum_module_expected=$(printf '4\n1')
[ "$enum_module_native" = "$enum_module_expected" ] ||
  fail "enum module native output"
[ "$enum_module_debug" = "$enum_module_expected" ] ||
  fail "enum module Fast Debug output"
python3 tests/tooling/check_enum_source_free.py ||
  fail "source-free enum module interface"
# EXPRESS-005 review registry: every repaired F case and decided D case is
# an ordinary permanent regression across checking, native, and Fast Debug.
PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/check_phase15_15_express005_followups.py "$compiler" --expect-fixed ||
  fail "EXPRESS-005 follow-up registry"
