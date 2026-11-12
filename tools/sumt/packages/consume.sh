#!/bin/bash
# SUMT consumer smoke test: `lib` publishes core.sumt; `app` checks its
# specializations of lib's generics and compares each with lib's summary.
# Usage: tools/sumt/packages/consume.sh <moss-binary>   (prints on screen)
set -e
here="$(cd "$(dirname "$0")" && pwd)"; repo="$here/../../.."
moss="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
work="$(mktemp -d)"; cp -r "$here/lib" "$here/app" "$work/"
cd "$work/app"
MOSS_SUMT_CONSUME="$work/consume.jsonl" MOSS="$moss" python3 "$repo/margo" run 2>&1 | grep -v '^  /'
cat "$work/consume.jsonl"
rm -rf "$work"
