#!/usr/bin/env python3
"""Reject new legacy expression-string parser call sites after Phase 27 baseline."""
import collections
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = ROOT / 'docs/phase27/STRING_DEBT_BASELINE.json'
PATTERN = re.compile(r'\b(?:parse_simple_call|split_top_level|strip_redundant_outer_parentheses|parse_enum_case_expression|eval)\s*\(')


def group(path):
    name = str(path)
    if 'semantic_checker' in name:
        return 'checker'
    if 'native_generator' in name:
        return 'emitter'
    if 'interpreter' in name:
        return 'interpreter'
    if name == 'src/moss.cpp':
        return 'reach_walk'
    return 'other'


def sites(commit=None):
    found = collections.Counter()
    names = (subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', commit, 'src'],
                                     cwd=ROOT, text=True).splitlines() if commit else
             [p.relative_to(ROOT).as_posix() for p in (ROOT / 'src').rglob('*')])
    for relative in sorted(names):
        path = ROOT / relative
        if path.suffix not in {'.cpp', '.hpp', '.inc'} or 'src/est/' in relative:
            continue
        consumer = group(relative)
        source = (subprocess.check_output(['git', 'show', f'{commit}:{relative}'],
                                          cwd=ROOT, text=True) if commit else path.read_text())
        for line in source.splitlines():
            for match in PATTERN.finditer(line):
                if match.group().startswith('eval') and consumer != 'interpreter':
                    continue
                found[(consumer, relative, line.strip(), match.group().split('(')[0].strip())] += 1
    return found


def main():
    actual = sites('0ecfa51' if '--write-baseline' in sys.argv else None)
    if '--write-baseline' in sys.argv:
        counts = collections.Counter()
        for (consumer, _, _, _), count in actual.items():
            counts[consumer] += count
        BASELINE.write_text(json.dumps({
            'source_commit': '0ecfa51',
            'counts_by_consumer': dict(sorted(counts.items())),
            'sites': [[*key, count] for key, count in sorted(actual.items())],
        }, indent=2) + '\n')
        return 0
    data = json.loads(BASELINE.read_text())
    baseline = {tuple(row[:4]): row[4] for row in data['sites']}
    added = [(key, count - baseline.get(key, 0)) for key, count in actual.items()
             if count > baseline.get(key, 0)]
    for key, count in added:
        print(f'new string parser call site ({count}): {key}', file=sys.stderr)
    return 1 if added else 0


if __name__ == '__main__':
    sys.exit(main())
