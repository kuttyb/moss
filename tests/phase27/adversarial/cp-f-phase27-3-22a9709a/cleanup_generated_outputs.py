import json,pathlib,subprocess,sys
root=pathlib.Path(sys.argv[1]).resolve(); own=pathlib.Path(__file__).resolve().parent
report=json.loads((own/'corpus_differential.json').read_text())
paths=set()
for row in report.get('rows',[]):
 for key in ('baseline','candidate_off','candidate_shadow'):
  data=row.get(key,{})
  for line in data.get('stdout','').splitlines():
   if line.startswith('generated '):
    rel=line[len('generated '):].strip()
    p=pathlib.PurePosixPath(rel)
    if p.is_absolute() or any('adversarial' in part.lower() for part in p.parts):
     continue
    if p.suffix in ('.rs','.mossmap'):
     paths.add(rel)
removed=[];tracked=[];missing=[]
for rel in sorted(paths):
 dest=(root/rel).resolve()
 if not dest.is_relative_to(root): continue
 q=subprocess.run(['git','ls-files','--error-unmatch','--',rel],cwd=root,capture_output=True,text=True)
 if q.returncode==0:
  tracked.append(rel);continue
 if dest.exists():
  dest.unlink();removed.append(rel)
 else:missing.append(rel)
text=''.join(f'removed\t{x}\n' for x in removed)+''.join(f'tracked-not-deleted\t{x}\n' for x in tracked)+''.join(f'already-missing\t{x}\n' for x in missing)
(own/'generated_outputs_cleanup.txt').write_text(text)
print(f'printed generated paths={len(paths)} removed={len(removed)} tracked-not-deleted={len(tracked)} missing={len(missing)}')
if tracked: print('tracked paths left untouched')
