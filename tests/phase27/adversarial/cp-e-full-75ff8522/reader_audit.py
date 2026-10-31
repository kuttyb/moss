#!/usr/bin/env python3
"""Generate current checker/emitter expression-reader inventory and taint guards."""
import collections, hashlib, importlib.util, json, pathlib, re
HERE=pathlib.Path(__file__).resolve().parent; ROOT=HERE.parents[3]
spec=importlib.util.spec_from_file_location("debt",ROOT/"tests/tooling/check_phase27_string_debt.py")
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
src_paths=[p for p in m.paths() if m.consumer_for(p) in ("checker","emitter")]
records=[]; name_counts=collections.Counter();binding_counts=collections.Counter();shape=[];tree_text=[];apis=collections.Counter();tree_api=collections.Counter();field_equalities=[];empty_tests=[]
for p in src_paths:
    consumer=m.consumer_for(p); text=m.read_source(p)
    for i,line in enumerate(text.splitlines(),1):
        for ma in m.RAW_NAME_COMPARE.finditer(line):
            name_counts[(consumer,ma[1])]+=1
            records.append({"kind":"raw-name","consumer":consumer,"path":p,"line":i,"name":ma[1],"source":line.strip()})
        for ma in m.RAW_FIELD_VARIABLE_COMPARE.finditer(line):
            binding_counts[(consumer,ma[1])]+=1
            records.append({"kind":"raw-binding-name","consumer":consumer,"path":p,"line":i,"slot":ma[1],"source":line.strip()})
        if m.RAW_FIELD_SHAPE.search(line) or m.RAW_FIELD_PREFIX.search(line) or m.RAW_FIELD_FRONT_DISPATCH.search(line):
            shape.append({"path":p,"line":i,"source":line.strip()})
        if re.search(r"\btree_expression_text\s*\(",line):tree_text.append({"path":p,"line":i,"source":line.strip()})
        if re.search(r"\.(?:a|b|text)\s*(?:==|!=)|(?:==|!=)\s*[^;]*\.(?:a|b|text)\b",line):
            field_equalities.append({"consumer":consumer,"path":p,"line":i,"source":line.strip()})
        if re.search(r"\.(?:a|b|text)\.empty\s*\(\)",line):
            empty_tests.append({"consumer":consumer,"path":p,"line":i,"source":line.strip()})
        for api in m.APIS:
            if re.search(r"\b"+re.escape(api)+r"\s*\(",line):apis[api]+=1
        for api in re.findall(r"\b(tree_[A-Za-z0-9_]+)\s*\(",line):tree_api[api]+=1
sites=m.sites()
if name_counts!=collections.Counter(m.RAW_NAME_COUNTS):
    raise SystemExit(f"raw NAME comparison inventory changed: {dict(name_counts)}")
if binding_counts!=collections.Counter(m.RAW_BINDING_COMPARE_COUNTS):
    raise SystemExit(f"raw field-variable comparison inventory changed: {dict(binding_counts)}")
exemptions=[{"path":p,"function":fn,"api":api,"reason":reason}
            for (p,fn,api),reason in sorted(m.load_exemptions().items())]
raw_expression_comparisons=[
 {"path":"src/semantic_checker.cpp","line":9905,"function":"check_closed_body_types",
  "expression":"stmt.a == name","role":"diagnostic line selection can inspect mixed expression/name slot",
  "classification":"MIGRATION BUG; direct expression-bearing slot equality remains"},
 {"path":"src/semantic_checker.cpp","line":19851,"function":"phase21_raise_set_block",
  "expression":"statement.a == selected_binding","role":"Raise value spelling selects the caught RaiseSet",
  "classification":"MIGRATION BUG; semantic raise-set decision uses raw expression equality"},
 {"path":"src/semantic_checker.cpp","line":20056,"function":"phase21_failure_capture_reads",
  "expression":"tree_plain_identifier(expression) && expression == statement.a",
  "role":"filters self-assignment from failure captures after EST shape guard",
  "classification":"MIGRATION BUG; EST identifier projection is followed by raw spelling equality"}]
result={"scope_files":src_paths,"legacy_reader_call_sites":{"checker":sum(n for k,n in sites.items() if k[0]=="checker"),"emitter":sum(n for k,n in sites.items() if k[0]=="emitter")},
"tree_expression_text_hits":tree_text,"raw_expression_shape_hits":shape,
"raw_name_counts":{f"{c}/{n}":v for (c,n),v in sorted(name_counts.items())},
"raw_binding_name_counts":{f"{c}/{n}":v for (c,n),v in sorted(binding_counts.items())},
"raw_field_records":records,"all_raw_stmt_field_equality_lines":field_equalities,
"stmt_field_empty_presence_tests":empty_tests,"raw_expression_comparison_findings":raw_expression_comparisons,
"tree_helper_callsite_lexical_counts":dict(sorted(tree_api.items())),
"exemptions":exemptions,"source_sha256":{p:hashlib.sha256(m.read_source(p).encode()).hexdigest() for p in src_paths}}
if result["legacy_reader_call_sites"]!={"checker":0,"emitter":0} or tree_text or shape:
    raise SystemExit("expression-text / shape reader found")
(HERE/"reader-audit.json").write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")
print(json.dumps({"source_files":len(src_paths),"legacy_checker":0,"legacy_emitter":0,"tree_expression_text_hits":0,"raw_name_comparisons":sum(name_counts.values()),"raw_binding_name_comparisons":sum(binding_counts.values()),"shape_hits":0,"exemptions":len(exemptions)}))
