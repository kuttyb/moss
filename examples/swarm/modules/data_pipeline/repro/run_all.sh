#!/bin/bash
ROOT=/home/kuttybanerjee/daji/moss3
D=$ROOT/examples/swarm/modules/data_pipeline/repro
short(){ python3 -c "
import sys,json,re
t=sys.stdin.read()
try:
  i=t.index('{'); d=json.loads(t[i:]); e=d.get('error',d); m=e['message'] if isinstance(e,dict) else str(e)
  print('  ERR', (e.get('code','') if isinstance(e,dict) else ''), m.split('\n')[0][:160], '| reported file:', (e.get('source_file','') if isinstance(e,dict) else '').replace('$ROOT/',''), 'line', e.get('line') if isinstance(e,dict) else '')
  if 'error[E' in m: print('  rustc:', re.findall(r'error\[E\d+\]: [^\n]*', m)[0])
except Exception:
  print('  ' + t.strip().replace('\n','\n  ')[:400])"; }
run(){ # dir cmd...
  d=$1; shift; echo "--- $d: $*"; (cd $D/$d && "$@" 2>&1 | short); }
for d in field_access/single field_access/split construct_across/single construct_across/split two_method_calls/single two_method_calls/split sibling_method/single sibling_method/split range_in_module/single range_in_module/split trait_across/single trait_across/split dep_type_methods/one_package dep_type_methods/two_packages/app dep_implicit_main/app_implicit dep_implicit_main/app_module test_module_mismatch/implicit_test test_module_mismatch/matching_module string_concat_field/single map_int_key/single map_in_while/single and_or_fast_debug/single write_params_ok/app; do
  case $d in test_module_mismatch/*) run $d $ROOT/margo test;; *) run $d $ROOT/margo run;; esac
  case $d in range_in_module/single|string_concat_field/single|map_int_key/single|map_in_while/single|and_or_fast_debug/single) ;; esac
  run $d $ROOT/margo debug
done
