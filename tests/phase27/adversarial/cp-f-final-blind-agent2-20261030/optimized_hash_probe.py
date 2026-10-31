import concurrent.futures,hashlib,io,json,os,shutil,subprocess,sys,tarfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]; compiler=Path(sys.argv[1]).resolve(); label=sys.argv[2]
rev='HEAD'; corpus=root/'tmp'/f'cpf-opt-rust-{label}'
shutil.rmtree(corpus,ignore_errors=True); corpus.mkdir(parents=True)
with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','archive',rev],cwd=root))) as t:t.extractall(corpus,filter='data')
sources=[x for x in subprocess.check_output(['git','ls-tree','-r','--name-only',rev],cwd=root,text=True).splitlines() if x.endswith('.moss')]
def one(source):
 env=os.environ.copy();env['MOSS_EST']='off'
 try:
  c=subprocess.run([str(compiler),'check',source,'--json'],cwd=corpus,env=env,text=True,capture_output=True,timeout=120)
  d=json.loads(c.stdout)
 except Exception as e:return source,'NOJSON:'+str(e)
 if not d.get('ok'):
  e=d.get('error') or {}; return source,f"{e.get('code')}:{e.get('line')}"
 out=corpus/'tmp'/'emit'/ (source.replace('/','_')+'.rs');out.parent.mkdir(parents=True,exist_ok=True)
 try:e=subprocess.run([str(compiler),source,'-O','-o',str(out)],cwd=corpus,env=env,text=True,capture_output=True,timeout=120)
 except subprocess.TimeoutExpired:return source,'EMIT_TIMEOUT'
 if e.returncode or not out.exists():return source,'NO_EMIT'
 digest=hashlib.sha256(out.read_bytes()).hexdigest();out.unlink();return source,'ok:'+digest
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:rows=dict(pool.map(one,sources))
out=root/'tmp'/f'cp-f-rust-opt-{label}.tsv';out.write_text(''.join(f'{k}\t{rows[k]}\n' for k in sorted(rows)))
print(f'{len(rows)} sources; {sum(v.startswith("ok:") for v in rows.values())} optimized Rust emitted; {sum(not v.startswith("ok:") for v in rows.values())} rejected/emit issues; output={out.relative_to(root)}')
