import concurrent.futures, hashlib, json, os, pathlib, resource, subprocess, sys
root=pathlib.Path(sys.argv[1]).resolve(); compiler=pathlib.Path(sys.argv[2]).resolve(); label=sys.argv[3]
name=pathlib.Path(__file__).resolve().parent
sources=subprocess.check_output(['git','ls-tree','-r','--name-only','HEAD'],cwd=root,text=True).splitlines()
sources=[p for p in sources if p.endswith('.moss') and not any('adversarial' in part.lower() for part in pathlib.PurePosixPath(p).parts)]
markers=('ERROR: AddressSanitizer','SUMMARY: AddressSanitizer','runtime error:','UndefinedBehaviorSanitizer')
def run(src):
 env=os.environ.copy(); env.update(MOSS_EST='shadow',MOSS_PHASE26_C1_SCOPE_TRACE='1',ASAN_OPTIONS='abort_on_error=1:symbolize=1:detect_leaks=0',UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')
 try:
  p=subprocess.run([str(compiler),'check',src,'--json'],cwd=root,env=env,capture_output=True,text=True,timeout=90,preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)))
 except subprocess.TimeoutExpired as e:
  return {'source':src,'kind':'timeout','stdout':e.stdout or '', 'stderr':e.stderr or ''}
 text=p.stdout+p.stderr; found=[m for m in markers if m in text]
 try: json.loads(p.stdout); has_json=True
 except Exception: has_json=False
 if found or p.returncode<0 or not has_json:
  return {'source':src,'kind':'sanitizer' if found else 'signal' if p.returncode<0 else 'no_json','returncode':p.returncode,'markers':found,'stdout':p.stdout,'stderr':p.stderr}
 return None
fail=[]; accepted=0; rejected=0
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for result in pool.map(run,sources):
  if result: fail.append(result)
  # counts determined from valid JSON result not retained
report={'revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),'sanitizers':['ASan','UBSan'],'mode':'shadow','source_count':len(sources),'finding_count':len(fail),'findings':fail,'excluded_adversarial_path_segments':True}
out=name/f'{label}_sanitizer.json'; out.write_text(json.dumps(report,indent=2)+'\n'); print(f'{label}: {len(sources)} sources; {len(fail)} sanitizer/process findings; {out}')
