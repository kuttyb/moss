#pragma once

namespace moss {

inline bool contains_fileio_token(const std::string& s, const char* token_str) {
  std::string_view token = token_str;
  size_t pos = 0;
  while ((pos = s.find(token, pos)) != std::string::npos) {
    bool before_ok = (pos == 0 || !(std::isalnum(static_cast<unsigned char>(s[pos - 1])) || s[pos - 1] == '_'));
    size_t end = pos + token.size();
    bool after_ok = (end == s.size() || !(std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '_'));
    if (before_ok && after_ok) return true;
    pos = end;
  }
  return false;
}

inline bool is_fileio_type(const std::string& type_str) {
  static const char* const types[] = {"FileIO", "RangeBatch", "Range"};
  for (const char* const t : types) {
    if (contains_fileio_token(type_str, t)) return true;
  }
  return false;
}

inline bool expr_uses_fileio(const std::string& s) {
  if (s.find("FileIO.") != std::string::npos || s.find("FileIO(") != std::string::npos ||
      s.find("RangeBatch.") != std::string::npos || s.find("RangeBatch(") != std::string::npos ||
      s.find("Range.empty") != std::string::npos || s.find("Range.from_") != std::string::npos ||
      s.find("Range::") != std::string::npos || s.find("FileIO::") != std::string::npos) {
    return true;
  }
  return false;
}

inline bool statement_uses_fileio(const Stmt& s) {
  return expr_uses_fileio(s.text) || expr_uses_fileio(s.a) || expr_uses_fileio(s.b);
}

inline bool program_uses_fileio(const Program& p) {
  if (p.main) {
    for (const auto& s : p.main->body) {
      if (statement_uses_fileio(s)) return true;
    }
  }
  for (const auto& obj : p.objects) {
    for (const auto& f : obj.fields) {
      if (is_fileio_type(f.type) || expr_uses_fileio(f.init)) return true;
    }
    for (const auto& m : obj.methods) {
      if (m.return_type && is_fileio_type(*m.return_type)) return true;
      for (const auto& param : m.params) {
        if (is_fileio_type(param.type)) return true;
      }
      for (const auto& s : m.body) {
        if (statement_uses_fileio(s)) return true;
      }
    }
  }
  for (const auto& d : p.domains) {
    for (const auto& f : d.state) {
      if (is_fileio_type(f.type) || expr_uses_fileio(f.init)) return true;
    }
    for (const auto& h : d.handlers) {
      if (h.reply_type && is_fileio_type(*h.reply_type)) return true;
      for (const auto& param : h.params) {
        if (is_fileio_type(param.type)) return true;
      }
      for (const auto& s : h.body) {
        if (statement_uses_fileio(s)) return true;
      }
    }
  }
  for (const auto& fn : p.functions) {
    if (fn.return_type && is_fileio_type(*fn.return_type)) return true;
    for (const auto& param : fn.params) {
      if (is_fileio_type(param.type)) return true;
    }
    for (const auto& s : fn.body) {
      if (statement_uses_fileio(s)) return true;
    }
  }
  for (const auto& e : p.enums) {
    for (const auto& c : e.cases) {
      for (const auto& f : c.fields) {
        if (is_fileio_type(f.type)) return true;
      }
    }
  }
  for (const auto& t : p.tests) {
    for (const auto& s : t.body) {
      if (statement_uses_fileio(s)) return true;
    }
  }
  for (const auto& b : p.benchmarks) {
    for (const auto& s : b.body) {
      if (statement_uses_fileio(s)) return true;
    }
  }
  return false;
}

inline const char* fileio_runtime_rust() {
  return R"RUST(
// Moss Phase 20 FileIO Runtime (Linux x86_64 / aarch64 native)
#[cfg(not(all(
    target_os = "linux",
    any(
        target_arch = "x86_64",
        target_arch = "aarch64"
    )
)))]
compile_error!("Moss Phase 20 FileIO runtime is only supported on Linux x86_64 and aarch64.");

#[allow(dead_code)]
pub mod moss_fileio {
    use std::convert::TryFrom;
    use std::os::unix::fs::MetadataExt;
    use std::os::unix::io::{FromRawFd, IntoRawFd, RawFd};
    use std::sync::Arc;
    #[cfg(any(test, moss_perf))]
    use std::sync::Mutex;
    use super::MossFairMutex;
    use std::path::Path;
    use std::ffi::CString;

    pub mod sys {
        extern "Rust" {
            pub fn moss_solo_enter(reason: &str);
            pub fn moss_solo_leave(reason: &str);
        }
        extern "C" {
            pub fn moss_fileio_registry_claim(dev: u64, ino: u64) -> bool;
            pub fn moss_fileio_registry_release(dev: u64, ino: u64);
            #[cfg(any(test, moss_perf))]
            pub fn moss_fileio_registry_contains(dev: u64, ino: u64) -> bool;
            #[cfg(any(test, moss_perf))]
            pub fn moss_fileio_registry_reset();
        }
    }

    pub mod posix {
        pub const O_RDONLY: i32 = 0;
        pub const O_WRONLY: i32 = 1;
        pub const O_RDWR: i32 = 2;
        pub const O_CREAT: i32 = 0x40;
        pub const O_EXCL: i32 = 0x80;
        pub const O_NONBLOCK: i32 = 0x800;
        pub const O_DIRECTORY: i32 = 0x10000;
        pub const O_CLOEXEC: i32 = 0x80000;

        pub const F_GETFL: i32 = 3;
        pub const F_SETFL: i32 = 4;

        pub const EINTR: i32 = 4;
        pub const EPERM: i32 = 1;
        pub const ENOENT: i32 = 2;
        pub const EACCES: i32 = 13;
        pub const EBUSY: i32 = 16;
        pub const EEXIST: i32 = 17;
        pub const ENOTDIR: i32 = 20;
        pub const EISDIR: i32 = 21;
        pub const ETXTBSY: i32 = 26;
        pub const ENOSPC: i32 = 28;
        pub const EROFS: i32 = 30;
        pub const EDQUOT: i32 = 122;

        #[repr(C)]
        pub struct statfs {
            pub f_type: i64,
            pub f_bsize: i64,
            pub f_blocks: u64,
            pub f_bfree: u64,
            pub f_bavail: u64,
            pub f_files: u64,
            pub f_ffree: u64,
            pub f_fsid: [i32; 2],
            pub f_namelen: i64,
            pub f_frsize: i64,
            pub f_flags: i64,
            pub f_spare: [i64; 4],
        }

        extern "C" {
            pub fn open(path: *const i8, oflag: i32, ...) -> i32;
            pub fn close(fd: i32) -> i32;
            pub fn pread(fd: i32, buf: *mut u8, count: usize, offset: i64) -> isize;
            pub fn pwrite(fd: i32, buf: *const u8, count: usize, offset: i64) -> isize;
            pub fn fsync(fd: i32) -> i32;
            pub fn fdatasync(fd: i32) -> i32;
            pub fn fcntl(fd: i32, cmd: i32, ...) -> i32;
            pub fn fstatfs(fd: i32, buf: *mut statfs) -> i32;
        }
    }

