#!/usr/bin/env python3
"""Plan diagnostics, deterministic lowering, engine equivalence, and release seeds.
No performance value is a pass/fail threshold.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
repo=Path(__file__).resolve().parents[2]
compiler=Path(sys.argv[1]).resolve()
out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
def run(args, *, cwd=None, expected=0, env=None):
    p=subprocess.run(list(map(str,args)),cwd=cwd,text=True,capture_output=True,env=env,timeout=180)
    assert p.returncode==expected,(args,p.stdout,p.stderr)
    return p

def inspect(source):
    args=[compiler,'inspect','main','--source',source,'--json']
    raw=run(args).stdout;assert run(args).stdout==raw
    result=json.loads(raw)['result']
    for d in result['synchronization_plan']['domains']:
        classes={c['class_id']:c for c in d['sync_classes']};handlers=d['handlers'];m=d['metrics']
        assert m['state_leaf_count']==len(d['state_leaves'])
        assert m['total_handler_pairs']==len(handlers)*(len(handlers)-1)//2
        assert m['conflicting_handler_pairs']+m['non_conflicting_handler_pairs']==m['total_handler_pairs']
        assert m['class_compression_ratio']==(len(classes)/len(d['protected_mutable_leaves']) if d['protected_mutable_leaves'] else None)
        assert m['read_only_handler_count']==sum(not h['write_set'] and not h['consume_set'] for h in handlers)
        assert m['self_conflicting_handler_count']==sum(h['self_conflict'] for h in handlers)
        assert d['handler_order']==[h['handler_identity'] for h in handlers]
        for i,a in enumerate(handlers):
            assert a['class_set_size']==a['shared_acquisitions']+a['exclusive_acquisitions']==len(a['class_set'])
            for j,b in enumerate(handlers):
                witnesses=[c for c in set(a['class_set']) & set(b['class_set']) if 'EXCLUSIVE' in (a['class_modes'][c],b['class_modes'][c])]
                assert d['conflict_matrix'][i][j]==bool(witnesses)
                assert d['conflict_matrix'][i][j]==d['conflict_matrix'][j][i]
        for c in d['class_opportunities']:
            assert c['leaf_count']==len(classes[c['class_id']]['member_leaves'])
            assert c['shared_shared_pairs']==len(c['reader_handlers'])*(len(c['reader_handlers'])-1)//2
            assert 'identical' in c['collapse_reason']
        assert len(d['class_splits'])==len(classes)*(len(classes)-1)//2
        for split in d['class_splits']:
            assert split['left_mode']!=split['right_mode']
            assert classes[split['left']]['mode_signature'][split['handler']]==split['left_mode']
            assert classes[split['right']]['mode_signature'][split['handler']]==split['right_mode']
    assert 'Metrics: state_leaves=' in result['synchronization_dump']
    return result

inspect(repo/'tests/phase106c_account.moss')
inspect(repo/'tests/phase106d_handler_2pl.moss')
inspect(repo/'benchmarks/synchronization/workload.moss')

corpus=['tests/phase106e_domains.moss','tests/phase106d_nested.moss','tests/phase106d_specializations.moss','examples/dogfood_index.moss','benchmarks/synchronization/workload.moss']
trace_stats=[]
for path in corpus:
    source=repo/path;rust=out/(source.stem+'.rs');binary=rust.with_suffix('')
    command=[compiler,'-O',source,'-o',rust]
    run(command);generated=rust.read_text();run(command);assert generated==rust.read_text()
    assert 'let self_ref = self.clone()' not in generated
    assert 'unsafe {' not in generated and 'unsafe impl' not in generated
    # No instrumentation calls/clock reads survive a normal optimized build.
    run(['rustc','--edition=2021','-D','warnings','-C','opt-level=3',rust,'-o',binary])
    production=run([binary]).stdout
    env=dict(os.environ,RUSTC='/no-rustc-for-interpreted-moss')
    interpreted=run([compiler,'run','--interp','--trace',source],env=env)
    assert interpreted.stdout==production,(source,production,interpreted.stdout)
    again=run([compiler,'run','--interp','--trace',source],env=env);assert interpreted.stderr==again.stderr
    trace=[json.loads(line) for line in interpreted.stderr.splitlines()]
    assert all(len(e.get('detail',''))<=256 for e in trace)
    trace_stats.append(dict(source=path,events=len(trace),bytes=len(interpreted.stderr.encode())))
(out/'trace_summary.json').write_text(json.dumps(trace_stats,indent=2)+'\n')

# A clean copy proves normal project workflows, with no ad-hoc library aliases.
project=out/'ledger'
if project.exists():shutil.rmtree(project)
shutil.copytree(repo/'examples/projects/ledger',project,ignore=shutil.ignore_patterns('build','.moss','examples','notes'))
expected='136\n150\n150 2 14\n'
assert run([compiler,'debug','app'],cwd=project,env=dict(os.environ,RUSTC='/no-rustc-for-interpreted-moss')).stdout==expected
built=json.loads(run([compiler,'build','--json'],cwd=project).stdout)
assert built['compiler_version']=='0.1.0'
assert run([built['result']['artifacts']['executable']]).stdout==expected
assert run([compiler,'run','--interp','src/main.moss'],cwd=project).stdout==expected
(project/'src/model.moss').rename(project/'model.hidden')
# Provisioning only Moss-named provider rlibs must also work: reconstruct rustc
# aliases instead of depending on a preceding build's incidental files.
for alias in (project/'build/debug').glob('libmoss_*.rlib'): alias.unlink()
Path(built['result']['artifacts']['executable']).unlink()
built=json.loads(run([compiler,'build','--json'],cwd=project).stdout)
assert run([built['result']['artifacts']['executable']]).stdout==expected
assert 'source' in run([compiler,'debug','app'],cwd=project,expected=1).stderr

# Compile/run physical instrumentation and benchmark structure with tiny work.
# Correctness has no wall-clock acceptance threshold.
run([sys.executable,repo/'benchmarks/synchronization/run.py','--compiler',compiler,'--out',out/'bench-structure','--repeats','1','--threads','1','--iterations','2'])
run([sys.executable,repo/'benchmarks/synchronization/instrument.py','--compiler',compiler,'--out',out/'instrument'])
print('10.6F plan metrics/conflict witnesses/partition explanations, deterministic output, expanded engine equivalence, trace bounds, clean multi-module/source-free seed, and optional instrumentation/benchmark structural checks passed.')
