// TEST ONLY -- NOT PHASE 20 RUNTIME.
//
// Minimal POSIX-backed stand-in for the subset of Agent B's FileIO/Range ABI
// that Agent D's codegen calls, with matching signatures so mismatches
// surface here: FileIO::open(&str, &str), read(i64, i64) -> Range,
// write<T: AsRef<[u8]>>(i64, T), sync(), sync_dataonly(), close(), and
// Range::len() -> i64. It deliberately exposes no `.length()` alias and no
// `sync(bool)`. Also provides the reserved compiler-internal Branch read
// token, moss_fileio_branch_read_borrow(&FileIO) -> MossFileIOReadBorrow:
// owned, Send + 'static, read-only, sharing the open file's backing without
// creating a second FileIO owner. No registry, Solo hooks, or domain-field
// locking. Never emitted by the compiler; not a proposal for Agent B.
pub struct Range {
    bytes: Vec<u8>,
}

impl Range {
    pub fn len(&self) -> i64 {
        self.bytes.len() as i64
    }
}

fn moss_test_pread(file: &std::fs::File, offset: i64, size: i64) -> Range {
    use std::os::unix::fs::FileExt;
    let want = if size < 0 { 0usize } else { size as usize };
    let mut buffer = vec![0u8; want];
    let mut total = 0usize;
    while total < buffer.len() {
        let got = file
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

pub struct FileIO {
    file: std::sync::Arc<std::fs::File>,
}

impl FileIO {
    pub fn open(path: &str, mode: &str) -> Self {
        let mut options = std::fs::OpenOptions::new();
        match mode {
            "ro" => { options.read(true); }
            "rw" => { options.read(true).write(true); }
            "create" => { options.read(true).write(true).create(true); }
            _ => std::process::abort(),
        }
        let file = options.open(path).unwrap_or_else(|_| std::process::abort());
        FileIO { file: std::sync::Arc::new(file) }
    }
    pub fn read(&self, offset: i64, size: i64) -> Range {
        moss_test_pread(&self.file, offset, size)
    }
    pub fn write<T: AsRef<[u8]>>(&self, offset: i64, data: T) {
        use std::os::unix::fs::FileExt;
        self.file.write_all_at(data.as_ref(), offset as u64).unwrap_or_else(|_| std::process::abort());
    }
    pub fn sync(&self) {
        self.file.sync_all().unwrap_or_else(|_| std::process::abort());
    }
    pub fn sync_dataonly(&self) {
        self.file.sync_data().unwrap_or_else(|_| std::process::abort());
    }
    pub fn close(&self) {}
}

pub struct MossFileIOReadBorrow {
    file: std::sync::Arc<std::fs::File>,
}

impl MossFileIOReadBorrow {
    pub fn read(&self, offset: i64, size: i64) -> Range {
        moss_test_pread(&self.file, offset, size)
    }
}

pub fn moss_fileio_branch_read_borrow(file: &FileIO) -> MossFileIOReadBorrow {
    MossFileIOReadBorrow { file: std::sync::Arc::clone(&file.file) }
}
