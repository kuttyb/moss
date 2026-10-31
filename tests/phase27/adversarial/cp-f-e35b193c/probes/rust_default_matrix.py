import os,pathlib,subprocess,sys
root=pathlib.Path.cwd(); work=root/'tmp/phase27-3/cp-f-agent2-e35b193c'
bins={'baseline':root.parent/'cp-f-agent2-baseline-e35b193c/build/compiler/moss-dev','candidate':root/'build/compiler/moss-dev'}
for rev in ('baseline','candidate'):
 for mode in ('off','shadow'):
  out=work/f'rust-{rev}-{mode}.tsv';env=os.environ.copy();env['MOSS_EST']=mode
  cmd=['python3','tests/tooling/corpus_verdicts.py',str(bins[rev]),str(out),'--rev','e35b193c4f8ffca14fd820f6c938f53e1c1a0744','--emit','--jobs','16']
  p=subprocess.run(cmd,cwd=root,env=env,text=True,capture_output=True)
  (work/f'rust-{rev}-{mode}.log').write_text(p.stdout+p.stderr)
  print(rev,mode,'rc',p.returncode,p.stdout.strip(),flush=True)
  if p.returncode:sys.exit(p.returncode)
