import json,os,pathlib,subprocess,sys,concurrent.futures
cand=pathlib.Path(sys.argv[1]).resolve();base=pathlib.Path(sys.argv[2]).resolve(); own=pathlib.Path(__file__).resolve().parent
results=[]
def one(label,root,binary):
 env=os.environ.copy();env['MOSS_EST']='off'
 try:
  p=subprocess.run([str(binary),'run','--interp','examples/functional_pipeline.moss'],cwd=root,env=env,capture_output=True,text=True,timeout=120)
  return {'engine':label,'returncode':p.returncode,'stdout':p.stdout,'stderr':p.stderr}
 except subprocess.TimeoutExpired as e:
  return {'engine':label,'timeout':120,'stdout':(e.stdout.decode(errors='replace') if isinstance(e.stdout,bytes) else e.stdout or ''),'stderr':(e.stderr.decode(errors='replace') if isinstance(e.stderr,bytes) else e.stderr or '')}
with concurrent.futures.ThreadPoolExecutor(2) as ex: results=list(ex.map(lambda x:one(*x),[('baseline',base,base/'build/compiler/moss-dev'),('candidate',cand,cand/'build/compiler/moss-dev')]))
results.append({'equal':results[0]=={k:v for k,v in results[1].items() if k!='engine'}})
(own/'fastdebug_pipeline_rerun.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
