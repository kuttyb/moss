import concurrent.futures,json,os,pathlib,subprocess,sys
cand=pathlib.Path(sys.argv[1]).resolve();base=pathlib.Path(sys.argv[2]).resolve();own=pathlib.Path(__file__).resolve().parent
cases=[('baseline',base,base/'build/compiler/moss-dev'),('candidate',cand,cand/'build/compiler/moss-dev')]
def one(item):
 label,root,binary=item;env=os.environ.copy();env['MOSS_EST']='off'
 try:
  p=subprocess.run([str(binary),'debug','tests/phase27/conformance/three_engine.moss'],cwd=root,env=env,capture_output=True,text=True,timeout=30)
  return {'engine':label,'returncode':p.returncode,'stdout':p.stdout,'stderr':p.stderr}
 except subprocess.TimeoutExpired as e:
  return {'engine':label,'timeout_seconds':30,'stdout':e.stdout.decode(errors='replace') if isinstance(e.stdout,bytes) else e.stdout or '', 'stderr':e.stderr.decode(errors='replace') if isinstance(e.stderr,bytes) else e.stderr or ''}
with concurrent.futures.ThreadPoolExecutor(2) as pool:rows=list(pool.map(one,cases))
(own/'fastdebug_conformance_fixture.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows,indent=2))
