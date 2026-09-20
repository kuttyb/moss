#!/usr/bin/env python3
"""Static typed microcases. No wall-clock correctness gate. Writes optimized ASM."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
repo = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--compiler', default=str(repo/'moss'))
p.add_argument('--out', default=str(repo/'tmp/106f1/typed'))
p.add_argument('--iterations', type=int, default=1000000)
p.add_argument('--repeats', type=int, default=11)
a=p.parse_args(); out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
rust=out/'typed.rs'
subprocess.run([a.compiler,'-O',str(repo/'tests/phase106f1_typed.moss'),'-o',str(rust)],check=True,capture_output=True)
text=rust.read_text()
constructors='\n'.join(line for line in text[text.index('fn main() {'):].splitlines() if ' = construct_' in line and ' account ' in line)
assert constructors
if 'fn moss_read_or_abort<T>' not in text:
    text += "\nfn moss_read_or_abort<T>(l: &std::sync::RwLock<T>) -> std::sync::RwLockReadGuard<'_ ,T> { l.read().unwrap_or_else(|_| std::process::abort()) }\nfn moss_write_or_abort<T>(l: &std::sync::RwLock<T>) -> std::sync::RwLockWriteGuard<'_ ,T> { l.write().unwrap_or_else(|_| std::process::abort()) }\n"
text+='\n'+(Path(__file__).with_suffix('.rs')).read_text().replace('CONSTRUCTORS',constructors)
rust.write_text(text)
# Benchmark entry selected by --test while cfg(test) remains disabled: normal
# production synchronization, as in the original Phase F throughput harness.
text=text.replace('#[test] fn measure_typed()', 'fn measure_typed()')
text=text.replace('fn main() {','fn saved_main() {',1)+'\nfn main() { measure_typed(); }\n'
rust.write_text(text)
subprocess.run(['rustc','--edition=2021','-C','opt-level=3','-D','warnings',str(rust),'-o',str(out/'typed'),'--emit=link,asm'],check=True,capture_output=True)
r=subprocess.run([str(out/'typed')],check=True,capture_output=True,text=True,env=dict(os.environ,MOSS_TYPED_ITERATIONS=str(a.iterations),MOSS_TYPED_REPEATS=str(a.repeats)))
(out/'raw.txt').write_text(r.stdout)
values={}
for line in r.stdout.splitlines():
    if line.startswith('TYPED|'):
        _,case,backend,rep,ns=line.split('|');values.setdefault(case,{}).setdefault(backend,[]).append(float(ns))
rows=[]
for case,backends in values.items():
    row={'case':case}
    for backend,samples in backends.items():
        ordered=sorted(samples);row[backend]={'median_ns':statistics.median(samples),'min_ns':ordered[0],'max_ns':ordered[-1]}
    row['delta_ns']=row['moss']['median_ns']-row['rust']['median_ns']
    row['ratio']=row['moss']['median_ns']/row['rust']['median_ns']
    rows.append(row)
(out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
print(out/'summary.json')
