#!/bin/bash
# Print the check / fmt / Fast Debug / native matrix for standalone reproducers.
# usage: matrix.sh file.moss [...]   (scratch projects go under <repo>/tmp/swarm_matrix)
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd); M=$ROOT/moss; G=$ROOT/margo
for f in "$@"; do
  f=$(cd "$(dirname "$f")" && pwd)/$(basename "$f"); n=$(basename "$f" .moss)
  d=$ROOT/tmp/swarm_matrix/$n; rm -rf "$d"; mkdir -p "$d/src"; cp "$f" "$d/src/main.moss"
  printf '[project]\nname = "m-%s"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n' "$(echo "$n" | tr _ -)" > "$d/Moss.toml"
  echo "### $n"
  echo "check : $("$M" check "$d/src/main.moss" --json 2>&1 | python3 -c 'import json,sys
t=sys.stdin.read()
try:
  d=json.loads(t); e=d.get("error") or {}; print("ok" if d["ok"] else e.get("code","?")+": "+e.get("message","")[:120])
except Exception: print("CRASH: "+t.strip().splitlines()[0][:120] if t.strip() else "CRASH")')"
  cp "$f" "$d/fmt.moss"; echo "fmt   : $(cd "$d" && "$M" fmt fmt.moss >/dev/null 2>"$d/fmt.err" && echo ok || head -c 140 "$d/fmt.err" | tr '\n' ' ')"
  echo "interp: $("$M" run --interp "$d/src/main.moss" 2>&1 | tr '\n' ' ' | cut -c1-140)"
  echo "native: $(cd "$d" && if "$G" run >"$d/nat.out" 2>&1; then tr '\n' ' ' < "$d/nat.out" | cut -c1-140; else r=$(grep -oE 'error(\[E[0-9]+\])?: [^\\"]*' "$d/nat.out" | grep -v 'command failed' | head -1); c=$(grep -oE '"code": "[A-Z_]+"' "$d/nat.out" | head -1); echo "$c ${r:0:120}"; fi)"
done