    // Phase 21's checked FileIO origin type. Generated fallible bodies convert
    // this closed runtime result into their specialization-local ABI-v8 frame;
    // this enum never crosses a provider boundary as a generic Result.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum MossFileError {
        NotFound,
        PermissionDenied,
        NotRegularFile,
        InUse,
        Full,
        IO,
    }

    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum MossFileIoFaultSite {
        OpenBefore,
        OpenAfterDescriptor,
        OpenFlagClear,
        ParentDirectoryOpen,
        ReadBefore,
        ReadAfterPrefix,
        WriteBefore,
        WriteAfterPrefix,
        FileSync,
        ParentDirectorySync,
        ParentDirectoryClose,
        FileClose,
    }

    #[cfg(any(test, moss_perf))]
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    struct MossFileIoFault {
        site: MossFileIoFaultSite,
        cause: MossFileError,
        // Read/write prefix length for the two AfterPrefix sites.
        progress: usize,
    }

    #[cfg(any(test, moss_perf))]
    static MOSS_FILEIO_FAULT: Mutex<Option<MossFileIoFault>> = Mutex::new(None);

    #[inline]
    fn fault_cause_allowed(site: MossFileIoFaultSite, cause: MossFileError) -> bool {
        use self::MossFileError::*;
        use self::MossFileIoFaultSite::*;
        match site {
            OpenBefore | OpenAfterDescriptor => true,
            OpenFlagClear => cause == IO,
            ParentDirectoryOpen => matches!(cause, NotFound | PermissionDenied | NotRegularFile | IO),
            ReadBefore | ReadAfterPrefix => cause == IO,
            WriteBefore | WriteAfterPrefix | FileSync | ParentDirectorySync =>
                matches!(cause, Full | IO),
            ParentDirectoryClose | FileClose => cause == IO,
        }
    }

    #[cfg(any(test, moss_perf))]
    pub fn moss_inject_fileio_fault(
        site: MossFileIoFaultSite,
        cause: MossFileError,
        progress: usize,
    ) {
        if !fault_cause_allowed(site, cause) {
            panic!("invalid FileIO fault cause {:?} for site {:?}", cause, site);
        }
        let mut fault = MOSS_FILEIO_FAULT.lock().unwrap_or_else(|_| std::process::abort());
        *fault = Some(MossFileIoFault { site, cause, progress });
    }

    #[cfg(any(test, moss_perf))]
    pub fn moss_clear_fileio_fault() {
        let mut fault = MOSS_FILEIO_FAULT.lock().unwrap_or_else(|_| std::process::abort());
        *fault = None;
    }

    #[inline]
    fn take_fileio_fault(site: MossFileIoFaultSite) -> Option<(MossFileError, usize)> {
        #[cfg(any(test, moss_perf))]
        {
            let mut slot = MOSS_FILEIO_FAULT.lock().unwrap_or_else(|_| std::process::abort());
            if slot.as_ref().map_or(false, |fault| fault.site == site) {
                return slot.take().map(|fault| (fault.cause, fault.progress));
            }
        }
        #[cfg(not(any(test, moss_perf)))]
        let _ = site;
        None
    }

    #[inline]
    fn map_os_error(err: &std::io::Error, open_site: bool) -> MossFileError {
        match err.raw_os_error() {
            Some(posix::ENOENT) | Some(posix::ENOTDIR) if open_site => MossFileError::NotFound,
            Some(posix::EACCES) | Some(posix::EPERM) | Some(posix::EROFS) =>
                MossFileError::PermissionDenied,
            Some(posix::ENOSPC) | Some(posix::EDQUOT) => MossFileError::Full,
            Some(posix::EISDIR) if open_site => MossFileError::NotRegularFile,
            Some(posix::EBUSY) | Some(posix::ETXTBSY) if open_site => MossFileError::InUse,
            _ => MossFileError::IO,
        }
    }

    #[inline]
    fn map_data_os_error(err: &std::io::Error, full_allowed: bool) -> MossFileError {
        match err.raw_os_error() {
            Some(posix::ENOSPC) | Some(posix::EDQUOT) if full_allowed => MossFileError::Full,
            _ => MossFileError::IO,
        }
    }

    #[cfg(any(test, moss_perf))]
    pub fn moss_fileio_map_open_errno_for_test(errno: i32) -> MossFileError {
        map_os_error(&std::io::Error::from_raw_os_error(errno), true)
    }

    #[cfg(any(test, moss_perf))]
    pub fn moss_fileio_map_data_errno_for_test(
        errno: i32,
        full_allowed: bool,
    ) -> MossFileError {
        map_data_os_error(
            &std::io::Error::from_raw_os_error(errno),
            full_allowed,
        )
    }

    #[cold]
    fn abort_fileio_error(operation: &str, cause: MossFileError) -> ! {
        match cause {
            MossFileError::NotRegularFile =>
                eprintln!("[moss-fileio] error: path is not a regular file"),
            MossFileError::InUse =>
                eprintln!("[moss-fileio] error: duplicate live FileIO (InUse)"),
            _ => eprintln!("[moss-fileio] error: {} failed: {:?}", operation, cause),
        }
        std::process::abort();
    }

    #[cfg(any(test, moss_perf))]
    static MOSS_CLOSE_OVERRIDE: Mutex<Option<fn(RawFd) -> i32>> = Mutex::new(None);

    #[cfg(any(test, moss_perf))]
    pub fn moss_set_close_override(f: fn(RawFd) -> i32) {
        if let Ok(mut g) = MOSS_CLOSE_OVERRIDE.lock() {
            *g = Some(f);
        }
    }

    #[cfg(any(test, moss_perf))]
    pub fn moss_clear_close_override() {
        if let Ok(mut g) = MOSS_CLOSE_OVERRIDE.lock() {
            *g = None;
        }
    }

    #[inline]
    pub unsafe fn sys_close(fd: RawFd) -> i32 {
        #[cfg(any(test, moss_perf))]
        {
            let override_fn = {
                if let Ok(g) = MOSS_CLOSE_OVERRIDE.lock() {
                    *g
                } else {
                    None
                }
            };
            if let Some(f) = override_fn {
                return f(fd);
            }
        }
        posix::close(fd)
    }

    #[inline]
    pub fn moss_check_filesystem_diagnostic(fd: RawFd, path: &str) {
        #[cfg(target_os = "linux")]
        unsafe{
            let mut st: posix::statfs = std::mem::zeroed();
            let guard = SoloGuard::new("fstatfs");
            let res = posix::fstatfs(fd, &mut st);
            drop(guard);
            if res == 0 {
                const FUSE_SUPER_MAGIC: i64 = 0x65735546;
                if st.f_type == FUSE_SUPER_MAGIC {
                    eprintln!("[moss-diagnostic] warning: path '{}' resides on a FUSE filesystem; Solo scheduling guarantees may be affected", path);
                }
            }
        }
        #[cfg(not(target_os = "linux"))]
        let _ = (fd, path);
    }

    #[inline]
    pub fn solo_enter(reason: &str) {
        unsafe { sys::moss_solo_enter(reason) };
    }

    #[inline]
    pub fn solo_leave(reason: &str) {
        unsafe { sys::moss_solo_leave(reason) };
    }

    #[inline]
    pub fn moss_solo_enter(reason: &str) {
        solo_enter(reason);
    }

    #[inline]
    pub fn moss_solo_leave(reason: &str) {
        solo_leave(reason);
    }

    pub struct SoloGuard<'a> {
        reason: &'a str,
    }

    impl<'a> SoloGuard<'a> {
        #[inline]
        pub fn new(reason: &'a str) -> Self {
            solo_enter(reason);
            SoloGuard { reason }
        }
    }

    impl<'a> Drop for SoloGuard<'a> {
        #[inline]
        fn drop(&mut self) {
            solo_leave(self.reason);
        }
    }

    #[cfg(any(test, moss_perf))]
    static MOSS_SYNC_HOOK: Mutex<Option<fn(&str, RawFd, &str)>> = Mutex::new(None);

    #[inline]
    pub fn moss_sync_event(event: &str, fd: RawFd, path: &str) {
        #[cfg(any(test, moss_perf))]
        {
            let hook = {
                if let Ok(guard) = MOSS_SYNC_HOOK.lock() {
                    *guard
                } else {
                    None
                }
            };
            if let Some(h) = hook {
                h(event, fd, path);
            }
        }
        #[cfg(not(any(test, moss_perf)))]
        {
            let _ = (event, fd, path);
        }
    }

    #[cfg(any(test, moss_perf))]
    pub fn moss_set_sync_hook(hook: fn(&str, RawFd, &str)) {
        if let Ok(mut guard) = MOSS_SYNC_HOOK.lock() {
            *guard = Some(hook);
        }
    }

    #[cfg(any(test, moss_perf))]
    pub fn moss_clear_sync_hook() {
        if let Ok(mut guard) = MOSS_SYNC_HOOK.lock() {
            *guard = None;
        }
    }

    #[inline]
    pub fn moss_fileio_registry_claim(dev: u64, ino: u64) -> bool {
        unsafe { sys::moss_fileio_registry_claim(dev, ino) }
    }

    #[inline]
    pub fn moss_fileio_registry_release(dev: u64, ino: u64) {
        unsafe { sys::moss_fileio_registry_release(dev, ino) }
    }

    #[cfg(any(test, moss_perf))]
    #[inline]
    pub fn moss_fileio_registry_contains(dev: u64, ino: u64) -> bool {
        unsafe { sys::moss_fileio_registry_contains(dev, ino) }
    }

    #[cfg(any(test, moss_perf))]
    #[inline]
    pub fn moss_fileio_registry_reset() {
        unsafe { sys::moss_fileio_registry_reset() }
    }

    #[derive(Debug, Clone)]
    pub struct Range {
        buf: Arc<Vec<u8>>,
        start: usize,
        len: usize,
    }

    impl PartialEq for Range {
        fn eq(&self, other: &Self) -> bool {
            self.as_slice() == other.as_slice()
        }
    }

    impl Eq for Range {}

    impl Range {
        pub fn empty() -> Self {
            Range {
                buf: Arc::new(vec![]),
                start: 0,
                len: 0,
            }
        }

        pub fn from_vec(v: Vec<u8>) -> Self {
            let len = v.len();
            Range {
                buf: Arc::new(v),
                start: 0,
                len,
            }
        }

        pub fn from_slice(s: &[u8]) -> Self {
            Self::from_vec(s.to_vec())
        }

        pub fn from_str(s: &str) -> Self {
            Self::from_vec(s.as_bytes().to_vec())
        }

        pub fn len(&self) -> i64 {
            i64::try_from(self.len).unwrap_or(i64::MAX)
        }

        pub fn is_empty(&self) -> bool {
            self.len == 0
        }

        pub fn as_slice(&self) -> &[u8] {
            &self.buf[self.start..(self.start + self.len)]
        }

        pub fn as_str(&self) -> &str {
            std::str::from_utf8(self.as_slice()).unwrap_or("")
        }

        pub fn get(&self, index: i64) -> i64 {
            let idx = match usize::try_from(index) {
                Ok(i) if i < self.len => i,
                _ => {
                    eprintln!("[moss-fileio] error: Range index {} out of bounds for length {}", index, self.len);
                    std::process::abort();
                }
            };
            self.buf[self.start + idx] as i64
        }

        pub fn bytes(&self) -> impl Iterator<Item = i64> + '_ {
            self.as_slice().iter().map(|byte| *byte as i64)
        }

        pub fn slice(&self, start: i64, length: i64) -> Self {
            if start < 0 || length <= 0 {
                return Range::empty();
            }
            let st = match usize::try_from(start) {
                Ok(s) if s < self.len => s,
                _ => return Range::empty(),
            };
            let req_len = match usize::try_from(length) {
                Ok(l) => l,
                _ => return Range::empty(),
            };
            let actual_len = std::cmp::min(req_len, self.len - st);
            Range {
                buf: self.buf.clone(),
                start: self.start + st,
                len: actual_len,
            }
        }

        pub fn to_bytes(&self) -> Vec<u8> {
            self.as_slice().to_vec()
        }

        pub fn to_vec(&self) -> Vec<i64> {
            let mut result = Vec::with_capacity(self.len);
            for &b in self.as_slice() {
                result.push(b as i64);
            }
            result
        }

        pub fn to_string(&self) -> String {
            String::from_utf8_lossy(self.as_slice()).into_owned()
        }
    }

    impl AsRef<[u8]> for Range {
        fn as_ref(&self) -> &[u8] {
            self.as_slice()
        }
    }

    impl std::fmt::Display for Range {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str("b\"")?;
            for byte in self.as_slice() {
                for escaped in std::ascii::escape_default(*byte) {
                    f.write_str(&(escaped as char).to_string())?;
                }
            }
            f.write_str("\"")
        }
    }

    #[derive(Debug, Clone, PartialEq, Eq)]
    pub struct RangeBatch {
        ranges: Vec<Range>,
    }

    impl RangeBatch {
        pub fn new(ranges: Vec<Range>) -> Self {
            RangeBatch { ranges }
        }

        pub fn len(&self) -> i64 {
            i64::try_from(self.ranges.len()).unwrap_or(i64::MAX)
        }

        pub fn is_empty(&self) -> bool {
            self.ranges.is_empty()
        }

        pub fn get(&self, index: i64) -> Range {
            let idx = match usize::try_from(index) {
                Ok(i) if i < self.ranges.len() => i,
                _ => {
                    eprintln!("[moss-fileio] error: RangeBatch index {} out of bounds for length {}", index, self.ranges.len());
                    std::process::abort();
                }
            };
            self.ranges[idx].clone()
        }

        pub fn ranges(&self) -> &[Range] {
            &self.ranges
        }

        pub fn into_ranges(self) -> Vec<Range> {
            self.ranges
        }
    }

    struct FileIOInner {
        fd: RawFd,
        parent_dir_fd: Option<RawFd>,
        dev: u64,
        ino: u64,
        created: bool,
        dir_obligation: bool,
        is_closed: bool,
        path: String,
        parent_path: String,
    }

    // `inner` is shared only with compiler-created branch read borrows
    // (MossFileIOReadBorrow). The FileIO remains the sole owner: only it can
    // write, sync, or close, and a borrow observes close and fails closed.
    pub struct FileIO {
        inner: Arc<MossFairMutex<Option<FileIOInner>>>,
    }

    impl Default for FileIO {
        fn default() -> Self {
            FileIO {
                inner: Arc::new(MossFairMutex::new(None)),
            }
        }
    }

    impl std::fmt::Debug for FileIO {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            let snapshot = {
                let guard = self.inner.lock();
                guard.as_ref().map(|inner| (inner.path.clone(), inner.dev, inner.ino, !inner.is_closed))
            };
            if let Some((path, dev, ino, open)) = snapshot {
                write!(f, "FileIO(path: '{}', dev: {}, ino: {}, open: {})", path, dev, ino, open)
            } else {
                write!(f, "FileIO(<uninitialized>)")
            }
        }
    }

    impl FileIO {
        pub fn open(path: &str, mode: &str) -> Self {
            Self::open_checked(path, mode)
                .unwrap_or_else(|cause| abort_fileio_error("open", cause))
        }

        pub fn open_checked(path: &str, mode: &str) -> Result<Self, MossFileError> {
            // An interior NUL is a caller-supplied path that cannot be passed
            // to POSIX. It is not runtime misuse: keep it on FileIO's generic
            // cause-based IO channel rather than turning it into a panic.
            let c_path = CString::new(path).map_err(|_| MossFileError::IO)?;
            if let Some((cause, _)) = take_fileio_fault(MossFileIoFaultSite::OpenBefore) {
                return Err(cause);
            }

            let (fd, created) = match mode {
                "create" => {
                    let guard = SoloGuard::new("open_create");
                    let fd = unsafe{
                        posix::open(
                            c_path.as_ptr(),
                            posix::O_RDWR | posix::O_CREAT | posix::O_EXCL | posix::O_NONBLOCK | posix::O_CLOEXEC,
                            0o666,
                        )
                    };
                    let open_err = if fd < 0 {
                        Some(std::io::Error::last_os_error())
                    } else {
                        None
                    };
                    drop(guard);
                    if fd >= 0 {
                        (fd, true)
                    } else {
                        let err = open_err.unwrap_or_else(std::io::Error::last_os_error);
                        if err.raw_os_error() == Some(posix::EEXIST) {
                            let guard = SoloGuard::new("open_existing");
                            let res = unsafe{
                                posix::open(
                                    c_path.as_ptr(),
                                    posix::O_RDWR | posix::O_NONBLOCK | posix::O_CLOEXEC,
                                )
                            };
                            let open_err = if res < 0 {
                                Some(std::io::Error::last_os_error())
                            } else {
                                None
                            };
                            drop(guard);
                            if res < 0 {
                                let err = open_err.unwrap_or_else(std::io::Error::last_os_error);
                                return Err(map_os_error(&err, true));
                            }
                            (res, false)
                        } else {
                            return Err(map_os_error(&err, true));
                        }
                    }
                }
                "ro" => {
                    let guard = SoloGuard::new("open_ro");
                    let fd = unsafe{
                        posix::open(
                            c_path.as_ptr(),
                            posix::O_RDONLY | posix::O_NONBLOCK | posix::O_CLOEXEC,
                        )
                    };
                    let open_err = if fd < 0 {
                        Some(std::io::Error::last_os_error())
                    } else {
                        None
                    };
                    drop(guard);
                    if fd < 0 {
                        let err = open_err.unwrap_or_else(std::io::Error::last_os_error);
                        return Err(map_os_error(&err, true));
                    }
                    (fd, false)
                }
                "rw" => {
                    let guard = SoloGuard::new("open_rw");
                    let fd = unsafe{
                        posix::open(
                            c_path.as_ptr(),
                            posix::O_RDWR | posix::O_NONBLOCK | posix::O_CLOEXEC,
                        )
                    };
                    let open_err = if fd < 0 {
                        Some(std::io::Error::last_os_error())
                    } else {
                        None
                    };
                    drop(guard);
                    if fd < 0 {
                        let err = open_err.unwrap_or_else(std::io::Error::last_os_error);
                        return Err(map_os_error(&err, true));
                    }
                    (fd, false)
                }
                _ => {
                    eprintln!("[moss-fileio] error: unsupported open mode '{}'; expected 'ro', 'rw', or 'create'", mode);
                    std::process::abort();
                }
            };

            if let Some((cause, _)) = take_fileio_fault(MossFileIoFaultSite::OpenAfterDescriptor) {
                let guard = SoloGuard::new("cleanup_close");
                let _ = unsafe{ sys_close(fd) };
                drop(guard);
                return Err(cause);
            }

            // Validate regular file via metadata and extract (dev, ino)
            let (dev, ino) = {
                let guard = SoloGuard::new("fstat");
                let file_temp = unsafe{ std::fs::File::from_raw_fd(fd) };
                let meta_res = file_temp.metadata();
                let _ = file_temp.into_raw_fd();
                drop(guard);
                match meta_res {
                    Ok(meta) => {
                        if !meta.file_type().is_file() {
                            let cguard = SoloGuard::new("cleanup_close");
                            unsafe{ sys_close(fd); }
                            drop(cguard);
                            return Err(MossFileError::NotRegularFile);
                        }
                        (meta.dev(), meta.ino())
                    }
                    Err(err) => {
                        let cguard = SoloGuard::new("cleanup_close");
                        unsafe{ sys_close(fd); }
                        drop(cguard);
                        return Err(map_os_error(&err, true));
                    }
                }
            };

            // Clear O_NONBLOCK before exposing the owner. A failure here is
            // an open failure, not permission to return a descriptor with
            // different blocking behavior. No inode claim exists yet.
            let injected_flag_error = take_fileio_fault(MossFileIoFaultSite::OpenFlagClear);
            let flags = if injected_flag_error.is_some() {
                -1
            } else {
                unsafe{ posix::fcntl(fd, posix::F_GETFL) }
            };
            let flag_clear_failed = flags < 0 ||
                unsafe{ posix::fcntl(fd, posix::F_SETFL, flags & !posix::O_NONBLOCK) } < 0;
            if flag_clear_failed {
                let cguard = SoloGuard::new("cleanup_close");
                let _ = unsafe{ sys_close(fd) };
                drop(cguard);
                return Err(MossFileError::IO);
            }

            // Check filesystem diagnostic
            moss_check_filesystem_diagnostic(fd, path);

            // Handle parent directory descriptor for create obligation
            let mut parent_dir_fd = None;
            let mut parent_path_str = String::new();
            if created {
                let p = Path::new(path);
                let parent = p.parent().unwrap_or_else(|| Path::new("."));
                let parent_str = if parent.as_os_str().is_empty() { "." } else { parent.to_str().unwrap_or(".") };
                parent_path_str = parent_str.to_string();
                let c_parent = CString::new(parent_str).unwrap_or_else(|_| std::process::abort());
                if let Some((cause, _)) = take_fileio_fault(MossFileIoFaultSite::ParentDirectoryOpen) {
                    let cguard = SoloGuard::new("cleanup_close");
                    let _ = unsafe{ sys_close(fd) };
                    drop(cguard);
                    return Err(cause);
                }
                let guard = SoloGuard::new("open_parent_dir");
                let pfd = unsafe{
                    posix::open(
                        c_parent.as_ptr(),
                        posix::O_RDONLY | posix::O_DIRECTORY | posix::O_CLOEXEC,
                        0,
                    )
                };
                let parent_open_err = if pfd < 0 {
                    Some(std::io::Error::last_os_error())
                } else {
                    None
                };
                drop(guard);
                if pfd >= 0 {
                    parent_dir_fd = Some(pfd);
                } else {
                    let cguard = SoloGuard::new("cleanup_close");
                    unsafe{ sys_close(fd); }
                    drop(cguard);
                    let err = parent_open_err.unwrap_or_else(std::io::Error::last_os_error);
                    return Err(map_os_error(&err, true));
                }
            }

            // Claim (dev, ino) in process-local registry
            if !moss_fileio_registry_claim(dev, ino) {
                let cguard = SoloGuard::new("cleanup_close");
                unsafe{
                    sys_close(fd);
                    if let Some(pfd) = parent_dir_fd {
                        sys_close(pfd);
                    }
                }
                drop(cguard);
                return Err(MossFileError::InUse);
            }

            Ok(FileIO {
                inner: Arc::new(MossFairMutex::new(Some(FileIOInner {
                    fd,
                    parent_dir_fd,
                    dev,
                    ino,
                    created,
                    dir_obligation: created,
                    is_closed: false,
                    path: path.to_string(),
                    parent_path: parent_path_str,
                }))),
            })
        }

        pub fn open_in_place(&mut self, path: &str, mode: &str) {
            self.open_in_place_checked(path, mode)
                .unwrap_or_else(|cause| abort_fileio_error("open", cause));
        }

        pub fn open_in_place_checked(&mut self, path: &str, mode: &str) -> Result<(), MossFileError> {
            let is_currently_open = {
                let guard = self.inner.lock();
                guard.as_ref().map_or(false, |i| !i.is_closed)
            };
            if is_currently_open {
                eprintln!("[moss-fileio] error: open_in_place on already-open FileIO");
                std::process::abort();
            }
            let new_file = FileIO::open_checked(path, mode)?;
            *self = new_file;
            Ok(())
        }

        pub fn read(&self, offset: i64, size: i64) -> Range {
            self.read_checked(offset, size)
                .unwrap_or_else(|cause| abort_fileio_error("read", cause))
        }

        pub fn read_checked(&self, offset: i64, size: i64) -> Result<Range, MossFileError> {
            read_shared_checked(&self.inner, offset, size)
        }
        pub fn read_batch(&self, requests: &[(i64, i64)]) -> RangeBatch {
            self.read_batch_checked(requests)
                .unwrap_or_else(|cause| abort_fileio_error("read", cause))
        }

        pub fn read_batch_checked(&self, requests: &[(i64, i64)]) -> Result<RangeBatch, MossFileError> {
            // Verify receiver lifecycle
            {
                let guard = self.inner.lock();
                match guard.as_ref() {
                    Some(inner) if !inner.is_closed => {}
                    _ => {
                        eprintln!("[moss-fileio] error: read_batch on closed or uninitialized FileIO");
                        std::process::abort();
                    }
                }
            }
            if requests.is_empty() {
                return Ok(RangeBatch::new(vec![]));
            }
            let batch_len = usize::try_from(requests.len()).unwrap_or_else(|_| std::process::abort());
            let mut ranges = Vec::with_capacity(batch_len);
            for &(offset, size) in requests {
                ranges.push(self.read_checked(offset, size)?);
            }
            Ok(RangeBatch::new(ranges))
        }

        pub fn write_bytes(&self, offset: i64, data: &[u8]) {
            self.write_bytes_checked(offset, data)
                .unwrap_or_else(|cause| abort_fileio_error("write", cause));
        }

        pub fn write_bytes_checked(&self, offset: i64, data: &[u8]) -> Result<(), MossFileError> {
            let fd = {
                let guard = self.inner.lock();
                match guard.as_ref() {
                    Some(inner) if !inner.is_closed => inner.fd,
                    _ => {
                        eprintln!("[moss-fileio] error: write on closed or uninitialized FileIO");
                        std::process::abort();
                    }
                }
            };
            let data_len_i64 = i64::try_from(data.len()).unwrap_or_else(|_| {
                eprintln!("[moss-fileio] error: write data length exceeds i64::MAX");
                std::process::abort();
            });
            if offset < 0 || (i64::MAX - offset < data_len_i64) {
                eprintln!("[moss-fileio] error: invalid write offset {}", offset);
                std::process::abort();
            }
            if data.is_empty() {
                return Ok(());
            }
            if let Some((cause, _)) = take_fileio_fault(MossFileIoFaultSite::WriteBefore) {
                return Err(cause);
            }
            let after_fault = take_fileio_fault(MossFileIoFaultSite::WriteAfterPrefix);

            let mut total_written = 0usize;
            let target = after_fault
                .map_or(data.len(), |(_, progress)| std::cmp::min(progress, data.len()));
            while total_written < target {
                let current_off = offset.checked_add(i64::try_from(total_written).unwrap_or_else(|_| std::process::abort()))
                    .unwrap_or_else(|| {
                        eprintln!("[moss-fileio] error: offset overflow during write");
                        std::process::abort();
                    });
                let guard = SoloGuard::new("write");
                let res = unsafe{
                    posix::pwrite(
                        fd,
                        data[total_written..].as_ptr(),
                        target - total_written,
                        current_off,
                    )
                };
                let write_err = if res < 0 {
                    Some(std::io::Error::last_os_error())
                } else {
                    None
                };
                drop(guard);
                if res < 0 {
                    let err = write_err.unwrap_or_else(std::io::Error::last_os_error);
                    if err.raw_os_error() == Some(posix::EINTR) {
                        continue;
                    }
                    return Err(map_data_os_error(&err, true));
                }
                if res == 0 {
                    return Err(MossFileError::IO);
                }
                let res_usize = usize::try_from(res).unwrap_or_else(|_| std::process::abort());
                total_written += res_usize;
            }
            if let Some((cause, _)) = after_fault {
                return Err(cause);
            }
            Ok(())
        }

        pub fn write<T: AsRef<[u8]>>(&self, offset: i64, data: T) {
            self.write_bytes(offset, data.as_ref());
        }

        pub fn write_checked<T: AsRef<[u8]>>(&self, offset: i64, data: T) -> Result<(), MossFileError> {
            self.write_bytes_checked(offset, data.as_ref())
        }

        pub fn sync(&self) {
            self.sync_checked(false)
                .unwrap_or_else(|cause| abort_fileio_error("sync", cause));
        }

        pub fn sync_dataonly(&self) {
            self.sync_checked(true)
                .unwrap_or_else(|cause| abort_fileio_error("sync", cause));
        }

        pub fn sync_with_mode(&self, dataonly: bool) {
            self.sync_checked(dataonly)
                .unwrap_or_else(|cause| abort_fileio_error("sync", cause));
        }

        pub fn sync_checked(&self, dataonly: bool) -> Result<(), MossFileError> {
            let (fd, pfd, path, parent_path) = {
                let guard = self.inner.lock();
                match guard.as_ref() {
                    Some(inner) if !inner.is_closed => (
                        inner.fd,
                        if inner.dir_obligation { inner.parent_dir_fd } else { None },
                        inner.path.clone(),
                        inner.parent_path.clone(),
                    ),
                    _ => {
                        eprintln!("[moss-fileio] error: sync on closed or uninitialized FileIO");
                        std::process::abort();
                    }
                }
            };

            if let Some((cause, _)) = take_fileio_fault(MossFileIoFaultSite::FileSync) {
                return Err(cause);
            }

            if dataonly {
                let guard = SoloGuard::new("sync_dataonly");
                moss_sync_event("file_fdatasync", fd, &path);
                let res = unsafe{ posix::fdatasync(fd) };
                let err_opt = if res < 0 { Some(std::io::Error::last_os_error()) } else { None };
                drop(guard);
                if let Some(err) = err_opt {
                    return Err(map_data_os_error(&err, true));
                }
            } else {
                let guard = SoloGuard::new("sync");
                moss_sync_event("file_fsync", fd, &path);
                let res = unsafe{ posix::fsync(fd) };
                let err_opt = if res < 0 { Some(std::io::Error::last_os_error()) } else { None };
                drop(guard);
                if let Some(err) = err_opt {
                    return Err(map_data_os_error(&err, true));
                }
            }

            if let Some(pfd_val) = pfd {
                if let Some((cause, _)) = take_fileio_fault(MossFileIoFaultSite::ParentDirectorySync) {
                    return Err(cause);
                }
                let guard = SoloGuard::new("sync_dir");
                moss_sync_event("dir_fsync", pfd_val, &parent_path);
                let res = unsafe{ posix::fsync(pfd_val) };
                let err_opt = if res < 0 { Some(std::io::Error::last_os_error()) } else { None };
                drop(guard);
                if let Some(err) = err_opt {
                    return Err(map_data_os_error(&err, true));
                }

                // Directory durability is established. Move the descriptor out
                // before attempting close so an explicit close failure is never
                // retried by Drop and cannot double-release the resource.
                {
                    let mut guard = self.inner.lock();
                    if let Some(inner) = guard.as_mut() {
                        if inner.dir_obligation {
                            inner.parent_dir_fd = None;
                            inner.dir_obligation = false;
                        }
                    }
                }
                let guard = SoloGuard::new("close_parent_dir");
                let cres = unsafe{ sys_close(pfd_val) };
                let cerr_opt = if cres < 0 { Some(std::io::Error::last_os_error()) } else { None };
                drop(guard);
                if let Some((cause, _)) = take_fileio_fault(MossFileIoFaultSite::ParentDirectoryClose) {
                    return Err(cause);
                }
                if let Some(err) = cerr_opt {
                    let _ = err;
                    return Err(MossFileError::IO);
                }
            }
            Ok(())
        }

        pub fn close(&self) {
            self.close_checked()
                .unwrap_or_else(|cause| abort_fileio_error("close", cause));
        }

        pub fn close_checked(&self) -> Result<(), MossFileError> {
            self.close_impl(true)
        }

        // Raised-path cleanup is idempotent, never syncs, and deliberately
        // ignores a secondary close failure so the selected typed error remains
        // authoritative. The registry claim is still released exactly once.
        pub fn abandon_for_raise(&self) {
            let _ = self.close_impl(false);
        }

        fn close_impl(&self, closed_is_misuse: bool) -> Result<(), MossFileError> {
            let to_close = {
                let mut guard = self.inner.lock();
                match guard.as_mut() {
                    Some(inner) if !inner.is_closed => {
                        inner.is_closed = true;
                        let fd = inner.fd;
                        let pfd = inner.parent_dir_fd.take();
                        let dev = inner.dev;
                        let ino = inner.ino;
                        Some((fd, pfd, dev, ino))
                    }
                    _ => {
                        if closed_is_misuse {
                            eprintln!("[moss-fileio] error: close on closed or uninitialized FileIO");
                            std::process::abort();
                        }
                        None
                    }
                }
            };

            if let Some((fd, pfd, dev, ino)) = to_close {
                let mut parent_close_err = None;
                if let Some(parent_fd) = pfd {
                    let guard = SoloGuard::new("close_parent_dir");
                    let pres = unsafe{ sys_close(parent_fd) };
                    if pres < 0 {
                        parent_close_err = Some(std::io::Error::last_os_error());
                    }
                    drop(guard);
                }

                let guard = SoloGuard::new("close");
                let file_res = unsafe{ sys_close(fd) };
                let file_close_err = if file_res < 0 {
                    Some(std::io::Error::last_os_error())
                } else {
                    None
                };
                drop(guard);

                // Inode claim is released after the close attempt
                moss_fileio_registry_release(dev, ino);

                let injected_parent =
                    take_fileio_fault(MossFileIoFaultSite::ParentDirectoryClose).map(|v| v.0);
                let injected_file =
                    take_fileio_fault(MossFileIoFaultSite::FileClose).map(|v| v.0);
                if injected_parent.is_some() || parent_close_err.is_some() {
                    if closed_is_misuse {
                        eprintln!("[moss-fileio] error: close failed on parent directory descriptor");
                    }
                    return Err(injected_parent.unwrap_or(MossFileError::IO));
                }
                if injected_file.is_some() || file_close_err.is_some() {
                    if closed_is_misuse {
                        eprintln!("[moss-fileio] error: close failed on descriptor {}", fd);
                    }
                    return Err(injected_file.unwrap_or(MossFileError::IO));
                }
            }
            Ok(())
        }

        pub fn chunks(&self, size: i64) -> MossChunks<'_> {
            MossChunks::new(self, size)
        }

        pub fn is_open(&self) -> bool {
            let guard = self.inner.lock();
            guard.as_ref().map_or(false, |i| !i.is_closed)
        }

        pub fn device(&self) -> u64 {
            let guard = self.inner.lock();
            guard.as_ref().map_or(0, |i| i.dev)
        }

        pub fn inode(&self) -> u64 {
            let guard = self.inner.lock();
            guard.as_ref().map_or(0, |i| i.ino)
        }

        pub fn created_file(&self) -> bool {
            let guard = self.inner.lock();
            guard.as_ref().map_or(false, |i| i.created)
        }

        pub fn has_directory_obligation(&self) -> bool {
            let guard = self.inner.lock();
            guard.as_ref().map_or(false, |i| i.dir_obligation)
        }
    }

    fn read_shared_checked(
        inner: &MossFairMutex<Option<FileIOInner>>,
        offset: i64,
        size: i64,
    ) -> Result<Range, MossFileError> {
        let fd = {
            let guard = inner.lock();
            match guard.as_ref() {
                Some(inner) if !inner.is_closed => inner.fd,
                _ => {
                    eprintln!("[moss-fileio] error: read on closed or uninitialized FileIO");
                    std::process::abort();
                }
            }
        };
        if offset < 0 || size < 0 || (i64::MAX - offset < size) {
            eprintln!("[moss-fileio] error: invalid read offset {} or size {}", offset, size);
            std::process::abort();
        }
        if size == 0 {
            return Ok(Range::empty());
        }
        if let Some((cause, _)) = take_fileio_fault(MossFileIoFaultSite::ReadBefore) {
            return Err(cause);
        }

        let target = usize::try_from(size).unwrap_or_else(|_| {
            eprintln!("[moss-fileio] error: read size {} exceeds address space", size);
            std::process::abort();
        });
        let after_fault = take_fileio_fault(MossFileIoFaultSite::ReadAfterPrefix);
        let target = after_fault
            .map_or(target, |(_, progress)| std::cmp::min(progress, target));
        let mut buf = vec![0u8; target];
        let mut total_read = 0usize;
        let mut at_eof = false;
        while total_read < target && !at_eof {
            let current_off = offset.checked_add(i64::try_from(total_read).unwrap_or_else(|_| std::process::abort()))
                .unwrap_or_else(|| {
                    eprintln!("[moss-fileio] error: offset overflow during read");
                    std::process::abort();
                });
            let guard = SoloGuard::new("read");
            let res = unsafe{
                posix::pread(
                    fd,
                    buf[total_read..].as_mut_ptr(),
                    target - total_read,
                    current_off,
                )
            };
            let read_err = if res < 0 {
                Some(std::io::Error::last_os_error())
            } else {
                None
            };
            drop(guard);
            if res < 0 {
                let err = read_err.unwrap_or_else(std::io::Error::last_os_error);
                if err.raw_os_error() == Some(posix::EINTR) {
                    continue;
                }
                let _ = err;
                return Err(MossFileError::IO);
            }
            if res == 0 {
                at_eof = true;
            } else {
                let res_usize = usize::try_from(res).unwrap_or_else(|_| std::process::abort());
                total_read += res_usize;
            }
        }
        buf.truncate(total_read);
        if let Some((cause, _)) = after_fault {
            return Err(cause);
        }
        Ok(Range::from_vec(buf))
    }

    // Compiler-created, read-only branch borrow (spec sec. 13.1). Owned and
    // Send + 'static so a chunk Branch can carry it; it shares the owner's
    // descriptor state and never becomes a second FileIO owner. Generated
    // code drops every borrow before the Branch join returns.
    pub struct MossFileIOReadBorrow {
        inner: Arc<MossFairMutex<Option<FileIOInner>>>,
    }

    impl MossFileIOReadBorrow {
        pub fn read(&self, offset: i64, size: i64) -> Range {
            self.read_checked(offset, size)
                .unwrap_or_else(|cause| abort_fileio_error("read", cause))
        }

        pub fn read_checked(&self, offset: i64, size: i64) -> Result<Range, MossFileError> {
            read_shared_checked(&self.inner, offset, size)
        }
    }

    pub fn moss_fileio_branch_read_borrow(file: &FileIO) -> MossFileIOReadBorrow {
        MossFileIOReadBorrow { inner: Arc::clone(&file.inner) }
    }

    impl Drop for FileIO {
        fn drop(&mut self) {
            self.abandon_for_raise();
        }
    }

    pub struct MossChunks<'a> {
        file: &'a FileIO,
        chunk_size: i64,
        offset: i64,
        finished: bool,
    }

    impl<'a> MossChunks<'a> {
        pub fn new(file: &'a FileIO, chunk_size: i64) -> Self {
            if !file.is_open() {
                eprintln!("[moss-fileio] error: chunks on closed or uninitialized FileIO");
                std::process::abort();
            }
            if chunk_size <= 0 {
                eprintln!("[moss-fileio] error: invalid chunks size {}", chunk_size);
                std::process::abort();
            }
            MossChunks {
                file,
                chunk_size,
                offset: 0,
                finished: false,
            }
        }

        pub fn next_chunk(&mut self) -> Option<Range> {
            self.next_chunk_checked()
                .unwrap_or_else(|cause| abort_fileio_error("read", cause))
        }

        // Phase 21 generated chunk lowering uses this checked path so a read
        // failure becomes an indexed typed outcome instead of a process abort.
        // The iterator adapter above retains Phase 20's fail-closed behavior.
        pub fn next_chunk_checked(&mut self) -> Result<Option<Range>, MossFileError> {
            if self.finished {
                return Ok(None);
            }
            let range = self.file.read_checked(self.offset, self.chunk_size)?;
            if range.is_empty() {
                self.finished = true;
                return Ok(None);
            }
            let len = range.len();
            self.offset = self.offset.checked_add(len).unwrap_or_else(|| {
                eprintln!("[moss-fileio] error: offset overflow during chunks");
                std::process::abort();
            });
            if len < self.chunk_size {
                self.finished = true;
            }
            Ok(Some(range))
        }
    }

    impl<'a> Iterator for MossChunks<'a> {
        type Item = Range;
        fn next(&mut self) -> Option<Self::Item> {
            self.next_chunk()
        }
    }
}

