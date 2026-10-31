import importlib.util,json,pathlib,re,shutil,tempfile
root=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('debt',root/'tests/tooling/check_phase27_string_debt.py'); debt=importlib.util.module_from_spec(spec);spec.loader.exec_module(debt)
source=(root/'src/semantic_checker.cpp').read_text(); exemptions=(root/'docs/phase27/27_3_STRING_READER_EXEMPTIONS.md').read_text()
entries=[('src/semantic_checker.cpp','phase20_is_nontransferable_capability','split_top_level'),('src/semantic_checker.cpp','builtin_equality_type','split_top_level'),('src/semantic_checker.cpp','contains_float_type','split_top_level')]
with tempfile.TemporaryDirectory(dir=root/'tmp') as td:
 t=pathlib.Path(td); (t/'src').mkdir();(t/'docs/phase27').mkdir(parents=True);(t/'src/semantic_checker.cpp').write_text(source);(t/'docs/phase27/27_3_STRING_READER_EXEMPTIONS.md').write_text(exemptions)
 debt.ROOT=t;debt.EXEMPTIONS=t/'docs/phase27/27_3_STRING_READER_EXEMPTIONS.md'
 result={'entry_count':len(debt.load_exemptions()),'baseline':'pass','checks':[]}
 debt.sites()
 for path,fn,api in entries:
  src=source; ranges=debt.function_ranges(src,{fn}); start,end=ranges[fn]; lines=src.splitlines(keepends=True); begin=sum(len(x) for x in lines[:start-1]); finish=sum(len(x) for x in lines[:end-1]); chunk=src[begin:finish]
  call=re.search(r'\b'+api+r'\s*\(',chunk); assert call
  stale=chunk[:call.start()]+'non_reader('+chunk[call.start()+len(api):]
  (t/'src/semantic_checker.cpp').write_text(src[:begin]+stale+src[finish:])
  try: debt.sites(); stale_rejected=False
  except SystemExit as e: stale_rejected='stale expression-reader exemptions' in str(e)
  dup=chunk+'\n  '+api+'("duplicate");\n'
  (t/'src/semantic_checker.cpp').write_text(src[:begin]+dup+src[finish:])
  try: debt.sites(); dup_rejected=False
  except SystemExit as e: dup_rejected='multiple reader sites' in str(e)
  result['checks'].append({'function':fn,'api':api,'removal_rejected':stale_rejected,'duplicate_rejected':dup_rejected})
 result['ok']=all(c['removal_rejected'] and c['duplicate_rejected'] for c in result['checks'])
 (root/'tmp/cp-e-evidence/exemption-fail-closed.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result))
