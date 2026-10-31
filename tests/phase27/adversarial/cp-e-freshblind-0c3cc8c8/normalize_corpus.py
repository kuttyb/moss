import json,pathlib
root=pathlib.Path(__file__).resolve().parents[2]
rows=[json.loads(x) for x in open(root/'tmp/cp-e-evidence/corpus.jsonl')]
excluded=[]; kept=[]
for r in rows:
 if r['path']=='sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss': excluded.append(r)
 else: kept.append(r)
def norm(x):
 y=dict(x)
 for path in ('/home/owner/daji/code/moss/tmp/cp-e-baseline-04be1a1','/home/owner/daji/code/moss/tmp/cp-e-blind-0c3cc8c8'):
  for key in ('message',):
   if y.get(key):y[key]=y[key].replace(path,'<WORKTREE>')
 return y
bd=[r for r in kept if norm(r['baseline_off'])!=norm(r['candidate_off'])]
sh=[r for r in kept if norm(r['candidate_off'])!=norm(r['candidate_shadow'])]
summary={'raw_file_count':len(rows),'comparable_common_files':len(kept),'excluded_out_of_project_context':[{'path':r['path'],'baseline':r['baseline_off'],'candidate':r['candidate_off']} for r in excluded],'baseline_candidate_off_differences_after_worktree_path_normalization':len(bd),'candidate_off_shadow_differences':len(sh),'candidate_off_rejections_in_comparable_files':sum(not r['candidate_off']['ok'] for r in kept),'baseline_candidate_off_verdict_differences':sum(r['baseline_off']['ok']!=r['candidate_off']['ok'] for r in kept)}
(root/'tmp/cp-e-evidence/corpus-normalized-summary.json').write_text(json.dumps(summary,indent=2)+'\n');(root/'tmp/cp-e-evidence/corpus-normalized-diffs.json').write_text(json.dumps({'baseline_candidate_off':bd,'candidate_off_shadow':sh},indent=2)+'\n');print(json.dumps(summary))
