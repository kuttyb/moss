#!/bin/sh
# SUMT shadow validation and regression, from the repository root.
#
#   tools/sumt/run.sh <base-moss> <out-dir>
#
#   <base-moss>  an unmodified build of the base commit (e.g. 8c363fee), for parity
#   <out-dir>    results; reruns overwrite
#   JOBS=N       parallel jobs (default: nproc)
#   SWEEP=all    validate every .moss under tests/ examples/ benchmarks/
#                (default: top-level tests/ and examples/, tools/sumt/targets.txt,
#                tools/sumt/fixtures/)
#
# Steps: (1) shadow + validator per file with generic functions, summarized by
# summarize.py; (2) `moss check` parity vs <base-moss>, shadow off and on;
# (3) top-level check + emitted-Rust byte identity; (4) the hold2 test groups.
set -u
BASE=$1; OUT=$2; JOBS=${JOBS:-$(nproc)}; SWEEP=${SWEEP:-bounded}
MOSS=./moss
mkdir -p "$OUT/val"; rm -f "$OUT"/val/*.jsonl
if [ "$SWEEP" = all ]; then
  find tests examples benchmarks -name '*.moss' -not -path '*/build/*' | sort > "$OUT/files.txt"
else
  { ls tests/*.moss examples/*.moss; cat tools/sumt/targets.txt; ls tools/sumt/fixtures/*.moss; } | sort -u > "$OUT/files.txt"
fi
xargs -P "$JOBS" -n 1 tools/sumt/validate_one.sh "$MOSS" "$OUT" < "$OUT/files.txt"
python3 tools/sumt/summarize.py "$OUT"/val/*.jsonl > "$OUT/summary.txt" 2>&1
echo "== validation (complete_agree / complete_runs; DISAGREE lines and LEDGER in summary.txt)"
grep -v '^LEDGER' "$OUT/summary.txt" | tail -25
echo "== parity, shadow off";  python3 tools/sumt/check_parity.py "$BASE" "$MOSS" --jobs "$JOBS" | tee "$OUT/parity_off.txt" | head -5
echo "== parity, shadow on";   python3 tools/sumt/check_parity.py "$BASE" "$MOSS" --shadow --jobs "$JOBS" | tee "$OUT/parity_on.txt" | head -5
echo "== byte identity (top-level check + emitted Rust)"
python3 tools/sumt/corpus.py "$BASE" "$MOSS" . "$OUT/corpus" > /dev/null 2>&1
python3 -c "import json,sys; r=json.load(open(sys.argv[1])); print(r['files'], 'files,', r['byte_identical'], 'identical', r['differences'])" "$OUT/corpus/corpus_report.json"
echo "== hold2 test groups"
sh tests/shards/shard_negative.sh "$MOSS" "$OUT/shard_negative" > "$OUT/shard_negative.log" 2>&1; echo "shard_negative rc=$?"
sh tests/shards/shard_phase25_int_payloads.sh "$MOSS" "$OUT/shard_p25" > "$OUT/shard_p25.log" 2>&1; echo "shard_phase25_int_payloads rc=$?"
