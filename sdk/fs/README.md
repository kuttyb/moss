# moss.fs

`moss.fs` is the small filesystem layer that is implementable on Moss v0.1's
current native `FileIO` surface. It deliberately keeps the existing contracts
visible instead of hiding them behind fake total APIs.

## Supported API

| Function | Contract |
| --- | --- |
| `read_prefix(path, offset) -> Vector[Int]` | Opens a regular file read-only and copies at most 4096 bytes starting at `offset`. A short vector indicates EOF. A negative offset raises `FsError.InvalidOffset`; open/read failures propagate the typed `FileError`; no sentinel is returned. |
| `read_all(path) -> Vector[Int]` | Repeats bounded 4096-byte reads from offset zero until a short read, returning an owned vector containing the complete file. Memory usage is proportional to file size. |
| `read_limited(path, max_bytes) -> Vector[Int]` | Repeats bounded reads while allocating at most `max_bytes` owned bytes. Empty files succeed with a zero limit; a nonempty file that would exceed the limit raises `FsError.LimitExceeded` before copying the overflowing Range. A negative limit raises `FsError.InvalidLimit`. |
| `copy_prefix(source, destination)` | Copies at most 4096 bytes from offset zero, syncs, and closes both files. The destination uses `create`: missing files are created, existing files are **not truncated**. Source and destination must not identify the same live file. |

The returned byte vectors are owned values; the borrowed `Range` never escapes
the FileIO lifetime. The fixed 4096-byte request is intentional: Moss v0.1
requires a statically provable finite bound for each FileIO request. `read_prefix`
is the bounded single-call API; `read_all` intentionally allocates in
proportion to the file size, while `read_limited` provides a caller-supplied
total allocation bound.

`FileIO` is synchronous native I/O for regular files. The package requires
native execution; Fast Debug does not implement FileIO. Callers should recover
the typed `FileError` variants documented by Phase 21, especially
`NotFound`, `PermissionDenied`, `NotRegularFile`, `InUse`, `Full`, and `IO`.
Callers should also recover `FsError.InvalidOffset` for caller-provided offsets,
`FsError.InvalidLimit` for negative limits, and `FsError.LimitExceeded` when a
limited read would exceed its total allocation bound.

## Deliberately unsupported host operations

The current compiler/runtime source surface exposes no supported filesystem
metadata/stat, existence query, directory listing, mkdir/rmdir, rename or
atomic replacement, truncate, file-size, current-directory, or permission API.
An unsuccessful `FileIO.open` is not a reliable existence check, and a sequence
of writes is not atomic replacement. `moss.fs` therefore does not publish
simulations or fake-success stubs for those operations. A future runtime
extension should specify signatures, error mapping, ownership/effects, and
native/Fast Debug behavior before these names are added.

## Example

```moss
import fs

fn main():
  try:
    data = fs.read_prefix("tmp/input.bin", 0)
    echo data |> count
  recover FileError.NotFound:
    echo "input is absent"
  recover:
    echo "file read failed"
```
