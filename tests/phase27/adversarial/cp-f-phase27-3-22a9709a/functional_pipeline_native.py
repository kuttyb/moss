import concurrent.futures,json,os,pathlib,subprocess,sys
cand=pathlib.Path(sys.argv[1]).resolve();base=pathlib.Path(sys.argv[2]).resolve();own=pathlib.Path(__file__).resolve().parent
cases=[('baseline',base,base/'build/compiler/moss-dev'),('candidate',cand,cand/'build/compiler/moss-dev')]
def one(item):
 label,root,binary=item; env=os.environ.copy();env['MOSS_EST']='off'; outdir=root/'tmp/cpf/functional_pipeline_native';outdir.mkdir(parents=True,exist_ok=True); runs=[]
 for moss_profile,args in [('default',()),('optimized',('-Oshared-memory',))]:
  rust=outdir/f'{label}_{moss_profile}.rs';p=subprocess.run([str(binary),*args,'examples/functional_pipeline.moss','-o',str(rust)],cwd=root,env=env,capture_output=True,text=True,timeout=30)
  if p.returncode or not rust.exists(): runs.append({'moss_profile':moss_profile,'emit_rc':p.returncode,'emit_stderr':p.stderr});continue
  for rust_profile,rflags in [('default',()),('optimized',('-O',))]:
   exe=outdir/f'{label}_{moss_profile}_{rust_profile}';c=subprocess.run(['rustc','--edition=2021',*rflags,str(rust),'-o',str(exe)],capture_output=True,text=True,timeout=60)
   if c.returncode:runs.append({'moss_profile':moss_profile,'rust_profile':rust_profile,'rustc_rc':c.returncode,'rustc_stderr':c.stderr});continue
   q=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
   runs.append({'moss_profile':moss_profile,'rust_profile':rust_profile,'returncode':q.returncode,'stdout':q.stdout,'stderr':q.stderr})
 return {'engine':label,'runs':runs}
with concurrent.futures.ThreadPoolExecutor(2) as pool: rows=list(pool.map(one,cases))
(own/'functional_pipeline_native.json').write_text(json.dumps(rows,indent=2)+'\n'); print(json.dumps(rows,indent=2))
