#!/usr/bin/env python3
"""Phase 21.B native FileIO typed-error and teardown regression suite."""

import os
import shutil
import subprocess
import sys

import check_phase20_fileio_runtime as phase20


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
TMP_ROOT = os.path.join(REPO_ROOT, "tmp", "phase21-fileio-errors")


RUST_HARNESS = r'''
#![allow(dead_code)]
#![allow(unused_imports)]
#![allow(unused_variables)]
#![allow(unused_mut)]
#![allow(non_snake_case)]

MOSS_FILEIO_RUNTIME_PLACEHOLDER
MOSS_FILEIO_ROOT_RUNTIME_PLACEHOLDER

use std::fs;
use std::io::Write;
use std::os::unix::io::RawFd;
use std::sync::Mutex;

static CLOSE_COUNT: Mutex<usize> = Mutex::new(0);

fn counting_close(fd: RawFd) -> i32 {
    *CLOSE_COUNT.lock().unwrap() += 1;
    unsafe { moss_fileio::posix::close(fd) }
}

fn require(condition: bool, message: &str) {
    if !condition {
        eprintln!("ASSERTION FAILED: {}", message);
        std::process::exit(2);
    }
}

fn require_error<T>(result: Result<T, MossFileError>, expected: MossFileError, message: &str) {
    match result {
        Err(actual) if actual == expected => {}
        Err(actual) => {
            eprintln!("ASSERTION FAILED: {}: expected {:?}, got {:?}", message, expected, actual);
            std::process::exit(2);
        }
        Ok(_) => {
            eprintln!("ASSERTION FAILED: {}: expected {:?}, got success", message, expected);
            std::process::exit(2);
        }
    }
}

fn assert_six_variant_taxonomy(error: MossFileError) -> u8 {
    match error {
        MossFileError::NotFound => 0,
        MossFileError::PermissionDenied => 1,
        MossFileError::NotRegularFile => 2,
        MossFileError::InUse => 3,
        MossFileError::Full => 4,
        MossFileError::IO => 5,
    }
}

fn test_errno_mapping() {
    for errno in [moss_fileio::posix::ENOENT, moss_fileio::posix::ENOTDIR] {
        require(moss_fileio_map_open_errno_for_test(errno) == MossFileError::NotFound,
                "missing-path errno maps to NotFound");
    }
    for errno in [moss_fileio::posix::EACCES, moss_fileio::posix::EPERM,
                  moss_fileio::posix::EROFS] {
        require(moss_fileio_map_open_errno_for_test(errno) == MossFileError::PermissionDenied,
                "access-refusal errno maps to PermissionDenied");
    }
    for errno in [moss_fileio::posix::ENOSPC, moss_fileio::posix::EDQUOT] {
        require(moss_fileio_map_open_errno_for_test(errno) == MossFileError::Full,
                "storage-exhaustion open errno maps to Full");
        require(moss_fileio_map_data_errno_for_test(errno, true) == MossFileError::Full,
                "storage-exhaustion data errno maps to Full when reachable");
        require(moss_fileio_map_data_errno_for_test(errno, false) == MossFileError::IO,
                "read-style data sites do not overclaim Full");
    }
    require(moss_fileio_map_open_errno_for_test(moss_fileio::posix::EISDIR) ==
                MossFileError::NotRegularFile,
            "directory errno maps to NotRegularFile");
    for errno in [moss_fileio::posix::EBUSY, moss_fileio::posix::ETXTBSY] {
        require(moss_fileio_map_open_errno_for_test(errno) == MossFileError::InUse,
                "busy-path errno maps to InUse");
    }
    require(moss_fileio_map_open_errno_for_test(9999) == MossFileError::IO,
            "unknown open errno maps to IO");
    require(moss_fileio_map_data_errno_for_test(moss_fileio::posix::EACCES, true) ==
                MossFileError::IO,
            "non-storage data errno maps to IO");
}

fn test_open_mapping_and_cleanup(dir: &str) {
    for cause in [
        MossFileError::NotFound,
        MossFileError::PermissionDenied,
        MossFileError::NotRegularFile,
        MossFileError::InUse,
        MossFileError::Full,
        MossFileError::IO,
    ] {
        moss_inject_fileio_fault(MossFileIoFaultSite::OpenBefore, cause, 0);
        require_error(FileIO::open_checked("ignored-by-open-fault", "ro"), cause,
                      "open accepts exactly the six public cause variants");
    }

    let missing = format!("{}/missing/child", dir);
    require_error(FileIO::open_checked(&missing, "ro"), MossFileError::NotFound,
                  "missing path maps to NotFound");
    require_error(FileIO::open_checked("invalid\0path", "ro"), MossFileError::IO,
                  "interior-NUL path is a catchable generic IO cause");
    require_error(FileIO::open_checked(dir, "ro"), MossFileError::NotRegularFile,
                  "directory maps to NotRegularFile");

    let path = format!("{}/open-cleanup.dat", dir);
    moss_inject_fileio_fault(
        MossFileIoFaultSite::OpenAfterDescriptor,
        MossFileError::PermissionDenied,
        0,
    );
    require_error(FileIO::open_checked(&path, "create"), MossFileError::PermissionDenied,
                  "post-descriptor open failure retains typed cause");
    let reopened = FileIO::open_checked(&path, "rw").unwrap();
    reopened.close_checked().unwrap();

    let flag_path = format!("{}/flag-clear-cleanup.dat", dir);
    *CLOSE_COUNT.lock().unwrap() = 0;
    moss_set_close_override(counting_close);
    moss_inject_fileio_fault(
        MossFileIoFaultSite::OpenFlagClear,
        MossFileError::IO,
        0,
    );
    require_error(FileIO::open_checked(&flag_path, "create"), MossFileError::IO,
                  "descriptor flag setup failure maps to IO and closes descriptor");
    require(*CLOSE_COUNT.lock().unwrap() == 1,
            "failed descriptor flag setup closes the new descriptor exactly once");
    moss_clear_close_override();
    let reopened_after_flag = FileIO::open_checked(&flag_path, "rw").unwrap();
    reopened_after_flag.close_checked().unwrap();

    let parent_path = format!("{}/parent-open-cleanup.dat", dir);
    moss_inject_fileio_fault(
        MossFileIoFaultSite::ParentDirectoryOpen,
        MossFileError::IO,
        0,
    );
    require_error(FileIO::open_checked(&parent_path, "create"), MossFileError::IO,
                  "parent-directory open failure maps to IO");
    let reopened_parent = FileIO::open_checked(&parent_path, "rw").unwrap();
    reopened_parent.close_checked().unwrap();
}

fn test_in_use_is_immediate_and_claim_is_unique(dir: &str) {
    let path = format!("{}/in-use.dat", dir);
    let first = FileIO::open_checked(&path, "create").unwrap();
    let dev = first.device();
    let ino = first.inode();
    require(moss_fileio_registry_contains(dev, ino), "first owner holds registry claim");
    require_error(FileIO::open_checked(&path, "rw"), MossFileError::InUse,
                  "same inode collision is immediate InUse");
    require(moss_fileio_registry_contains(dev, ino), "failed duplicate did not disturb claim");
    first.close_checked().unwrap();
    require(!moss_fileio_registry_contains(dev, ino), "close releases unique claim");
    let reopened = FileIO::open_checked(&path, "rw").unwrap();
    reopened.close_checked().unwrap();
}

fn test_read_fault_sites(dir: &str) {
    let path = format!("{}/read.dat", dir);
    fs::write(&path, b"abcdefgh").unwrap();
    let file = FileIO::open_checked(&path, "ro").unwrap();

    moss_inject_fileio_fault(MossFileIoFaultSite::ReadBefore, MossFileError::IO, 0);
    require_error(file.read_checked(0, 8), MossFileError::IO,
                  "read-before failure maps to IO");
    moss_inject_fileio_fault(MossFileIoFaultSite::ReadAfterPrefix, MossFileError::IO, 3);
    require_error(file.read_checked(0, 8), MossFileError::IO,
                  "read-after-prefix failure maps to IO");
    require(file.read_checked(0, 8).unwrap().as_slice() == b"abcdefgh",
            "read remains usable after checked failure");
    file.close_checked().unwrap();
}

fn test_checked_chunks_propagate_read_errors(dir: &str) {
    let path = format!("{}/chunks-read.dat", dir);
    fs::write(&path, b"abcdef").unwrap();
    let file = FileIO::open_checked(&path, "ro").unwrap();
    let mut chunks = file.chunks(4);

    moss_inject_fileio_fault(MossFileIoFaultSite::ReadBefore, MossFileError::IO, 0);
    require_error(chunks.next_chunk_checked(), MossFileError::IO,
                  "checked chunk read propagates an IO raise");
    moss_inject_fileio_fault(MossFileIoFaultSite::ReadAfterPrefix, MossFileError::IO, 2);
    require_error(chunks.next_chunk_checked(), MossFileError::IO,
                  "checked chunk read propagates failure after a partial prefix");

    require(chunks.next_chunk_checked().unwrap().unwrap().as_slice() == b"abcd",
            "a failed checked chunk read does not advance its logical offset");
    require(chunks.next_chunk_checked().unwrap().unwrap().as_slice() == b"ef",
            "checked chunk reads preserve short-final-chunk behavior");
    require(chunks.next_chunk_checked().unwrap().is_none(),
            "checked chunk iteration ends after its short final chunk");
    require(moss_fileio::moss_test_chunk_boundary(&file),
            "a full chunk ending at i64::MAX has no representable next request");
    file.close_checked().unwrap();
}

fn test_partial_write_and_repair(dir: &str) {
    let path = format!("{}/write.dat", dir);
    let file = FileIO::open_checked(&path, "create").unwrap();

    moss_inject_fileio_fault(MossFileIoFaultSite::WriteBefore, MossFileError::IO, 0);
    require_error(file.write_checked(0, b"abcdefgh"), MossFileError::IO,
                  "write-before failure maps to IO");
    require(file.read_checked(0, 8).unwrap().is_empty(),
            "write-before failure writes no prefix");

    moss_inject_fileio_fault(MossFileIoFaultSite::WriteAfterPrefix, MossFileError::Full, 3);
    require_error(file.write_checked(0, b"abcdefgh"), MossFileError::Full,
                  "write-after-prefix maps storage exhaustion to Full");
    require(file.read_checked(0, 8).unwrap().as_slice() == b"abc",
            "failed write preserves its already-written prefix without rollback");

    file.write_checked(0, b"ABCDEFGH").unwrap();
    require(file.read_checked(0, 8).unwrap().as_slice() == b"ABCDEFGH",
            "same handle can rewrite from trusted source");
    file.close_checked().unwrap();
}

fn test_sync_failures_and_directory_obligation(dir: &str) {
    for cause in [MossFileError::Full, MossFileError::IO] {
        let suffix = assert_six_variant_taxonomy(cause);
        let path = format!("{}/sync-{}.dat", dir, suffix);
        let file = FileIO::open_checked(&path, "create").unwrap();
        file.write_checked(0, b"initial").unwrap();
        moss_inject_fileio_fault(MossFileIoFaultSite::FileSync, cause, 0);
        require_error(file.sync_checked(false), cause,
                      "file sync preserves Full/IO cause");
        file.write_checked(0, b"trusted").unwrap();
        file.sync_checked(false).unwrap();
        require(!file.has_directory_obligation(),
                "successful repair sync clears directory obligation");
        file.close_checked().unwrap();
    }

    let path = format!("{}/directory-sync.dat", dir);
    let file = FileIO::open_checked(&path, "create").unwrap();
    file.write_checked(0, b"payload").unwrap();
    moss_inject_fileio_fault(
        MossFileIoFaultSite::ParentDirectorySync,
        MossFileError::Full,
        0,
    );
    require_error(file.sync_checked(false), MossFileError::Full,
                  "parent-directory sync has a distinct Full path");
    require(file.has_directory_obligation(),
            "failed parent-directory sync retains durability obligation");
    file.write_checked(0, b"repaired").unwrap();
    file.sync_checked(false).unwrap();
    require(!file.has_directory_obligation(),
            "successful rewrite and sync on same handle is legal");
    file.close_checked().unwrap();
}

fn test_parent_close_failure_is_not_retried(dir: &str) {
    let path = format!("{}/parent-close.dat", dir);
    let file = FileIO::open_checked(&path, "create").unwrap();
    file.write_checked(0, b"payload").unwrap();
    moss_inject_fileio_fault(
        MossFileIoFaultSite::ParentDirectoryClose,
        MossFileError::IO,
        0,
    );
    require_error(file.sync_checked(false), MossFileError::IO,
                  "explicit parent close failure maps to IO");
    require(!file.has_directory_obligation(),
            "completed directory sync moves descriptor out before failed close");
    file.sync_checked(false).unwrap();
    file.close_checked().unwrap();
}

fn test_checked_close_and_abandoned_cleanup(dir: &str) {
    let path = format!("{}/checked-close.dat", dir);
    let file = FileIO::open_checked(&path, "create").unwrap();
    let dev = file.device();
    let ino = file.inode();
    moss_inject_fileio_fault(MossFileIoFaultSite::FileClose, MossFileError::IO, 0);
    require_error(file.close_checked(), MossFileError::IO,
                  "explicit close failure is catchable IO");
    require(!file.is_open(), "failed explicit close is still final");
    require(!moss_fileio_registry_contains(dev, ino),
            "failed explicit close releases registry claim");
    let reopened = FileIO::open_checked(&path, "rw").unwrap();
    reopened.close_checked().unwrap();

    let parent_path = format!("{}/checked-parent-close.dat", dir);
    let parent_file = FileIO::open_checked(&parent_path, "create").unwrap();
    let parent_dev = parent_file.device();
    let parent_ino = parent_file.inode();
    moss_inject_fileio_fault(
        MossFileIoFaultSite::ParentDirectoryClose,
        MossFileError::IO,
        0,
    );
    require_error(parent_file.close_checked(), MossFileError::IO,
                  "explicit parent close failure is catchable IO");
    require(!moss_fileio_registry_contains(parent_dev, parent_ino),
            "parent close failure still closes file and releases claim");
    let parent_reopened = FileIO::open_checked(&parent_path, "rw").unwrap();
    parent_reopened.close_checked().unwrap();

    let abandoned_path = format!("{}/abandoned.dat", dir);
    fs::write(&abandoned_path, b"content").unwrap();
    let abandoned = FileIO::open_checked(&abandoned_path, "ro").unwrap();
    let abandoned_dev = abandoned.device();
    let abandoned_ino = abandoned.inode();
    moss_inject_fileio_fault(MossFileIoFaultSite::WriteBefore, MossFileError::Full, 0);
    require_error(abandoned.write_checked(0, b"x"), MossFileError::Full,
                  "selected original raised cause is Full");
    moss_inject_fileio_fault(MossFileIoFaultSite::FileClose, MossFileError::IO, 0);
    abandoned.abandon_for_raise();
    require(!abandoned.is_open(), "abandoned cleanup closes owner");
    require(!moss_fileio_registry_contains(abandoned_dev, abandoned_ino),
            "abandoned cleanup releases claim despite secondary close error");
}

fn test_exactly_once_teardown(dir: &str) {
    let path = format!("{}/once.dat", dir);
    fs::write(&path, b"content").unwrap();
    let file = FileIO::open_checked(&path, "ro").unwrap();
    let dev = file.device();
    let ino = file.inode();
    *CLOSE_COUNT.lock().unwrap() = 0;
    moss_set_close_override(counting_close);
    file.abandon_for_raise();
    file.abandon_for_raise();
    require(*CLOSE_COUNT.lock().unwrap() == 1,
            "repeated abandoned cleanup closes descriptor exactly once");
    require(!moss_fileio_registry_contains(dev, ino),
            "exactly-once teardown releases inode claim");
    moss_clear_close_override();
    drop(file);
    require(*CLOSE_COUNT.lock().unwrap() == 1,
            "Drop after explicit abandoned cleanup does not close again");
    let reopened = FileIO::open_checked(&path, "ro").unwrap();
    reopened.close_checked().unwrap();
}

fn main() {
    let dir = std::env::args().nth(1).expect("workspace argument");
    test_errno_mapping();
    test_open_mapping_and_cleanup(&dir);
    test_in_use_is_immediate_and_claim_is_unique(&dir);
    test_read_fault_sites(&dir);
    test_checked_chunks_propagate_read_errors(&dir);
    test_partial_write_and_repair(&dir);
    test_sync_failures_and_directory_obligation(&dir);
    test_parent_close_failure_is_not_retried(&dir);
    test_checked_close_and_abandoned_cleanup(&dir);
    test_exactly_once_teardown(&dir);
    println!("phase21 FileIO typed-error tests passed");
}
'''


