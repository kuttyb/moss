#!/usr/bin/env python3
"""Run every test Moss file standalone on two pinned compiler binaries.

Writes raw structured JSONL to the output directory. Exit 0 only when
verdicts and normalized diagnostic payloads match for each source.
"""
import argparse, concurrent.futures, json, os, pathlib, subprocess, sys

p = argparse.ArgumentParser()
p.add_argument('--candidate', required=True)
p.add_argument('--baseline', required=True)
p.add_argument('--root', required=True)
p.add_argument('--out', required=True)
a = p.parse_args()
root = pathlib.Path(a.root).resolve(); out = pathlib.Path(a.out).resolve(); out.mkdir(parents=True, exist_ok=True)
paths = sorted(x for x in (root/'tests').rglob('*.moss') if 'build' not in x.parts)

def run(binpath, source):
    env = dict(os.environ, MOSS_EST='off')
    r = subprocess.run([binpath, 'check', str(source), '--json'], cwd=root, env=env,
                       text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try: payload = json.loads(r.stdout)
    except Exception: payload = {'decode_error': True, 'stdout': r.stdout, 'stderr': r.stderr}
    payload.pop('source_file', None)
    result = payload.get('result')
    if isinstance(result, dict): result.pop('source_file', None)
    return {'exit': r.returncode, 'payload': payload, 'stderr': r.stderr}

def one(source):
    rel = source.relative_to(root).as_posix()
    return rel, run(a.baseline, source), run(a.candidate, source)

rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    for row in pool.map(one, paths): rows.append(row)
with (out/'corpus_raw.jsonl').open('w') as f:
    for rel, b, c in rows: f.write(json.dumps({'path':rel,'baseline':b,'candidate':c},sort_keys=True)+'\n')

def signature(x):
    payload=x['payload']
    return x['exit'], payload.get('ok'), payload.get('error'), (payload.get('result') or {}).get('diagnostics')
diffs=[]
for rel,b,c in rows:
    if signature(b)!=signature(c): diffs.append({'path':rel,'baseline':signature(b),'candidate':signature(c)})
report={'count':len(rows),'mismatch_count':len(diffs),'mismatches':diffs}
(out/'corpus_summary.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
print(json.dumps({'count':len(rows),'mismatch_count':len(diffs),'mismatches':diffs[:20]},indent=2))
sys.exit(bool(diffs))
