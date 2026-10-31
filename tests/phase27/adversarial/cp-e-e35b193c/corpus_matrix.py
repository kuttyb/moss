#!/usr/bin/env python3
import concurrent.futures, json, os, pathlib, subprocess, sys, time
ROOT=pathlib.Path(__file__).resolve().parents[4]
CAND=ROOT/'moss'
BASE=ROOT/'tmp/phase27-3/review-cp-e-e35b193c-baseline/moss'
SOURCES=sorted(x for x in subprocess.check_output(['git','ls-files','*.moss'],cwd=ROOT,text=True).splitlines() if (ROOT/x).is_file())
def one(source):
    path=ROOT/source
    out={'source':source}
    for label,compiler,mode in [('baseline',BASE,'off'),('candidate_off',CAND,'off'),('candidate_shadow',CAND,'shadow')]:
        env=os.environ.copy(); env['MOSS_EST']=mode
        try:
            r=subprocess.run([str(compiler),'check',str(path),'--json'],cwd=ROOT,env=env,text=True,capture_output=True,timeout=20)
            obj=json.loads(r.stdout)
            res=obj.get('result',{})
            out[label]={'rc':r.returncode,'ok':obj.get('ok'),'diagnostics':res.get('diagnostics'),'error':obj.get('error'),'json_command':obj.get('command')}
        except subprocess.TimeoutExpired:
            out[label]={'timeout':True}
        except Exception as e:
            out[label]={'error':repr(e),'stderr':getattr(locals().get('r',None),'stderr','')}
    return out
start=time.time(); rows=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    for row in pool.map(one,SOURCES): rows.append(row)
path=ROOT/'tests/phase27/adversarial/cp-e-e35b193c/corpus-matrix.json'
path.write_text(json.dumps({'candidate':'e35b193c4f8ffca14fd820f6c938f53e1c1a0744','baseline':'04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4','source_count':len(SOURCES),'elapsed_seconds':round(time.time()-start,2),'rows':rows},indent=2)+'\n')
def clean(x):
    return (x.get('rc'),x.get('ok'),x.get('diagnostics'),x.get('error'),x.get('timeout'),x.get('error'))
diffs=[]
for r in rows:
    if clean(r.get('baseline',{}))!=clean(r.get('candidate_off',{})) or clean(r.get('candidate_off',{}))!=clean(r.get('candidate_shadow',{})):
        diffs.append({'source':r['source'],'baseline':clean(r.get('baseline',{})),'off':clean(r.get('candidate_off',{})),'shadow':clean(r.get('candidate_shadow',{}))})
print(json.dumps({'sources':len(rows),'differences':len(diffs),'diffs':diffs[:100]}))
