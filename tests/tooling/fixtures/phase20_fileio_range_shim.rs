// TEST ONLY -- NOT PHASE 20 RUNTIME.
//
// Minimal POSIX-backed shim for the Rust-facing names Agent D's codegen
// assumes Agent B's crate will supply: `FileIO` (open/read/write/sync/close)
// and `Range` (the borrowed-byte-view result of `.read()`, here with just
// enough surface -- `.length()` -- for Agent D's own chunk-pipeline
// ordering/termination tests). This is not Agent B's general FileIO
// ownership/effect implementation: no domain-field lock integration, no
// real borrow-scoped Range lifetime (it owns its bytes outright here, for
// simplicity), no batch reads, no (device, inode) uniqueness registry
// beyond what is needed for the one differential test that exercises it.
// It exists only so Agent D's own compiler tests can produce a runnable
// executable without Agent B's real runtime; it is never emitted by the
// compiler and must not be read as the normative Phase 20 FileIO/Range
// behavior (see docs/ROOT_RUNTIME_ABI.md, which this file is deliberately
// absent from).
struct Range {
    bytes: Vec<u8>,
}

impl Range {
    fn length(&self) -> i64 {
        self.bytes.len() as i64
    }
}

struct FileIO {
    file: std::fs::File,
}

impl FileIO {
    fn open(path: &str, mode: &str) -> FileIO {
        let mut options = std::fs::OpenOptions::new();
        match mode {
            "ro" => {
                options.read(true);
            }
            "rw" => {
                options.read(true).write(true);
            }
            "create" => {
                options.read(true).write(true).create(true);
            }
            _ => std::process::abort(),
        }
        let file = options.open(path).unwrap_or_else(|_| std::process::abort());
        FileIO { file }
    }

    fn read(&self, offset: i64, size: i64) -> Range {
        use std::os::unix::fs::FileExt;
        let want = if size < 0 { 0usize } else { size as usize };
        let mut buffer = vec![0u8; want];
        let mut total = 0usize;
        while total < buffer.len() {
            let got = self
                .file
                .read_at(&mut buffer[total..], (offset as u64) + (total as u64))
                .unwrap_or_else(|_| std::process::abort());
            if got == 0 {
                break;
            }
            total += got;
        }
        buffer.truncate(total);
        Range { bytes: buffer }
    }

    fn write(&self, offset: i64, data: &str) {
        use std::os::unix::fs::FileExt;
        self.file
            .write_all_at(data.as_bytes(), offset as u64)
            .unwrap_or_else(|_| std::process::abort());
    }

    fn sync(&self, dataonly: bool) {
        let result = if dataonly { self.file.sync_data() } else { self.file.sync_all() };
        result.unwrap_or_else(|_| std::process::abort());
    }

    fn close(self) {}
}
