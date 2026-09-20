#!/usr/bin/env python3
"""Compiler/plan scaling and deterministic output, with no timing threshold."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import time
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--compiler',default=str(repo/'moss'));p.add_argument('--out',default=str(repo/'tmp/106f/scaling'));p.add_argument('--repeats',type=int,default=5);a=p.parse_args()
out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
rows=[]
for size in (8,32,96):
    source=out/f'stress{size}.moss'
    text='domain Store:\n'+''.join(f'  f{i}: Int\n' for i in range(size))
    text+=''.join(f'  fn W{i}():\n    f{i} = f{i} + 1\n  fn R{i}() -> Int:\n    reply f{i}\n' for i in range(size))
    text+='\ndomain Link:\n  domainroutes(child: Store)\n  fn Get() -> Int:\n    reply message child.R0()\n\nfn main():\n'
    text+=''.join(f'  s{i} = Store()\n  r{i} = Link(child: s{i})\n' for i in range(max(1,size//8)))
    text+='  echo message r0.Get()\n';source.write_text(text)
    # Source domain specializations are unique per instance; increasing routes
    # tests whole-program growth as well as the L x H signature matrix.
    records={};elapsed=[];old_rust=None;old_query=None
    for n in range(a.repeats):
        rust=out/'stress.rs';start=time.perf_counter_ns()
        run=subprocess.run([a.compiler,'-O',str(source),'-o',str(rust)],env=dict(os.environ,MOSS_PROFILE_COMPILER='1'),text=True,capture_output=True,check=True)
        elapsed.append((time.perf_counter_ns()-start)/1e6)
        for line in run.stderr.splitlines():
            if line.startswith('MOSS_PROFILE|'):
                _,stage,ns=line.split('|');records.setdefault(stage,[]).append(int(ns)/1e6)
        generated=rust.read_text();assert old_rust is None or old_rust==generated;old_rust=generated
        query=subprocess.run([a.compiler,'inspect','main','--source',str(source),'--json'],text=True,capture_output=True,check=True).stdout
        assert old_query is None or old_query==query;old_query=query
    plan=json.loads(old_query)['result']['synchronization_plan']
    rows.append(dict(leaves_per_store=size,handlers_per_store=2*size,instances=len(plan['domains']),routes=max(1,size//8),classes=sum(len(d['sync_classes']) for d in plan['domains']),rust_bytes=len(old_rust),total_ms=statistics.median(elapsed),stages_ms={k:statistics.median(v) for k,v in sorted(records.items())}))
(out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n');print(out/'summary.json')
