#!/usr/bin/env python3
"""Opt-in physical lock events and timing; no production trace or timing gate."""
import argparse
import json
from pathlib import Path
import subprocess
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--compiler',default=str(repo/'moss'));p.add_argument('--out',default=str(repo/'tmp/106f/instrument'));a=p.parse_args()
out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
def run(args): return subprocess.run(list(map(str,args)),check=True,text=True,capture_output=True).stdout
rust=out/'instrument.rs';run([a.compiler,'-O',repo/'benchmarks/synchronization/workload.moss','-o',rust]);text=rust.read_text()
constructors='\n'.join(line for line in text[text.index('fn main() {'):].splitlines() if ' = construct_' in line).replace('"seed".to_string()','"x".repeat(size)').replace('vec![7_i64]','vec![7_i64;size]')
rust.write_text(text+'\n'+(repo/'benchmarks/synchronization/harness.rs').read_text().replace('CONSTRUCTORS',constructors)+'\n'+(repo/'benchmarks/synchronization/instrument.rs').read_text())
run(['rustc','--edition=2021','--cfg','moss_perf','--check-cfg','cfg(moss_perf)','--check-cfg','cfg(test)','--test','-C','opt-level=3','-D','warnings',rust,'-o',out/'instrument'])
raw=run([out/'instrument','instrumented','--nocapture','--test-threads=1']);(out/'raw.txt').write_text(raw)
rows={};overlap={}
keys=['calls','shared','exclusive','contended','wait_ns','hold_ns','handler_ns','nested_ns','max_depth']
for line in raw.splitlines():
    if 'MOSS_LOCK|' in line:
        _,label,identity,*values=line[line.index('MOSS_LOCK|'):].split('|');key=(label,identity)
        row=rows.setdefault(key,dict.fromkeys(keys,0))
        for k,v in zip(keys,map(int,values)):
            row[k]=max(row[k],v) if k=='max_depth' else row[k]+v
    elif 'MOSS_OVERLAP|' in line:
        _,label,per_class,all_classes=line[line.index('MOSS_OVERLAP|'):].split('|');overlap[label]=dict(max_same_class=int(per_class),max_all_classes=int(all_classes))
result=dict(events=[dict(workload=label,identity=identity,**row) for (label,identity),row in sorted(rows.items())],overlap=overlap)
assert all(r['wait_ns']>=0 and r['hold_ns']>=0 and r['handler_ns']>=r['nested_ns'] for r in result['events'])
assert overlap['writers']['max_same_class']==1
assert any(r['max_depth']==3 for r in result['events'])
(out/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(out/'summary.json')
