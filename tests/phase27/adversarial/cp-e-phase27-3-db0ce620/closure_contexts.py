#!/usr/bin/env python3
"""Classify missing C1 traces by the source-context rules used by Moss."""
import json, re, tomllib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
DIR=ROOT/'tests/phase27/adversarial/cp-e-phase27-3-db0ce620'
rows=[json.loads(line) for line in (DIR/'closure.jsonl').open()]

def within(path, root):
    try: rel=path.relative_to(root)
    except ValueError: return False
    return bool(rel.parts) and rel.parts[0] != '..'

def local_import_context(path):
    seen=set(); pending=[path]; found=False
    while pending:
        current=pending.pop()
        if current in seen: continue
        seen.add(current)
        try: text=current.read_text()
        except OSError: continue
        for module in re.findall(r'^\s*import\s+([A-Za-z_][A-Za-z0-9_]*)\b',text,re.M):
            local=current.parent/(module+'.moss')
            if local.is_file(): found=True; pending.append(local)
    return found

entries=[]
for row in rows:
    if row['legacy_complete'] and row['tree_complete']: continue
    path=ROOT/row['entry']; cur=path.parent; manifest=None
    while cur != ROOT.parent:
        for name in ('Moss.toml','moss.toml'):
            if (cur/name).is_file(): manifest=cur/name; break
        if manifest or cur==ROOT: break
        cur=cur.parent
    if manifest:
        try: data=tomllib.loads(manifest.read_text())
        except Exception: data={}
        source_root=manifest.parent/data.get('source','src')
        target=within(path,source_root) or within(path,manifest.parent/'tests') or within(path,manifest.parent/'benches')
        reason='project-target-no-trace-early-reject' if target else 'standalone-outside-project-target'
    else:
        target=local_import_context(path)
        reason='local-import-project' if target else 'standalone-no-project-context'
    entries.append({'entry':row['entry'],'trace_complete_baseline':row['legacy_complete'],
      'trace_complete_candidate':row['tree_complete'],'baseline_verdict':row['legacy_verdict'],
      'candidate_verdict':row['tree_verdict'],'project_scope_by_context_rules':target,
      'classification':reason})
from collections import Counter
summary=Counter(e['classification'] for e in entries)
(DIR/'closure-context-exclusions.json').write_text(json.dumps({'counts':dict(summary),'entries':entries},indent=2)+'\n')
print('incomplete trace entries',len(entries),'context classes',dict(summary))
print('scoped-target early-reject entries',sum(e['project_scope_by_context_rules'] for e in entries))