pub use moss_fileio::{FileIO, Range, RangeBatch, MossChunks, MossFileError, MossFileIoFaultSite, MossFileIOReadBorrow, moss_fileio_branch_read_borrow, moss_fileio_registry_claim, moss_fileio_registry_release};
#[cfg(any(test, moss_perf))]
pub use moss_fileio::{moss_set_sync_hook, moss_clear_sync_hook, moss_set_close_override, moss_clear_close_override, moss_fileio_registry_contains, moss_fileio_registry_reset, moss_inject_fileio_fault, moss_clear_fileio_fault, moss_fileio_map_open_errno_for_test, moss_fileio_map_data_errno_for_test};
)RUST";
}

inline const char* fileio_root_runtime_rust() {
  return R"RUST(
// Moss Phase 20 process-wide root coordination runtime.
// Portable std-only state owned by every executable root: the inode registry,
// its fair leaf lock, and Solo hook storage.  The Linux-only FileIO
// implementation lives in `moss_fileio`, which carries its own target guard.
#[allow(dead_code)]
pub mod moss_root_runtime {
    use super::{MossFairMutex, MossFairGuard, MossFairCondvar};
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::collections::HashSet;

    // R5 fair registry protocol: callers take a FIFO ticket and park on the
    // fair condition (releasing the fair state lock) until served. Critical
    // sections are only the concrete HashSet operations below: no kernel I/O,
    // no callbacks, no other runtime or Moss lock, and nothing that unwinds,
    // so a served ticket is always advanced.
    struct FairRegistryLock {
        next_ticket: AtomicU64,
        state: MossFairMutex<RegistryState>,
        cv: MossFairCondvar,
    }

    struct RegistryState {
        serving_ticket: u64,
        inode_set: Option<HashSet<(u64, u64)>>,
        #[cfg(any(test, moss_perf))]
        trace: Option<Vec<test_support::RegistryTraceEntry>>,
        #[cfg(any(test, moss_perf))]
        waiting: u64,
    }

    impl RegistryState {
        fn set(&mut self) -> &mut HashSet<(u64, u64)> {
            self.inode_set.get_or_insert_with(HashSet::new)
        }
    }

    impl FairRegistryLock {
        const fn new() -> Self {
            FairRegistryLock {
                next_ticket: AtomicU64::new(0),
                state: MossFairMutex::new(RegistryState {
                    serving_ticket: 0,
                    inode_set: None,
                    #[cfg(any(test, moss_perf))]
                    trace: None,
                    #[cfg(any(test, moss_perf))]
                    waiting: 0,
                }),
                cv: MossFairCondvar::new(),
            }
        }

        fn lock_state(&self) -> MossFairGuard<'_, RegistryState> {
            self.state.lock()
        }

        fn admit(&self) -> (MossFairGuard<'_, RegistryState>, u64) {
            let ticket = self.next_ticket.fetch_add(1, Ordering::SeqCst);
            let mut state = self.lock_state();
            while state.serving_ticket != ticket {
                #[cfg(any(test, moss_perf))]
                {
                    state.waiting += 1;
                }
                state = self.cv.wait(state);
                #[cfg(any(test, moss_perf))]
                {
                    state.waiting -= 1;
                }
            }
            (state, ticket)
        }

        fn finish(&self, mut state: MossFairGuard<'_, RegistryState>) {
            state.serving_ticket = state.serving_ticket.wrapping_add(1);
            self.cv.notify_all(&state);
        }

        fn claim(&self, dev: u64, ino: u64) -> bool {
            let (mut state, _ticket) = self.admit();
            #[cfg(any(test, moss_perf))]
            state.record(_ticket, test_support::RegistryOp::Claim, dev, ino);
            let inserted = state.set().insert((dev, ino));
            self.finish(state);
            inserted
        }

        fn release(&self, dev: u64, ino: u64) {
            let (mut state, _ticket) = self.admit();
            #[cfg(any(test, moss_perf))]
            state.record(_ticket, test_support::RegistryOp::Release, dev, ino);
            state.set().remove(&(dev, ino));
            self.finish(state);
        }

        #[cfg(any(test, moss_perf))]
        fn contains(&self, dev: u64, ino: u64) -> bool {
            let (mut state, _ticket) = self.admit();
            let present = state.set().contains(&(dev, ino));
            self.finish(state);
            present
        }

        #[cfg(any(test, moss_perf))]
        fn reset(&self) {
            let (mut state, _ticket) = self.admit();
            state.set().clear();
            self.finish(state);
        }
    }

    static MOSS_REGISTRY: FairRegistryLock = FairRegistryLock::new();
    static MOSS_SOLO_ENTER_HOOK: MossFairMutex<Option<fn(&str)>> = MossFairMutex::new(None);
    static MOSS_SOLO_LEAVE_HOOK: MossFairMutex<Option<fn(&str)>> = MossFairMutex::new(None);

    #[no_mangle]
    pub extern "C" fn moss_fileio_registry_claim(dev: u64, ino: u64) -> bool {
        MOSS_REGISTRY.claim(dev, ino)
    }

    #[no_mangle]
    pub extern "C" fn moss_fileio_registry_release(dev: u64, ino: u64) {
        MOSS_REGISTRY.release(dev, ino)
    }

    #[cfg(any(test, moss_perf))]
    #[no_mangle]
    pub extern "C" fn moss_fileio_registry_contains(dev: u64, ino: u64) -> bool {
        MOSS_REGISTRY.contains(dev, ino)
    }

    #[cfg(any(test, moss_perf))]
    #[no_mangle]
    pub extern "C" fn moss_fileio_registry_reset() {
        MOSS_REGISTRY.reset()
    }

    // Test-only fair-lock instrumentation.  Never part of the production ABI.
    #[cfg(any(test, moss_perf))]
    pub mod test_support {
        use super::{RegistryState, MOSS_REGISTRY};
        use std::sync::atomic::Ordering;

        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum RegistryOp {
            Claim,
            Release,
        }

        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub struct RegistryTraceEntry {
            pub ticket: u64,
            pub op: RegistryOp,
            pub dev: u64,
            pub ino: u64,
        }

        impl RegistryState {
            pub(super) fn record(&mut self, ticket: u64, op: RegistryOp, dev: u64, ino: u64) {
                self.trace.get_or_insert_with(Vec::default).push(RegistryTraceEntry { ticket, op, dev, ino });
            }
        }

        // Critical-section entry order of claim/release operations, with the
        // ticket each one was assigned.  Draining also clears the trace.
        pub fn take_trace() -> Vec<RegistryTraceEntry> {
            let mut state = MOSS_REGISTRY.lock_state();
            state.trace.take().unwrap_or_default()
        }

        pub fn issued_tickets() -> u64 {
            MOSS_REGISTRY.next_ticket.load(Ordering::SeqCst)
        }

        // Number of callers currently parked in `Condvar::wait`.
        pub fn parked_waiters() -> u64 {
            MOSS_REGISTRY.lock_state().waiting
        }

        // Holds one admitted turn without holding the state mutex, so later
        // tickets observably park on the Condvar.  Dropping ends the turn.
        pub struct HeldTurn {
            pub ticket: u64,
        }

        pub fn hold_turn() -> HeldTurn {
            let (state, ticket) = MOSS_REGISTRY.admit();
            drop(state);
            HeldTurn { ticket }
        }

        impl Drop for HeldTurn {
            fn drop(&mut self) {
                let state = MOSS_REGISTRY.lock_state();
                MOSS_REGISTRY.finish(state);
            }
        }
    }

    #[no_mangle]
    pub extern "Rust" fn moss_solo_enter(reason: &str) {
        let hook = *MOSS_SOLO_ENTER_HOOK.lock();
        if let Some(enter) = hook {
            enter(reason);
        }
    }

    #[no_mangle]
    pub extern "Rust" fn moss_solo_leave(reason: &str) {
        let hook = *MOSS_SOLO_LEAVE_HOOK.lock();
        if let Some(leave) = hook {
            leave(reason);
        }
    }

    pub fn moss_set_solo_hooks(enter: fn(&str), leave: fn(&str)) {
        *MOSS_SOLO_ENTER_HOOK.lock() = Some(enter);
        *MOSS_SOLO_LEAVE_HOOK.lock() = Some(leave);
    }

    pub fn moss_clear_solo_hooks() {
        *MOSS_SOLO_ENTER_HOOK.lock() = None;
        *MOSS_SOLO_LEAVE_HOOK.lock() = None;
    }
}

pub use moss_root_runtime::{moss_set_solo_hooks, moss_clear_solo_hooks};
)RUST";
}

} // namespace moss
