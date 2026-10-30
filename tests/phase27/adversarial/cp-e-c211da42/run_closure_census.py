#!/usr/bin/env python3
"""Census every source declaration and checked callable in the closure probe."""
import json,os,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[4]
source=root/'tests/phase27/adversarial/cp-e-c211da42/closure_enum_recovery.moss'
out=root/'tests/phase27/adversarial/cp-e-c211da42'
all_rows={}
for label,compiler in [('baseline',pathlib.Path(sys.argv[1]).resolve()),('candidate',pathlib.Path(sys.argv[2]).resolve())]:
 def run(args):
  r=subprocess.run([str(compiler),*args,'--json'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=dict(os.environ,MOSS_EST='off'))
  if r.returncode not in (0,1): raise RuntimeError((args,r.returncode,r.stdout,r.stderr))
  return json.loads(r.stdout)
 check=run(['check',str(source)])
 symbols=run(['symbols','--source',str(source)])['result']['symbols']
 local=[s for s in symbols if pathlib.Path(s['source']['file']).name==source.name]
 calls=run(['calls','entity-v1:main','--source',str(source)])['result']
 reached={'main'}
 for e in calls.get('transitive_calls',[]):
  target=e['target']; reached.add(target.split(':',1)[1])
 declarations=[]
 for s in local:
  if s['kind'] not in ('function','main'): continue
  q=run(['inspect',s['entity_id'],'--source',str(source)])['result']
  declarations.append({'name':s['qualified_name'],'entity_id':s['entity_id'],'reachable_from_main':s['qualified_name'] in reached,'status':q.get('status'),'type_origin':q.get('type_facts',{}).get('origin')})
 all_rows[label]={'check':check,'declared_symbols':[{'kind':s['kind'],'name':s['qualified_name'],'id':s['entity_id']} for s in local],'calls':calls,'callable_census':declarations}
(out/'closure_raw.json').write_text(json.dumps(all_rows,indent=2,sort_keys=True)+'\n')

def normalized(x):
 check=x['check']; result=check.get('result',{}); result.pop('source_file',None)
 calls=x['calls']; calls.pop('target',None)
 return {'ok':check.get('ok'),'diagnostics':result.get('diagnostics'),'declared_symbols':x['declared_symbols'],'calls':calls,'callable_census':x['callable_census']}
summary={'equal':normalized(all_rows['baseline'])==normalized(all_rows['candidate']),'baseline':normalized(all_rows['baseline']),'candidate':normalized(all_rows['candidate'])}
(out/'closure_summary.json').write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n')
print(json.dumps({'equal':summary['equal'],'symbols':len(summary['candidate']['declared_symbols']),'callables':len(summary['candidate']['callable_census']),'reachable':sum(x['reachable_from_main'] for x in summary['candidate']['callable_census']),'unreachable':sum(not x['reachable_from_main'] for x in summary['candidate']['callable_census']),'all_checked':all(x['type_origin']=='checked_declaration' for x in summary['candidate']['callable_census'])},indent=2))
sys.exit(not summary['equal'])
