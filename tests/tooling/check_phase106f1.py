#!/usr/bin/env python3
"""Typed synchronization structure, zero hot-path allocation, and provider ABI.
Timings are deliberately not correctness thresholds.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve(); out.mkdir(parents=True, exist_ok=True)
def run(args, cwd=None):
    p = subprocess.run(list(map(str,args)),cwd=cwd,text=True,capture_output=True,timeout=180)
    assert p.returncode == 0, (args,p.stdout,p.stderr)
    return p.stdout
source = repo/'tests/phase106f1_typed.moss'
rust = out/'typed.rs'
run([compiler,'-O',source,'-o',rust]); text=rust.read_text()
run([compiler,'-O',source,'-o',rust]); assert text==rust.read_text()
plan=json.loads(run([compiler,'inspect','main','--source',source,'--json']))['result']['synchronization_plan']
account=next(d for d in plan['domains'] if d['concrete_instance_id']=='main::account')
for retired in ('MossPhysicalPlan','MossHandlerPlan','MossClassRuntime','MossHandlerFrame','MossSlot','BTreeMap','BTreeSet','leaf_classes','take_exclusive','__restored'):
    assert retired not in text, retired

def block(signature):
    pos=text.index(signature);start=text.index('{',pos);depth=1;i=start+1
    while depth:
        depth += (text[i]=='{')-(text[i]=='}');i+=1
    return text[start+1:i-1]

def field(path): return '__moss_storage_'+''.join(str(len(p))+'_'+p for p in path.split('.'))
for c in account['sync_classes']:
    layout=block('struct AccountClass'+str(c['class_rank'])+' ')
    assert set(re.findall(r'(__moss_storage_\w+):',layout))=={field(p) for p in c['member_leaves']}
assert field('currency') in block('struct AccountImmutable ')
assert text.count('struct AccountRuntime ')==1 # identical layouts reused by two instances
for h in account['handlers']:
    # The first occurrence can be the semantic route trait declaration.
    start=text.index('impl AccountRef {')
    position=text.index('fn '+h['name']+'_shared(&self',start)
    entry=block(text[position:text.index('{',position)])
    expected=[]
    for c in account['sync_classes']:
        mode=h['class_modes'].get(c['class_id'])
        if mode: expected.append((str(c['class_rank']),'write' if mode=='EXCLUSIVE' else 'read',str(c['class_rank'])))
    actual=re.findall(r'let (?:mut )?__class(\d+) = moss_(read|write)_or_abort\(&self.state.class(\d+)\)',entry)
    assert actual==expected,(h['name'],actual,expected)
    assert not re.search(r'\b(for|while)\b|\.find\(|\.get\(|\.remove\(|\.insert\(|vec!|Vec<|Box::|\.clone\(',entry),entry
    assert 'MossAbsent' not in entry # unused scalar state must not materialize
    # Lexical guards survive the complete body, including all nested messages.
    for rank,_,_ in actual:
        assert entry.index('let result =') < entry.index('drop(__class'+rank+')')
constructors='\n'.join(l for l in text[text.index('fn main() {'):].splitlines() if ' = construct_' in l)
rust.write_text(text+'\n#[test] fn distinct_instances() {\n'+constructors+'\nassert!(!std::ptr::eq(&account.state.class0, &other.state.class0));\naccount.Write_shared(); assert_eq!(other.Read_shared(), Some(100));\n}\n')
run(['rustc','--edition=2021','--test','-D','warnings',rust,'-o',out/'instances']);run([out/'instances'])
# Optimized assembly is a second no-allocation check, independent of Rust source
# spelling. Inspect each externally named microcase root and the generated crate
# implementations it may call. Native RwLock cold paths belong to std.
run([sys.executable,repo/'benchmarks/synchronization/typed.py','--compiler',compiler,'--out',out/'asm','--iterations','10','--repeats','1'])
assembly=(out/'asm/typed.s').read_text()
functions={m[1]:m[2] for m in re.finditer(r'^([A-Za-z_][\w.$]*):\n(?=(?:\.Lfunc_begin\d+:\n)?\t\.cfi_startproc)(.*?)(?=^\.Lfunc_end\d+:)',assembly,re.M|re.S)}
for root in ('probe_empty','probe_read','probe_write','probe_two','probe_mixed','probe_three'):
    assert root in functions,root
    pending=[root];seen=set()
    while pending:
        name=pending.pop()
        if name in seen:continue
        seen.add(name);body=functions[name]
        assert not re.search(r'(?:call\w*|jmp)\s+[^\n]*(?:__rust_alloc|malloc|realloc|BTree|HashMap|memcmp|strcmp)',body), (root,name)
        for callee in re.findall(r'(?:call\w*|jmp)\s+([A-Za-z_][\w.$]*)',body):
            if callee in functions:pending.append(callee)
# Provider built without any concrete instance. Consumer later creates two and
# routes through it with source removed. The application generates its layout;
# only semantic body access is imported from the provider rlib.
project=out/'provider'
if project.exists():shutil.rmtree(project)
(project/'src').mkdir(parents=True)
(project/'moss.toml').write_text('[project]\nname="typed_provider"\nversion="0.1.0"\n')
provider=project/'src/provider.moss'
provider.write_text('''module provider
fn increment(n: Int):
  n = n + 1
export domain Counter:
  value: Int
  fn Bump():
    increment(value)
  fn Read() -> Int:
    reply value
''')
main=project/'src/main.moss'
main.write_text('''module app
import provider
fn main():
  echo 1
''')
run([compiler,'build','--json'],cwd=project)
provider.rename(provider.with_suffix('.hidden'))
main.write_text('''module app
import provider
domain Root:
  domainroutes(child: provider.Counter)
  fn Run() -> Int:
    message child.Bump()
    reply message child.Read()
fn main():
  left = provider.Counter(value: 3)
  right = provider.Counter(value: 10)
  root = Root(child: right)
  message left.Bump()
  echo message left.Read(), message root.Run()
''')
built=json.loads(run([compiler,'build','--json'],cwd=project))['result']
assert run([built['artifacts']['executable']]).strip()=='4 11'
provider_rust=(project/'build/debug/provider.rs').read_text()
application_rust=(project/'build/debug/app.rs').read_text()
assert 'struct provider__CounterRuntime' not in provider_rust
assert 'struct provider__CounterRuntime' in application_rust
assert '__moss_body_provider__Counter_Bump' in provider_rust
interface=(project/'build/debug/provider.mossi').read_text()
assert 'native_abi 6' in interface
assert not any(x in interface for x in ('ClassState','RwLock','ClassSet','class_rank','MossRead'))
print('10.6F.1 typed class layouts, exact direct acquisitions, no metadata/heap work, optimized assembly, independent instances, and uninstantiated/source-free provider body ABI passed.')
