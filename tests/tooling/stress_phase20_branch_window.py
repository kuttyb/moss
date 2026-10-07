#!/usr/bin/env python3
"""Repeat the isolated one-worker Branch-window binary from the Agent D suite.

First run check_phase20_executor_fileio.py to build phase20_chunk_handler_bin.
This binary checks compiler join placement; concurrent free-worker behavior
is covered separately by Agent C and the other Agent D fixtures.
"""
import argparse
from pathlib import Path
import subprocess


def main():
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', nargs='?',
                        default=repo / 'build/tests/phase20_chunk_handler_bin')
    parser.add_argument('--runs', type=int, default=1000)
    args = parser.parse_args()
    assert args.runs > 0, args.runs
    binary = Path(args.binary).resolve()
    expected = ['moss-branch publish'] * 4 + ['moss-branch run'] * 4
    for iteration in range(args.runs):
        result = subprocess.run([binary], cwd=repo, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, (iteration, result)
        lines = [line for line in result.stdout.splitlines()
                 if not line.startswith(('moss-solo ', 'moss-worker spawned '))]
        trace = [line for line in lines if line.startswith('moss-branch ')]
        program = [line for line in lines if not line.startswith('moss-branch ')]
        assert trace == expected, (iteration, trace, result.stderr)
        assert program == ['33', '44'], (iteration, program, result.stderr)
        if (iteration + 1) % 100 == 0:
            print(f'Branch-window stress: {iteration + 1}/{args.runs} passed', flush=True)
    print(f'Branch-window stress: {args.runs} runs, zero failures')


if __name__ == '__main__':
    main()
