#!/usr/bin/env python3
"""Opt-in release microbenchmarks; timings never determine test success."""
import argparse
import collections
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess

repo = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--compiler', default=str(repo / 'moss'))
p.add_argument('--out', default=str(repo / 'tmp/106f/bench'))
p.add_argument('--borrow-routes', action='store_true', help='Benchmark-only experiment: borrow private route fields and self')
p.add_argument('--iterations', type=int, default=20000)
p.add_argument('--repeats', type=int, default=7)
p.add_argument('--threads', type=int, default=min(8, len(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else os.cpu_count() or 1))
a = p.parse_args()
out = Path(a.out).resolve(); out.mkdir(parents=True, exist_ok=True)
def run(args, **kw):
    return subprocess.run(list(map(str,args)), check=True, text=True, capture_output=True, **kw).stdout
rust = out / 'workload.rs'
run([a.compiler,'-O',repo / 'benchmarks/synchronization/workload.moss','-o',rust])
text = rust.read_text()
if a.borrow_routes:
    text = re.sub(r"(struct \w+State<'a> \{)(.*?)(\n\})", lambda m: m[1] + re.sub(r": (\w+Ref),", r": &'a \1,", m[2]) + m[3], text, flags=re.S)
    text = re.sub(r"(\w+): self\.\1\.clone\(\),", r"\1: &self.\1,", text)
    text = text.replace('let self_ref = self.clone();', 'let self_ref = self;')
constructors = '\n'.join(line for line in text[text.index('fn main() {'):].splitlines() if ' = construct_' in line)
constructors = constructors.replace('"seed".to_string()', '"x".repeat(size)').replace('vec![7_i64]', 'vec![7_i64; size]')
harness = (repo / 'benchmarks/synchronization/harness.rs').read_text().replace('CONSTRUCTORS',constructors)
rust.write_text(text.replace('fn main() {', 'fn moss_program_main() {', 1) + '\n' + harness.replace('#[test]\nfn measure()', 'fn main()'))
run(['rustc','--edition=2021','-C','opt-level=3','-D','warnings',rust,'-o',out/'measure'])
env = dict(os.environ, MOSS_BENCH_ITERATIONS=str(a.iterations), MOSS_BENCH_THREADS=str(a.threads), MOSS_BENCH_REPEATS=str(a.repeats))
raw = run([out/'measure'],env=env)
(out/'raw.txt').write_text(raw)
rows=collections.defaultdict(list)
for line in raw.splitlines():
    if 'MOSS_PERF|' not in line: continue
    _,name,backend,threads,size,iteration,ns = line[line.index('MOSS_PERF|'):].split('|')
    rows[name,backend,int(threads),int(size)].append(float(ns))
results=[]
for (name,backend,threads,size),values in sorted(rows.items()):
    ordered=sorted(values); median=statistics.median(values)
    results.append(dict(case=name,backend=backend,threads=threads,size=size,median_ns=median,p95_ns=ordered[min(len(ordered)-1,int(.95*len(ordered)))],min_ns=min(values),max_ns=max(values),ops_sec=1e9/median))
report=dict(environment=dict(os=platform.platform(),cpu=next((line.split(':',1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')),'unknown'),affinity=len(os.sched_getaffinity(0)),rust=run(['rustc','--version']).strip(),compiler=str(Path(a.compiler).resolve()),rust_flags='--edition=2021 -C opt-level=3 -D warnings',repeats=a.repeats,max_threads=a.threads),results=results)
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print(out/'summary.json')
