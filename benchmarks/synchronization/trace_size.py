#!/usr/bin/env python3
"""Fast Debug usability/trace-size sample, not an interpreter timing gate."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import time
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--compiler',default=str(repo/'moss'));p.add_argument('--out',default=str(repo/'tmp/106f/traces'));a=p.parse_args()
out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
rows=[]
for count in (40,400,4000):
    source=out/f'index{count}.moss';source.write_text((repo/'examples/dogfood_index.moss').read_text().replace('i < 40:',f'i < {count}:'))
    samples=[];digest=None
    for _ in range(5):
        start=time.perf_counter_ns();r=subprocess.run([a.compiler,'run','--interp','--trace',str(source)],text=True,capture_output=True,check=True);samples.append((time.perf_counter_ns()-start)/1e6)
        assert r.stdout==f'{count} {count*(count-1)//2}\n'
        current=hashlib.sha256(r.stderr.encode()).hexdigest();assert digest is None or current==digest;digest=current
    rows.append(dict(messages=count+2,events=len(r.stderr.splitlines()),bytes=len(r.stderr.encode()),median_ms=statistics.median(samples),max_ms=max(samples)))
(out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n');print(out/'summary.json')
