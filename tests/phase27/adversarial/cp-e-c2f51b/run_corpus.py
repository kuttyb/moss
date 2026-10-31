import concurrent.futures, pathlib, subprocess, json, os, re
root=pathlib.Path(__file__).resolve().parents[4]
base=root.parent/'cp-e-base/build/compiler/moss-dev'; cand=root/'build/compiler/moss-dev'
files=sorted(subprocess.check_output(['git','ls-files','*.moss'],cwd=root,text=True).splitlines())
# all current tests/**/*.moss, skip generated outputs

def run(binary, mode, path):
 env=os.environ.copy(); env['MOSS_EST']=mode
 try:
  p=subprocess.run([str(binary),'check',str(root/path),'--json'],cwd=root,env=env,text=True,capture_output=True,timeout=45)
  try: j=json.loads(p.stdout); err=j.get('error') or {}; code=err.get('code'); ok=j.get('ok')
  except Exception: code='NO_JSON'; ok=False
  return {'path':path,'ok':ok,'code':code,'line':(j.get('error') or {}).get('line') if isinstance(j,dict) else None,'timeout':False,'stderr':p.stderr[:250]}
 except subprocess.TimeoutExpired: return {'path':path,'ok':None,'code':'TIMEOUT','timeout':True}

def pair(path): return path,run(base,'off',path),run(cand,'off',path),run(cand,'shadow',path)
with concurrent.futures.ThreadPoolExecutor(8) as ex: rows=list(ex.map(pair,files))
(root/'tests/phase27/adversarial/cp-e-c2f51b/corpus.jsonl').write_text(''.join(json.dumps({'path':p,'baseline_off':a,'candidate_off':b,'candidate_shadow':c},sort_keys=True)+'\n' for p,a,b,c in rows))
mis=[{'path':p,'base':a,'off':b,'shadow':c} for p,a,b,c in rows if (a['ok'],a['code'],a['line'])!=(b['ok'],b['code'],b['line']) or (b['ok'],b['code'],b['line'])!=(c['ok'],c['code'],c['line']) or a['timeout'] or b['timeout'] or c['timeout']]
summary={'count':len(rows),'baseline_accepted':sum(r[1]['ok'] is True for r in rows),'candidate_off_accepted':sum(r[2]['ok'] is True for r in rows),'candidate_shadow_accepted':sum(r[3]['ok'] is True for r in rows),'timeouts':sum(r[k]['timeout'] for r in rows for k in (1,2,3)),'mismatches':mis}
(root/'tests/phase27/adversarial/cp-e-c2f51b/corpus-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='mismatches'},indent=2)); print('mismatch_count',len(mis))
