#! /bin/sh

python3 - <<'EOF'
import json, glob, collections
c = collections.Counter(); real = []
for p in glob.glob('../sumt-out/val/*.jsonl'):
    for l in open(p):
        try: r = json.loads(l)
        except: continue
        if r.get('phase') != 'validate' or r.get('status') != 'complete' or r['agree']: continue
        k = (r['predicted'], r['concrete']); c[k] += 1
        if r['concrete'] in ('accept', 'reject'):
            real.append((p.split('/')[-1][:60], r['function'], r['sigma'], r['predicted'], r['predicted_reason'][:60], r['concrete'], r['concrete_reason'][:60]))
print(dict(c))
for x in real: print(*x, sep=' | ')
EOF
