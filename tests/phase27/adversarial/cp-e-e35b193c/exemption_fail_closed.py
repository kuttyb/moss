#!/usr/bin/env python3
import importlib.util,pathlib,re
root=pathlib.Path(__file__).resolve().parents[4]
spec=importlib.util.spec_from_file_location('debt',root/'tests/tooling/check_phase27_string_debt.py')
debt=importlib.util.module_from_spec(spec); spec.loader.exec_module(debt)
entries=debt.load_exemptions()
results=[]
original_read=debt.read_source
for key in sorted(entries):
 path,function,api=key
 source=original_read(path)
 # Fail closed if the keyed consumer disappears.
 pattern=re.compile(r'(?m)^([ \t]*(?:static[ \t]+)?(?:bool|vector<string>)[ \t]+)'+re.escape(function)+r'(\s*\()')
 changed,count=pattern.subn(lambda m:m.group(1)+'deleted_'+function+m.group(2),source,count=1)
 assert count==1
 debt.read_source=lambda rel,commit=None, p=path,s=changed: s if rel==p and commit is None else original_read(rel,commit)
 try: debt.sites(); outcome='UNEXPECTED PASS'
 except SystemExit as e: outcome='PASS missing consumer: '+str(e)
 results.append({'key':list(key),'case':'missing-function','result':outcome})
 # Fail closed if a second occurrence of the keyed reader appears in its function.
 definition=pattern.search(source)
 assert definition
 opening=source.find('{',definition.end())
 assert opening>=0
 duplicate=source[:opening+1]+"\n (void)moss::split_top_level(\"X,Y\", ',');\n"+source[opening+1:]
 debt.read_source=lambda rel,commit=None, p=path,s=duplicate: s if rel==p and commit is None else original_read(rel,commit)
 try: debt.sites(); outcome='UNEXPECTED PASS'
 except SystemExit as e: outcome='PASS duplicate site: '+str(e)
 results.append({'key':list(key),'case':'second-reader-site','result':outcome})
debt.read_source=original_read
for item in results: print(item)
assert len(results)==6 and all('PASS ' in x['result'] for x in results)
