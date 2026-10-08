#!/usr/bin/env python3
"""Builtin/generic typing, constructor and vector ownership, and assert syntax."""
import argparse
import json
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FIXTURES = REPO / 'tests/tooling/fixtures/builtin_generic_typing'
BUILD = REPO / 'tmp/builtin-generic-typing'


def run(command):
    result = subprocess.run([str(arg) for arg in command], cwd=REPO,
                            capture_output=True, text=True, timeout=180)
    return {'exit': result.returncode, 'stdout': result.stdout,
            'stderr': result.stderr}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('compiler', nargs='?', default='./moss')
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    compiler = (REPO / args.compiler).resolve()
    BUILD.mkdir(parents=True, exist_ok=True)
    cases = json.loads((FIXTURES / 'manifest.json').read_text())
    records, failures = {}, []
    for name, case in cases.items():
        source = FIXTURES / (name + '.moss')
        record = {'check': run([compiler, 'check', source, '--json']),
                  'lower': run([compiler, source, '-o', BUILD / (name + '.rs')]),
                  'interp': run([compiler, 'run', '--interp', source])}
        if record['lower']['exit'] == 0:
            record['native_compile'] = run(['rustc', '-O', '-D', 'warnings',
                                           BUILD / (name + '.rs'), '-o', BUILD / name])
            if record['native_compile']['exit'] == 0:
                record['native'] = run([BUILD / name])
        errors = []
        doc = json.loads(record['check']['stdout'])
        if 'code' in case:
            if record['check']['exit'] == 0 or doc.get('ok') or doc.get('error', {}).get('code') != case['code']:
                errors.append('expected ' + case['code'] + ': ' + str(doc.get('error')))
            for stage in ['lower', 'interp']:
                if record[stage]['exit'] == 0 or case['code'] not in record[stage]['stderr']:
                    errors.append(stage + ' did not reject at the shared front end')
        else:
            for stage in ['check', 'lower', 'native_compile', 'native', 'interp']:
                if record.get(stage, {}).get('exit') != 0:
                    errors.append(stage + ': ' + str(record.get(stage))[:600])
            if record.get('native', {}).get('stdout') != case['stdout']:
                errors.append('unexpected native output')
            if record['interp']['stdout'] != case['stdout']:
                errors.append('unexpected Fast Debug output')
            if record.get('native', {}).get('stdout') != record['interp']['stdout']:
                errors.append('native/Fast Debug parity failure')
            for target, expected_effects in case.get('effects', {}).items():
                for query in ['type', 'effects', 'ownership', 'inspect']:
                    result = run([compiler, query, target, '--source', source, '--json'])
                    record[query + ':' + target] = result
                    if result['exit'] != 0:
                        errors.append(query + ' query failed for ' + target)
                    elif query in ['effects', 'ownership']:
                        result_doc = json.loads(result['stdout'])
                        key = 'ownership' if query == 'effects' else 'parameters_and_values'
                        effects = [p['effect'] for p in result_doc.get('result', {}).get(key, [])]
                        if effects != expected_effects:
                            errors.append(f'{query} {target}: expected {expected_effects}, got {effects}')
            for target, expected_leaves in case.get('leaf_effects', {}).items():
                result = run([compiler, 'effects', target, '--source', source, '--json'])
                record['leaf_effects:' + target] = result
                doc = json.loads(result['stdout'])
                handler_name = target.rsplit('.', 1)[-1]
                handlers = [handler for domain in doc.get('result', {}).get('synchronization_plan', {}).get('domains', [])
                            for handler in domain.get('handlers', [])
                            if handler.get('name') == handler_name]
                if result['exit'] != 0 or not handlers or any(
                        handler.get('normalized_effects') != expected_leaves for handler in handlers):
                    errors.append(f'{target}: expected state leaf effects {expected_leaves}')
            if 'synchronization' in case:
                result = run([compiler, 'effects', case['synchronization'],
                              '--source', source, '--json'])
                record['synchronization'] = result
                doc = json.loads(result['stdout'])
                handlers = [handler for instance in doc.get('result', {}).get('synchronization', {}).get('instances', [])
                            for handler in instance.get('handlers', [])]
                if result['exit'] != 0 or not any(
                        handler.get('write_set') == ['items'] and
                        any(a.get('mode') == 'EXCLUSIVE' for a in handler.get('acquisitions', []))
                        for handler in handlers):
                    errors.append('missing container WRITE / EXCLUSIVE state acquisition')
        records[name] = record
        if errors:
            failures.append(name)
        label = 'BASELINE' if args.baseline else 'FAIL' if errors else 'PASS'
        print(label + ' ' + name + (': ' + '; '.join(errors) if errors else ''), flush=True)
    (BUILD / ('baseline.json' if args.baseline else 'results.json')).write_text(
        json.dumps(records, indent=2) + '\n')
    print(f'{len(cases)} probes; {len(failures)} failures', flush=True)
    return 0 if args.baseline or not failures else 1


if __name__ == '__main__':
    raise SystemExit(main())
