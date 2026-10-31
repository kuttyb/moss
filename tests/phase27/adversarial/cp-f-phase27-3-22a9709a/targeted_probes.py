import json, os, pathlib, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parents[4]
CAND = pathlib.Path(sys.argv[1]).resolve()
BASE = pathlib.Path(sys.argv[2]).resolve()
OUT = ROOT / 'tests/phase27/adversarial/cp-f-phase27-3-22a9709a'
CASES = {
    'unsupported_exponent': 'tests/phase27/est/repro/unsupported_exponent.moss',
    'unsupported_operator': 'tests/phase27/est/repro/unsupported_operators.moss',
    'membership_operator': 'tests/phase27/est/repro/membership_operator.moss',
    'symbolic_boolean': 'tests/phase27/est/repro/symbolic_boolean.moss',
}
rows=[]
for name, rel in CASES.items():
    by_engine={}
    for label, root, binary in [('baseline', BASE, BASE/'build/compiler/moss-dev'), ('candidate', CAND, CAND/'build/compiler/moss-dev')]:
        for mode in ('off','shadow'):
            env=os.environ.copy(); env['MOSS_EST']=mode
            p=subprocess.run([str(binary),'check',rel,'--json'],cwd=root,env=env,text=True,capture_output=True)
            try: payload=json.loads(p.stdout)
            except Exception: payload={'json_error':p.stdout,'stderr':p.stderr,'returncode':p.returncode}
            e=payload.get('error') or {}
            by_engine[f'{label}_{mode}']={'ok':payload.get('ok'),'code':e.get('code'),'message':e.get('message'),'line':e.get('line'),'column':e.get('column'),'returncode':p.returncode}
    rows.append({'case':name,'path':rel,'results':by_engine})
    if len({json.dumps(v,sort_keys=True) for v in by_engine.values()}) != 1:
        print('DIFF',name,by_engine)
(OUT/'targeted_probes.json').write_text(json.dumps(rows,indent=2)+'\n')