def main() -> int:
    if os.path.isdir(TMP_ROOT):
        shutil.rmtree(TMP_ROOT)
    os.makedirs(TMP_ROOT, exist_ok=True)
    workspace = os.path.join(TMP_ROOT, "workspace")
    os.makedirs(workspace)

    runtime = phase20.extract_fileio_runtime_rust()
    iterator_marker = "    impl<'a> Iterator for MossChunks<'a> {"
    assert runtime.count(iterator_marker) == 1
    runtime = runtime.replace(iterator_marker, '''
    #[cfg(any(test, moss_perf))]
    pub fn moss_test_chunk_boundary(file: &FileIO) -> bool {
        let mut chunks = file.chunks(i64::MAX);
        chunks.offset = i64::MAX;
        chunks.next_chunk_checked().unwrap().is_none()
    }

''' + iterator_marker, 1)
    source = RUST_HARNESS.replace(
        "MOSS_FILEIO_RUNTIME_PLACEHOLDER", runtime
    ).replace(
        "MOSS_FILEIO_ROOT_RUNTIME_PLACEHOLDER", phase20.extract_fileio_root_runtime_rust()
    )
    rust_source = os.path.join(TMP_ROOT, "phase21_fileio_errors.rs")
    executable = os.path.join(TMP_ROOT, "phase21_fileio_errors")
    with open(rust_source, "w", encoding="utf-8") as handle:
        handle.write(source)

    compile_result = subprocess.run(
        ["rustc", "-D", "warnings", *phase20.TEST_CFG, rust_source, "-o", executable],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if compile_result.returncode != 0:
        sys.stderr.write(compile_result.stderr)
        return compile_result.returncode

    run_result = subprocess.run(
        [executable, workspace],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=30,
    )
    sys.stdout.write(run_result.stdout)
    sys.stderr.write(run_result.stderr)
    return run_result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
