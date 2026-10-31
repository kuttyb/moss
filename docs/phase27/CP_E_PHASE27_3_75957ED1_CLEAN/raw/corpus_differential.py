import concurrent.futures, json, os, pathlib, subprocess, time
root=pathlib.Path.cwd(); exclude='tests/phase27/adversarial'; files=[]
for top in ('tests','sdk'):
 for directory,dirs,names in os.walk(root/top,followlinks=False):
  rel=pathlib.Path(directory).relative_to(root).as_posix()
  dirs[:]=[d for d in dirs if not (top=='tests' and pathlib.PurePosixPath(rel,d).as_posix()==exclude) and 'adversarial' not in d.lower()]
  files.extend(pathlib.Path(directory)/n for n in names if n.endswith('.moss'))
files=sorted(files); outdir=root/'tmp/CP_E_PHASE27_3_75957ED1_CLEAN'
outdir.mkdir(parents=True,exist_ok=True)
(outdir/'corpus_manifest.txt').write_text(''.join(p.relative_to(root).as_posix()+'\n' for p in files))
comps={
 'base_off':(root.parent/'CP_E_PHASE27_3_75957ED1_CLEAN_BASE/moss','off'),
 'base_shadow':(root.parent/'CP_E_PHASE27_3_75957ED1_CLEAN_BASE/moss','shadow'),
 'cand_off':(root/'moss','off'),
 'cand_shadow':(root/'moss','shadow'),}
def one(item):
 path,label=item; exe,mode=comps[label]; env=os.environ.copy(); env['MOSS_EST']=mode
 try:
  r=subprocess.run([str(exe),'check',str(path),'--json'],cwd=root,env=env,text=True,capture_output=True,timeout=30)
  p=json.loads(r.stdout); e=p.get('error',{})
  return (path.relative_to(root).as_posix(),label,r.returncode,p.get('ok'),e.get('code'),e.get('message'),e.get('line'),e.get('column'),None if 'error' in p else p.get('result',{}).get('diagnostics'))
 except Exception as e:return(path.relative_to(root).as_posix(),label,None,None,None,None,None,None,repr(e))
items=[(p,l) for p in files for l in comps]; results=[]; start=time.time()
with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
 for i,row in enumerate(pool.map(one,items),1):
  results.append(row)
  if i%500==0: print('completed',i,'/',len(items),flush=True)
(outdir/'corpus_results.json').write_text(json.dumps(results,indent=2)+'\n')
by={}
for row in results:by.setdefault(row[0],{})[row[1]]=row[2:]
summary={'files':len(files),'runs':len(results),'elapsed_seconds':round(time.time()-start,2),'failures':sum(1 for r in results if r[2] is None),'baseline_mode_divergences':0,'candidate_mode_divergences':0,'baseline_candidate_off_diffs':0}
for rows in by.values():
 summary['baseline_mode_divergences']+=rows['base_off']!=rows['base_shadow'];summary['candidate_mode_divergences']+=rows['cand_off']!=rows['cand_shadow'];summary['baseline_candidate_off_diffs']+=rows['base_off']!=rows['cand_off']
print(json.dumps(summary,sort_keys=True))
for path,rows in by.items():
 if rows['cand_off']!=rows['cand_shadow']:print('candidate mode diff',path,rows['cand_off'],rows['cand_shadow'])
for path,rows in by.items():
 if rows['base_off']!=rows['cand_off']:print('baseline candidate diff',path,rows['base_off'],rows['cand_off'])
