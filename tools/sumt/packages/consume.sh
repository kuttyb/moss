#!/bin/bash
# SUMT across a package boundary, self-checking. `lib` publishes core.sumt;
# `app` depends on it.
#   1. app builds and runs (expected output below).
#   2. Shadow comparison (MOSS_SUMT_CONSUME): every specialization of lib's
#      generics whose summary is complete agrees with the checker.
#   3. Fast path (MOSS_SUMT_TRUST=1 moss check): ok, some specializations from
#      summaries; bump_first's summary is incomplete, so it comes from its body.
#   4. Each reject/*.moss as app's main: the build is rejected, the summary
#      predicted the rejection, and the trusted check prints the same
#      diagnostics as the plain check.
# Usage: tools/sumt/packages/consume.sh <moss-binary>    VERBOSE=1 for raw JSON
# Prints PASS/FAIL per check; exit status is the number of failures (0 = all pass).
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../../.." && pwd)"
moss="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
cp -r "$here/lib" "$here/app" "$work/"
cd "$work/app"
fails=0
pass() { echo "  PASS $*"; }
fail() { echo "  FAIL $*"; fails=$((fails + 1)); }
json_check() {  # json_check <file> <python expression over rows> -> prints detail, exit 0/1
  python3 - "$@" <<'EOF'
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.startswith("{")]
consume = [r for r in rows if r.get("phase") == "consume"]
ok = eval(sys.argv[2])
if not ok:
    for r in consume: print("      ", json.dumps(r)[:240])
sys.exit(0 if ok else 1)
EOF
}

expected=$(printf '6\n3.0\n2\n4\n[2, 4, 6]\n[a!]\n2\n6')
output=$(MOSS_SUMT_CONSUME="$work/consume.jsonl" MOSS="$moss" python3 "$repo/margo" run 2>&1 | grep -v '^  /')
[ "$output" = "$expected" ] && pass "app builds and runs" ||
  { fail "app output"; echo "$output" | sed 's/^/      /'; }
[ -n "${VERBOSE:-}" ] && cat "$work/consume.jsonl"
json_check "$work/consume.jsonl" \
  'len(consume) > 0 and all(r["agree"] for r in consume if r.get("summary_status") == "complete") and any(r.get("summary_status") != "complete" for r in consume)' &&
  pass "shadow comparison: every complete summary agrees with the checker ($(grep -c '"agree":true' "$work/consume.jsonl") agreeing lines)" ||
  fail "shadow comparison"

trust=$(MOSS_MODULE_PATH="$work/lib/build/debug" MOSS_SUMT_TRUST=1 "$moss" check src/main.moss 2>&1)
if echo "$trust" | grep -q ': ok$' &&
   echo "$trust" | grep -Eq '^sumt-trust: [1-9][0-9]* specialization\(s\) from summaries, [1-9][0-9]* from bodies'; then
  pass "fast path: $(echo "$trust" | grep '^sumt-trust:' | cut -c13-)"
else
  fail "fast path"; echo "$trust" | sed 's/^/      /'
fi

for program in "$here"/reject/*.moss; do
  name="reject/$(basename "$program")"
  cp "$program" src/main.moss; rm -rf build "$work/consume.jsonl"
  build=$(MOSS_SUMT_CONSUME="$work/consume.jsonl" MOSS="$moss" python3 "$repo/margo" build 2>&1)
  code=$(echo "$build" | grep -o '"code": "[A-Z_]*"' | head -1 | cut -d'"' -f4)
  [ -n "${VERBOSE:-}" ] && cat "$work/consume.jsonl"
  [ -n "$code" ] || { fail "$name: build was not rejected"; continue; }
  json_check "$work/consume.jsonl" \
    'any(r.get("checked") == "reject" for r in consume) and all(r["agree"] for r in consume if r.get("checked") == "reject")' ||
    { fail "$name: summary did not predict the rejection ($code)"; continue; }
  plain="$(MOSS_MODULE_PATH="$work/lib/build/debug" "$moss" check src/main.moss 2>&1)"
  trusted="$(MOSS_MODULE_PATH="$work/lib/build/debug" MOSS_SUMT_TRUST=1 "$moss" check src/main.moss 2>&1 | grep -v '^sumt-trust:')"
  [ "$plain" = "$trusted" ] || { fail "$name: trusted check printed different diagnostics"; continue; }
  pass "$name: rejected ($code), predicted by the summary, same diagnostics with trust"
done
exit "$fails"
