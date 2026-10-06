#!/usr/bin/env python3
"""Observe real generated FileIO class guards under two executor Roots."""
from pathlib import Path
import re
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2] if len(sys.argv) > 2 else repo / 'tmp/phase20-domain-physical').resolve()
out.mkdir(parents=True, exist_ok=True)
fixture = repo / 'tests/tooling/fixtures/phase20_domain_fileio_native.moss'
rust = out / 'generated.rs'
subprocess.run([str(compiler), str(fixture), '-o', str(rust)], check=True,
               capture_output=True, text=True, timeout=120)
code = rust.read_text()
constructor = re.search(r'    let store = construct_store\([^\n]+;', code)
assert constructor, 'generated Store constructor missing'
harness = (repo / 'tests/tooling/fixtures/phase20_domain_field_physical.rs').read_text()
assert harness.count('CONSTRUCT_STORE') == 1
combined = out / 'physical.rs'
combined.write_text(code + harness.replace('CONSTRUCT_STORE', constructor.group().strip()))
binary = out / 'physical'
subprocess.run(['rustc', '--test', '-D', 'warnings', '--cfg', 'moss_perf',
                str(combined), '-o', str(binary)], check=True, timeout=120)
for case in ('shared', 'update', 'flush', 'close'):
    subprocess.run([str(binary), '--exact', 'phase20_domain_physical::' + case,
                    '--nocapture'], check=True, cwd=repo, timeout=30)
print('Phase 20 domain-field physical SHARED/SHARED and SHARED/EXCLUSIVE tests passed')
