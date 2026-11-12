#!/bin/bash
# SUMT consumer smoke test: `lib` publishes core.sumt; `app` checks its calls
# into lib's generics and compares each with lib's summary. Then each program
# in reject/ replaces app's main: the checker must reject it, and the summary
# should predict the rejection.
# Usage: tools/sumt/packages/consume.sh <moss-binary>   (prints on screen)
here="$(cd "$(dirname "$0")" && pwd)"; repo="$here/../../.."
moss="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
work="$(mktemp -d)"; cp -r "$here/lib" "$here/app" "$work/"
cd "$work/app"
echo "== app (accepted calls)"
MOSS_SUMT_CONSUME="$work/consume.jsonl" MOSS="$moss" python3 "$repo/margo" run 2>&1 | grep -v '^  /'
cat "$work/consume.jsonl"
echo "== moss check, MOSS_SUMT_TRUST=1 (bump_first's summary is incomplete: one from its body)"
MOSS_MODULE_PATH="$work/lib/build/debug" MOSS_SUMT_TRUST=1 "$moss" check src/main.moss 2>&1
cp src/main.moss "$work/main.moss.accepted"
for program in "$here"/reject/*.moss; do
  echo "== reject/$(basename "$program")"
  cp "$program" src/main.moss; rm -rf build "$work/consume.jsonl"
  MOSS_SUMT_CONSUME="$work/consume.jsonl" MOSS="$moss" python3 "$repo/margo" build 2>&1 |
    grep -o '"code": "[A-Z_]*"' | head -1
  cat "$work/consume.jsonl"
  plain="$(MOSS_MODULE_PATH="$work/lib/build/debug" "$moss" check src/main.moss 2>&1)"
  trusted="$(MOSS_MODULE_PATH="$work/lib/build/debug" MOSS_SUMT_TRUST=1 "$moss" check src/main.moss 2>&1 | grep -v '^sumt-trust:')"
  [ "$plain" == "$trusted" ] && echo "trusted check: same diagnostics" || echo "trusted check: DIFFERENT diagnostics"
done
rm -rf "$work"
