#!/bin/bash
# SUMT correctness gate on the current tree. No baseline compiler needed.
#
#   tools/sumt/check.sh quick    # about a minute: packages, fixtures (make sumt-smoke)
#   tools/sumt/check.sh full     # every .moss in the repository    (make sumt-check)
#
#   JOBS=N   parallel jobs (default: nproc)
#   OUT=dir  where results go (default: build/sumt-check)
#
# Steps:
#   1. Across a package boundary (tools/sumt/packages/consume.sh): summaries
#      published by one package, compared and trusted by its consumer.
#   2. Shadow neutrality: `moss check` gives identical output with SUMT
#      recording off and on.
#   3. Validation: every summary checked against the real checker for a set of
#      substitutions. Fails on any unknown prediction for a complete summary,
#      or a disagreement not listed in tools/sumt/known_disagreements.txt.
# Prints PASS/FAIL per step; exits non-zero if any step fails.
set -u
cd "$(dirname "$0")/../.."
mode=${1:-full}; JOBS=${JOBS:-$(nproc)}; OUT=${OUT:-build/sumt-check}; MOSS=./moss
case "$mode" in quick|full) ;; *) echo "usage: $0 quick|full"; exit 2;; esac
[ -x "$MOSS" ] || { echo "no ./moss: run make first"; exit 2; }
rm -rf "$OUT"; mkdir -p "$OUT/val"
failed=0
step() { echo; echo "== $*"; }
verdict() { if [ "$1" -eq 0 ]; then echo "PASS $2"; else echo "FAIL $2"; failed=$((failed + 1)); fi; }

if [ "$mode" = full ]; then
  { find tests examples benchmarks -name '*.moss' -not -path '*/build/*'; ls tools/sumt/fixtures/*.moss; } | sort -u > "$OUT/files.txt"
else
  ls tools/sumt/fixtures/*.moss > "$OUT/files.txt"
fi
echo "SUMT check ($mode): $(wc -l < "$OUT/files.txt") files, $JOBS jobs, results in $OUT"

step "1/3 across a package boundary"
tools/sumt/packages/consume.sh "$MOSS"
verdict $? "package summaries"

step "2/3 shadow neutrality (moss check, SUMT recording off vs on)"
python3 tools/sumt/check_parity.py "$MOSS" "$MOSS" --shadow --jobs "$JOBS" --files "$OUT/files.txt" > "$OUT/shadow_parity.txt"
read -r _ checked _ differ < <(head -1 "$OUT/shadow_parity.txt")
echo "  $checked files, $differ differ"
[ "$differ" = 0 ] || sed -n '2,13p' "$OUT/shadow_parity.txt" | sed 's/^/  /'
verdict "$([ "$differ" = 0 ] && echo 0 || echo 1)" "shadow neutrality"

step "3/3 validation against the checker"
total=$(wc -l < "$OUT/files.txt")
( while sleep 30; do echo "  ... $(ls "$OUT/val" | wc -l) result files so far ($total inputs)"; done ) & progress=$!
xargs -P "$JOBS" -n 1 tools/sumt/validate_one.sh "$MOSS" "$OUT" < "$OUT/files.txt"
kill "$progress" 2>/dev/null; wait "$progress" 2>/dev/null
python3 tools/sumt/summarize.py "$OUT"/val/*.jsonl > "$OUT/summary.txt" 2>&1
python3 - "$OUT/summary.txt" tools/sumt/known_disagreements.txt <<'PY'
import json, sys
text = open(sys.argv[1]).read()
known = {l.split()[0] for l in open(sys.argv[2]) if l.strip() and not l.startswith("#")}
totals = json.loads(text[text.index("{"):])
disagree = [l for l in text.splitlines() if l.startswith("DISAGREE ")]
new = [l for l in disagree if l.split()[2] not in known]
print(f"  entries {totals.get('entries', 0)}, complete {totals.get('complete', 0)}, "
      f"validated {totals.get('validated', 0)}")
print(f"  complete runs {totals.get('complete_runs', 0)}: agree {totals.get('complete_agree', 0)}, "
      f"unknown {totals.get('complete_unknown_pred', 0)}; excluded as oracle crashes {totals.get('oracle_crash', 0)}")
print(f"  disagreements {len(disagree)}: known {len(disagree) - len(new)}, new {len(new)}")
for l in new[:20]: print("   ", l[:200])
if len(new) > 20: print(f"    ... {len(new) - 20} more in {sys.argv[1]}")
sys.exit(1 if new or totals.get("complete_unknown_pred", 0) else 0)
PY
verdict $? "validation"

echo
if [ "$failed" -eq 0 ]; then echo "SUMT check ($mode): all passed"; else echo "SUMT check ($mode): $failed step(s) failed (details in $OUT)"; fi
exit "$failed"
