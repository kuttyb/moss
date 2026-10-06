#!/usr/bin/env python3
"""Deterministic FIFO admission test for the production fair leaf primitive."""
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1] if len(sys.argv) > 1 else repo / 'tmp/phase20-r5').resolve()
out.mkdir(parents=True, exist_ok=True)
executor = (repo / 'src/executor_runtime.hpp').read_text()
fileio = (repo / 'src/fileio_runtime.hpp').read_text()
begin = 'inline const char* fair_leaf_runtime_items() {\n  return R"EXECUTOR_RUST(\n'
end = '\n)EXECUTOR_RUST";\n}'
start = executor.index(begin) + len(begin)
fair_runtime = executor[start:executor.index(end, start)]
assert 'std::thread::park()' in fair_runtime and 'yield_now' not in fair_runtime
for token in ('inner: MossFairMutex<MossExecInner>',
              'inner: MossFairMutex<MossProcessRuntimeInner>',
              'branch_scopes: MossFairMutex<', 'worker_threads: MossFairMutex<'):
    assert token in executor, token
for token in ('inner: Arc<MossFairMutex<Option<FileIOInner>>>',
              'state: MossFairMutex<RegistryState>',
              'MOSS_SOLO_ENTER_HOOK: MossFairMutex<',
              'MOSS_SOLO_LEAVE_HOOK: MossFairMutex<'):
    assert token in fileio, token
fixture = (repo / 'tests/tooling/fixtures/phase20_fair_leaf_harness.rs').read_text()
assert fixture.count('FAIR_LEAF_RUNTIME') == 1
source = out / 'fair_leaf.rs'
source.write_text(fixture.replace('FAIR_LEAF_RUNTIME', fair_runtime))
binary = out / 'fair_leaf'
subprocess.run(['rustc', '-D', 'warnings', '--cfg', 'moss_perf',
                str(source), '-o', str(binary)], check=True, timeout=120)
subprocess.run([str(binary)], check=True, cwd=repo, timeout=30)
