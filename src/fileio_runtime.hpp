#pragma once

namespace moss {
// Phase 20 Agent D -- minimal root-local FileIO runtime support.
//
// This is an intentionally narrow adapter, not Agent A/B's general FileIO
// runtime: it supports exactly the operations compiler-lowered chunk
// pipelines and plain root-local open/read/write/sync/close statements need
// (docs/MOSS_PHASE_20_FILE_IO_AND_EXECUTORS.md secs. 8-13). Range/RangeBatch
// borrow scoping, batch reads, domain-field lock integration, and the full
// FileIO ownership/escape checker remain out of scope here and belong to
// Agent A/B's eventual implementation; this adapter exists so chunk-pipeline
// eligibility/sequential-lowering has something real to execute against.
//
// Chunk bytes are surfaced to Moss as an ordinary `string` (Rust `String`),
// decoded with lossy UTF-8. Phase 20's `Range` borrowed-byte-view type is not
// implemented; this is a documented simplification, not the normative
// representation.
inline const char* fileio_runtime_rust() {
  return R"RUST(
static MOSS_FILEIO_REGISTRY: std::sync::Mutex<Vec<(u64, u64)>> =
    std::sync::Mutex::new(vec![]);
struct MossFileIO {
    __moss_file: std::fs::File,
    __moss_key: (u64, u64),
}
impl MossFileIO {
    fn __moss_open(path: &str, mode: &str) -> MossFileIO {
        use std::os::unix::fs::MetadataExt;
        let mut options = std::fs::OpenOptions::new();
        match mode {
            "ro" => { options.read(true); }
            "rw" => { options.read(true).write(true); }
            "create" => { options.read(true).write(true).create(true); }
            _ => std::process::abort(),
        }
        let file = options.open(path).unwrap_or_else(|_| std::process::abort());
        let metadata = file.metadata().unwrap_or_else(|_| std::process::abort());
        if !metadata.is_file() { std::process::abort(); }
        let key = (metadata.dev(), metadata.ino());
        {
            let mut registry = MOSS_FILEIO_REGISTRY
                .lock()
                .unwrap_or_else(|_| std::process::abort());
            if registry.contains(&key) { std::process::abort(); }
            registry.push(key);
        }
        MossFileIO { __moss_file: file, __moss_key: key }
    }
    fn __moss_read(&self, offset: i64, size: i64) -> String {
        use std::os::unix::fs::FileExt;
        let want = if size < 0 { 0usize } else { size as usize };
        let mut buffer = vec![0u8; want];
        let mut total = 0usize;
        while total < buffer.len() {
            let read = self
                .__moss_file
                .read_at(&mut buffer[total..], (offset as u64) + (total as u64))
                .unwrap_or_else(|_| std::process::abort());
            if read == 0 { break; }
            total += read;
        }
        buffer.truncate(total);
        String::from_utf8_lossy(&buffer).into_owned()
    }
    fn __moss_write(&self, offset: i64, data: &str) {
        use std::os::unix::fs::FileExt;
        self.__moss_file
            .write_all_at(data.as_bytes(), offset as u64)
            .unwrap_or_else(|_| std::process::abort());
    }
    fn __moss_sync(&self, dataonly: bool) {
        let result = if dataonly {
            self.__moss_file.sync_data()
        } else {
            self.__moss_file.sync_all()
        };
        result.unwrap_or_else(|_| std::process::abort());
    }
    fn __moss_close(self) {
        let mut registry = MOSS_FILEIO_REGISTRY
            .lock()
            .unwrap_or_else(|_| std::process::abort());
        registry.retain(|entry| *entry != self.__moss_key);
    }
}
)RUST";
}
}  // namespace moss
