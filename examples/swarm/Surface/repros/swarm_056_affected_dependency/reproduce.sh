#!/bin/bash
# SWARM-056: dependency implementation change -> `moss test --affected` selects nothing
# while `margo test` fails. Works on a scratch copy under <repo>/tmp; the committed
# sources are not modified.
set -u
ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
SRC=$(cd "$(dirname "$0")" && pwd)
W=$ROOT/tmp/swarm_056; rm -rf "$W"; mkdir -p "$W"; cp -r "$SRC/lib" "$SRC/app" "$W/"
cd "$W/app"
export MOSS_MODULE_PATH=../lib/build/debug
"$ROOT/margo" test | tail -2
echo "--- baseline --affected:"; "$ROOT/moss" test --affected --json | grep -o '"selected": \[[^]]*\]\|"conservative_fallback": [a-z]*'
sed -i 's/return v + v/return v + v + 1/' ../lib/src/calc.moss
(cd ../lib && "$ROOT/margo" build >/dev/null 2>&1)
echo "--- after changing calc.double (lib rebuilt) --affected:"; "$ROOT/moss" test --affected --json | grep -o '"selected": \[[^]]*\]\|"conservative_fallback": [a-z]*\|semantic dependency cone is unchanged' | sort -u
echo "--- margo test:"; "$ROOT/margo" test | tail -2
