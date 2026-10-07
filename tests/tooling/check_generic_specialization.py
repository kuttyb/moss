#!/usr/bin/env python3
"""Concrete generic semantics: checked effects, native ABI and Fast Debug parity."""
import argparse
import json
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FIXTURES = REPO / 'tests/tooling/fixtures/generic_specialization'
BUILD = REPO / 'tmp/generic-specialization'


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
    (BUILD / 'input.txt').write_text('abcd')
    cases = json.loads((FIXTURES / 'manifest.json').read_text())
    records, failures = {}, []
    for name, case in cases.items():
        source = FIXTURES / (name + '.moss')
        record = {'check': run([compiler, 'check', source, '--json'])}
        record['lower'] = run([compiler, source, '-o', BUILD / (name + '.rs')])
        if record['lower']['exit'] == 0:
            record['native_compile'] = run(['rustc', '-O', '-D', 'warnings',
                                           BUILD / (name + '.rs'), '-o', BUILD / name])
            if record['native_compile']['exit'] == 0:
                record['native'] = run([BUILD / name])
        record['interp'] = run([compiler, 'run', '--interp', source])
        for query in ['type', 'effects', 'ownership', 'calls', 'inspect']:
            record[query] = run([compiler, query, case['target'], '--source', source, '--json'])
        records[name] = record
        errors = []
        if case['negative']:
            doc = json.loads(record['check']['stdout'])
            if doc.get('ok') or doc.get('error', {}).get('code') != case.get('code', 'TYPE_MISMATCH'):
                errors.append('expected structured rejection')
        else:
            for stage in ['check', 'lower', 'native_compile', 'native']:
                if record.get(stage, {}).get('exit') != 0:
                    errors.append(stage + ': ' + (record.get(stage, {}).get('stderr', '') or
                                                  record.get(stage, {}).get('stdout', ''))[:500])
                    break
            if record.get('native', {}).get('stdout') != case['stdout']:
                errors.append('native stdout differs from expected')
            if not case['fileio'] and not case.get('interp_unsupported'):
                if record['interp']['exit'] != 0 or record['interp']['stdout'] != case['stdout']:
                    errors.append('Fast Debug stdout differs from expected: ' + record['interp']['stderr'][:300])
                if record.get('native', {}).get('stdout') != record['interp']['stdout']:
                    errors.append('native/Fast Debug mismatch')
            for query in ['type', 'effects', 'ownership', 'calls', 'inspect']:
                if record[query]['exit'] != 0:
                    errors.append(query + ' query failed')
            if name in ['domain_write', 'domain_write_typed', 'domain_write_debug']:
                doc = json.loads(record['effects']['stdout'])
                sync = doc.get('result', {}).get('synchronization', {})
                handlers = [handler for instance in sync.get('instances', [])
                            for handler in instance.get('handlers', [])]
                if not any(handler.get('write_set') == ['items'] and
                           any(a.get('mode') == 'EXCLUSIVE' for a in handler.get('acquisitions', []))
                           for handler in handlers):
                    errors.append('missing items WRITE / EXCLUSIVE state acquisition')
            for target, expected in case.get('effects', {}).items():
                query = run([compiler, 'effects', target, '--source', source, '--json'])
                record['effects:' + target] = query
                doc = json.loads(query['stdout'])
                actual = [p['effect'] for p in doc.get('result', {}).get('ownership', [])]
                if actual != expected:
                    errors.append(f'{target} effects: expected {expected}, got {actual}')
        label = 'BASELINE' if args.baseline else 'FAIL' if errors else 'PASS'
        print(f'{label} {name}' + (': ' + '; '.join(errors) if errors else ''), flush=True)
        if errors:
            failures.append(name)
    if not args.baseline:
        generic_sync = json.loads(records['domain_write']['effects']['stdout']).get('result', {}).get('synchronization')
        typed_sync = json.loads(records['domain_write_typed']['effects']['stdout']).get('result', {}).get('synchronization')
        if generic_sync != typed_sync:
            failures.append('typed/generic synchronization mismatch')
    (BUILD / ('baseline-extra.json' if args.baseline else 'results.json')).write_text(
        json.dumps(records, indent=2) + '\n')
    print(f'{len(cases)} probes; {len(failures)} failures', flush=True)
    return 0 if args.baseline or not failures else 1


if __name__ == '__main__':
    raise SystemExit(main())
