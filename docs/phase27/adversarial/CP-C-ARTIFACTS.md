# Phase 27.2 CP-C records

The saved reviewer reports are copied byte-for-byte from their isolated review worktrees. The adjacent `tests/phase27/adversarial/cp-c-<sha>/` directories contain the probes copied byte-for-byte from the corresponding frozen review worktree when present.

Records for `e021d206` and `31c4261a` preserve their original `CURRENT_STATUS.md` outcome lines verbatim because neither isolated review worktree contains a standalone report. Their probe artifacts are copied from those frozen worktrees. No missing report prose is represented as an original reviewer document.

| Frozen candidate | Record | Probe directory |
| --- | --- | --- |
| `e021d206064d0d716f55e092d277027181f51f73` | [CP-C-e021d206.md](CP-C-e021d206.md) | [probes](../../../../tests/phase27/adversarial/cp-c-e021d206/) |
| `31c4261a121e1036e74092f717496e6fe95bdca8` | [CP-C-31c4261a.md](CP-C-31c4261a.md) | [probes](../../../../tests/phase27/adversarial/cp-c-31c4261a/) |
| `14c2ffbc09638c4810e9acd79ac9f2c3ba284d6f` | [CP-C-14c2ffbc.md](CP-C-14c2ffbc.md) | [probes](../../../../tests/phase27/adversarial/cp-c-14c2ffbc/) |
| `34e006000428bd3cd5cc61c5ea4b192296935ac3` | [CP-C-34e00600.md](CP-C-34e00600.md) | [probes](../../../../tests/phase27/adversarial/cp-c-34e00600/) |
| `24abc2b9e79f95c3a7f7f7e4222214691af32f56` | [CP-C-24abc2b9.md](CP-C-24abc2b9.md) | [probes](../../../../tests/phase27/adversarial/cp-c-24abc2b9/) |
| `4ad2f84e2ef52538ddd86b672f1abe7433648ec2` | [CP-C-4ad2f84e.md](CP-C-4ad2f84e.md) | [probes](../../../../tests/phase27/adversarial/cp-c-4ad2f84e/) |
