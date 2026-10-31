from pathlib import Path
import json
root=Path(__file__).resolve().parents[4]; out=root/'tests/phase27/adversarial/cp-e-c2f51b'
runs=json.loads((out/'native-build-reproduction.json').read_text())
source=root/'src/numeric_runtime.cpp'; gen=root/'src/native_generator.cpp'
report={}
for run in runs:
 lines=run['generated_numeric_helper_lines']
 report[run['label']]={'source_sha':run['source_sha'],'compiler_sha256':run['compiler_sha256'],'build_exit':run['exit'],'generated_call_site_count':sum(not r['definition'] for r in lines),'generated_definition_count':sum(r['definition'] for r in lines),'generated_lines':lines}
report['runtime_source_definitions']=[{'file':'src/numeric_runtime.cpp','line':n,'text':line.strip()} for n,line in enumerate(source.read_text().splitlines(),1) if line.lstrip().startswith('fn __moss_parse_')]
report['conditional_runtime_emission']=[{'file':'src/native_generator.cpp','line':n,'text':line.strip()} for n,line in enumerate(gen.read_text().splitlines(),1) if 'program_uses_numeric_builtins' in line or 'numeric_runtime_rust()' in line]
(out/'native-symbols.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:{'build_exit':v['build_exit'],'calls':v['generated_call_site_count'],'definitions':v['generated_definition_count']} for k,v in report.items() if isinstance(v,dict) and 'build_exit' in v},indent=2))
