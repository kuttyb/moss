# Team H path/fs capability note

This leaf was designed against the live `moss-0.1` bootstrap at the Team H
backplane SHA. `./moss agent bootstrap --json` reports `phase20_fileio_executor`
and the source surface exposes only regular-file `FileIO` operations:
`open`, bounded `read`, bounded `write`, `sync`, `close`, and `chunks`.
`Range` is a scoped read-only borrow, not an owned byte vector.

## Implemented

- `moss.path` is a pure POSIX lexical package. It provides component parsing,
  normalization, joining, basename/dirname, extension/stem, and component-boundary
  prefix/suffix tests. It performs no host lookup or symlink resolution.
- `moss.fs` provides `read_prefix` (fixed 4096-byte request), `read_all` (repeated
  fixed 4096-byte requests until EOF), and `copy_prefix` (bounded read, explicit
  offset zero write, explicit sync, and close). Ranges are copied before FileIO
  teardown. Negative offsets raise the package's typed `FsError.InvalidOffset`.

## Confirmed boundaries

A helper using `file.read(offset, size)` with an `Int` size is rejected by the
checker as `FILEIO_UNBOUNDED_REQUEST`; fixed literal request sizes compile. A
dynamic String write payload is similarly rejected. This is why the package does
not publish an arbitrary-size write helper.

The bootstrap and documentation do not expose supported metadata/stat, reliable
existence, directory listing, mkdir/rmdir, rename, atomic replacement, truncate,
file-size, current-directory, or permission operations. An unsuccessful open is
not promoted to an existence predicate, and a write sequence is not advertised
as atomic replacement. These operations remain Team I/runtime interface requests,
not SDK stubs.

`margo test`'s ordinary package harness lacks the production FileIO host symbols
when linking a FileIO provider. Native and release acceptance therefore uses a
real external consumer (`sdk/fs/tests/check_native.py`), and source-free ABI-v9
acceptance uses `check_source_free.py`. Fast Debug is explicitly checked as an
unsupported FileIO execution mode.

Evidence commands:

```text
./moss agent bootstrap --json
./moss check tmp/host-fs-unbounded-probe.moss --json
python3 sdk/path/tests/check_differential.py
python3 sdk/path/tests/check_source_free.py
python3 sdk/fs/tests/check_negative.py
python3 sdk/fs/tests/check_native.py
python3 sdk/fs/tests/check_source_free.py
```
