# moss.path

`moss.path` provides pure lexical path manipulation. It has no operating-system
dependency and never opens, stats, resolves, or canonicalizes a filesystem path.

The contract is deliberately POSIX-oriented: `/` is the separator, a leading
`/` means absolute, and other paths are relative. Windows drive letters,
backslashes, UNC roots, symlink resolution, permissions, and case folding are
not interpreted. Paths are Moss `String` values; String lengths and indices in
the implementation are Unicode code-point counts.

## API

| Function | Contract |
| --- | --- |
| `is_absolute(value)` / `is_relative(value)` | Leading `/` classification. |
| `components(value)` | Nonempty raw components; repeated separators are omitted; `.` and `..` remain. |
| `component_count(value)` | Number of components in `components`. |
| `normalize(value)` | Lexically removes `.` and repeated separators, cancels `..` where possible, and preserves an absolute root. Empty/fully-cancelled relative paths become `.`. No filesystem access. |
| `join(left, right)` | Normalizes the joined path; an absolute right operand replaces the left operand. |
| `basename(value)` / `dirname(value)` | Normalized final component and parent. Root is `/`; a relative path without a parent has dirname `.`. |
| `extension(value)` | Final basename suffix after the last non-leading dot, without the dot. `.env` has no extension. |
| `stem(value)` | Basename with `extension` removed. |
| `has_prefix(value, prefix)` / `has_suffix(value, suffix)` | Normalized component-boundary relationships, not raw string prefixes. |

This package intentionally does not advertise metadata, existence checks, directory
listing, rename, atomic replacement, or any security guarantee. Those require host
filesystem primitives and are outside the current Moss v0.1 source surface.

```moss
import path

test "lexical paths":
  assertEqual(path.normalize("/var/./tmp/../log"), "/var/log")
  assertEqual(path.join("/var", "log/app.txt"), "/var/log/app.txt")
  assertEqual(path.basename("/var/log/app.txt"), "app.txt")
  assertEqual(path.dirname("/var/log/app.txt"), "/var/log")
  assertEqual(path.extension("app.tar.gz"), "gz")
```
