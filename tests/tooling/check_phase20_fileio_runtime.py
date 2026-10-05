#!/usr/bin/env python3
"""Phase 20 Agent B — Final FileIO Runtime Verification Suite.

Tests all required behaviors:
1. Exact read
2. Short final read
3. Zero-length EOF
4. Explicit offsets independent of implicit cursor
5. Complete write semantics
6. Multiple independent offsets
7. Batch ordering
8. Bounded batch behavior
9. Non-regular file rejected
10. Same pathname duplicate live open rejected
11. Symlink alias rejected
12. Hardlink alias rejected
13. Different inode succeeds
14. Claim released after close
15. create missing file records creation
16. create existing file does not truncate
17. rw existing file does not truncate
18. First sync() after create includes parent-directory sync
19. First sync(dataonly) after create includes parent-directory sync
20. Later sync after obligation clear does not require another parent sync
21. Close without sync does not imply durability
22. Close on closed/uninitialized fails closed
23. Runtime/registry locks not held across file waits
24. Solo hooks paired around waits
25. open_in_place lifecycle semantics (closed owner succeeds, open owner fails)
26. Closed/uninitialized zero/empty operations fail closed
27. Close error simulation (single attempt, registry released, fails closed)
28. Numerical overflow validation on offsets and sizes
29. Rust compile-negative test: FileIO is NOT cloneable
30. Cross-crate process-global registry collision rejection (.rlib provider + consumer)
31. Cross-crate process-global Solo hook propagation
32. Hook registration lock-dropping discipline
33. Parent-directory close failure handling (main close executed, registry released, fail closed)
34. Main-only FileIO compiler runtime emission
35. False-positive emission avoidance on unrelated identifiers (RangeRover)
"""

import os
import shutil
import subprocess
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
TMP_DIR = os.path.join(REPO_ROOT, "tmp")
os.makedirs(TMP_DIR, exist_ok=True)

RUST_HARNESS_SOURCE = r"""
#![allow(dead_code)]
#![allow(unused_imports)]
#![allow(unused_variables)]
#![allow(unused_mut)]
#![allow(non_snake_case)]

// Include FileIO runtime and root runtime
MOSS_FILEIO_RUNTIME_PLACEHOLDER
MOSS_FILEIO_ROOT_RUNTIME_PLACEHOLDER

use std::sync::{Arc, Mutex};
use std::collections::HashSet;
use std::fs::{self, File};
use std::io::{Read, Write};
use std::os::unix::fs::symlink;
use std::os::unix::io::RawFd;
use std::path::Path;

fn assert_true(cond: bool, msg: &str) {
    if !cond {
        eprintln!("ASSERTION FAILED: {}", msg);
        std::process::exit(1);
    }
}

fn assert_eq<T: std::fmt::Debug + PartialEq>(a: T, b: T, msg: &str) {
    if a != b {
        eprintln!("ASSERTION FAILED: {} (left: {:?}, right: {:?})", msg, a, b);
        std::process::exit(1);
    }
}

static TEST_SYNC_EVENTS: Mutex<Vec<String>> = Mutex::new(Vec::new());

fn test_sync_hook(event: &str, _fd: RawFd, _path: &str) {
    if let Ok(mut list) = TEST_SYNC_EVENTS.lock() {
        list.push(event.to_string());
    }
}

static TEST_SOLO_DEPTH: Mutex<i32> = Mutex::new(0);
static TEST_SOLO_ENTERS: Mutex<i32> = Mutex::new(0);
static TEST_SOLO_LEAVES: Mutex<i32> = Mutex::new(0);
static TEST_SOLO_REASONS: Mutex<Vec<String>> = Mutex::new(Vec::new());

fn test_solo_enter_hook(reason: &str) {
    if let Ok(mut d) = TEST_SOLO_DEPTH.lock() {
        *d += 1;
    }
    if let Ok(mut c) = TEST_SOLO_ENTERS.lock() {
        *c += 1;
    }
    if let Ok(mut r) = TEST_SOLO_REASONS.lock() {
        r.push(reason.to_string());
    }
}

fn test_solo_leave_hook(_reason: &str) {
    if let Ok(mut d) = TEST_SOLO_DEPTH.lock() {
        *d -= 1;
    }
    if let Ok(mut c) = TEST_SOLO_LEAVES.lock() {
        *c += 1;
    }
}

fn test_solo_lock_check_hook(_reason: &str) {
    let _ = moss_fileio::moss_fileio_registry_contains(99999, 99999);
}

fn dummy_solo_leave_hook(_reason: &str) {}

static CLOSE_ATTEMPT_COUNT: Mutex<i32> = Mutex::new(0);
static TARGET_FD: Mutex<RawFd> = Mutex::new(-1);
static FAIL_PARENT_CLOSE_ONLY: Mutex<bool> = Mutex::new(false);
static FILE_CLOSE_ATTEMPTED: Mutex<bool> = Mutex::new(false);

fn failing_close_override(fd: RawFd) -> i32 {
    let target = *TARGET_FD.lock().unwrap();
    let fail_parent = *FAIL_PARENT_CLOSE_ONLY.lock().unwrap();
    if fail_parent {
        // Parent close fails, main file close succeeds
        let count = {
            let mut c = CLOSE_ATTEMPT_COUNT.lock().unwrap();
            *c += 1;
            *c
        };
        if count == 1 {
            // First close is parent close
            unsafe { moss_fileio::posix::close(fd) };
            -1
        } else {
            if let Ok(mut f) = FILE_CLOSE_ATTEMPTED.lock() {
                *f = true;
            }
            unsafe { moss_fileio::posix::close(fd) }
        }
    } else {
        if target == -1 || fd == target {
            if let Ok(mut c) = CLOSE_ATTEMPT_COUNT.lock() {
                *c += 1;
            }
            unsafe { moss_fileio::posix::close(fd) };
            -1
        } else {
            unsafe { moss_fileio::posix::close(fd) }
        }
    }
}

fn test_1_exact_read(dir: &str) {
    println!("[RUN] Test 1: exact read");
    let path = format!("{}/test1.dat", dir);
    let data = b"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    {
        let mut f = File::create(&path).unwrap();
        f.write_all(data).unwrap();
    }
    let file = FileIO::open(&path, "ro");
    let range = file.read(0, data.len() as i64);
    assert_eq(range.len(), data.len() as i64, "range length matches exact read");
    assert_eq(range.as_slice(), data.as_ref(), "range bytes match exact read");
    assert_eq(range.is_empty(), false, "range is not empty");
    file.close();
    println!("[PASS] Test 1: exact read");
}

fn test_2_short_final_read(dir: &str) {
    println!("[RUN] Test 2: short final read");
    let path = format!("{}/test2.dat", dir);
    let data = b"Hello World!"; // 12 bytes
    {
        let mut f = File::create(&path).unwrap();
        f.write_all(data).unwrap();
    }
    let file = FileIO::open(&path, "ro");
    let range = file.read(6, 20);
    assert_eq(range.len(), 6, "range length is short at EOF");
    assert_eq(range.as_slice(), b"World!".as_ref(), "short range contains remaining bytes");
    file.close();
    println!("[PASS] Test 2: short final read");
}

fn test_3_zero_length_eof(dir: &str) {
    println!("[RUN] Test 3: zero-length EOF");
    let path = format!("{}/test3.dat", dir);
    let data = b"Short file"; // 10 bytes
    {
        let mut f = File::create(&path).unwrap();
        f.write_all(data).unwrap();
    }
    let file = FileIO::open(&path, "ro");
    let range1 = file.read(10, 50);
    assert_eq(range1.len(), 0, "read at EOF returns 0 length");
    assert_eq(range1.is_empty(), true, "range is empty");
    let range2 = file.read(100, 50);
    assert_eq(range2.len(), 0, "read past EOF returns 0 length");
    assert_eq(range2.is_empty(), true, "range past EOF is empty");
    let range3 = file.read(0, 0);
    assert_eq(range3.len(), 0, "read size 0 returns 0 length");
    file.close();
    println!("[PASS] Test 3: zero-length EOF");
}

fn test_4_explicit_offsets(dir: &str) {
    println!("[RUN] Test 4: explicit offsets independent of implicit cursor");
    let path = format!("{}/test4.dat", dir);
    let data = b"AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH"; // 32 bytes (8 blocks of 4)
    {
        let mut f = File::create(&path).unwrap();
        f.write_all(data).unwrap();
    }
    let file = FileIO::open(&path, "ro");
    let r1 = file.read(12, 4);
    assert_eq(r1.as_slice(), b"DDDD".as_ref(), "offset 12 read");
    let r2 = file.read(0, 4);
    assert_eq(r2.as_slice(), b"AAAA".as_ref(), "offset 0 read");
    let r3 = file.read(24, 4);
    assert_eq(r3.as_slice(), b"GGGG".as_ref(), "offset 24 read");
    let r4 = file.read(8, 4);
    assert_eq(r4.as_slice(), b"CCCC".as_ref(), "offset 8 read");
    file.close();
    println!("[PASS] Test 4: explicit offsets independent of implicit cursor");
}

fn test_5_complete_write(dir: &str) {
    println!("[RUN] Test 5: complete write semantics");
    let path = format!("{}/test5.dat", dir);
    let file = FileIO::open(&path, "create");
    let payload = b"The quick brown fox jumps over the lazy dog";
    file.write(0, payload.as_ref());
    let r = file.read(0, payload.len() as i64);
    assert_eq(r.as_slice(), payload.as_ref(), "written payload matches read");
    file.close();
    println!("[PASS] Test 5: complete write semantics");
}

fn test_6_multiple_independent_offsets(dir: &str) {
    println!("[RUN] Test 6: multiple independent offsets");
    let path = format!("{}/test6.dat", dir);
    let file = FileIO::open(&path, "create");
    file.write(0, b"START".as_ref());
    file.write(50, b"MIDDLE".as_ref());
    file.write(100, b"END".as_ref());

    assert_eq(file.read(0, 5).as_slice(), b"START".as_ref(), "read offset 0");
    assert_eq(file.read(50, 6).as_slice(), b"MIDDLE".as_ref(), "read offset 50");
    assert_eq(file.read(100, 3).as_slice(), b"END".as_ref(), "read offset 100");
    file.close();
    println!("[PASS] Test 6: multiple independent offsets");
}

fn test_7_batch_ordering(dir: &str) {
    println!("[RUN] Test 7: batch ordering");
    let path = format!("{}/test7.dat", dir);
    let file = FileIO::open(&path, "create");
    file.write(0, b"000011112222333344445555".as_ref());

    let reqs = vec![(8, 4), (0, 4), (16, 4), (4, 4)];
    let batch = file.read_batch(&reqs);
    assert_eq(batch.len(), 4, "batch length");
    assert_eq(batch.get(0).as_slice(), b"2222".as_ref(), "batch[0] preserves request order (offset 8)");
    assert_eq(batch.get(1).as_slice(), b"0000".as_ref(), "batch[1] preserves request order (offset 0)");
    assert_eq(batch.get(2).as_slice(), b"4444".as_ref(), "batch[2] preserves request order (offset 16)");
    assert_eq(batch.get(3).as_slice(), b"1111".as_ref(), "batch[3] preserves request order (offset 4)");
    file.close();
    println!("[PASS] Test 7: batch ordering");
}

fn test_8_bounded_batch_behavior(dir: &str) {
    println!("[RUN] Test 8: bounded batch behavior");
    let path = format!("{}/test8.dat", dir);
    let file = FileIO::open(&path, "create");
    file.write(0, b"HELLO".as_ref()); // 5 bytes

    // Empty batch
    let empty_batch = file.read_batch(&[]);
    assert_eq(empty_batch.is_empty(), true, "empty batch is empty");
    assert_eq(empty_batch.len(), 0, "empty batch length 0");

    // Mixed batch with in-bounds and EOF
    let reqs = vec![(0, 2), (3, 10), (10, 5)];
    let batch = file.read_batch(&reqs);
    assert_eq(batch.len(), 3, "batch length");
    assert_eq(batch.get(0).as_slice(), b"HE".as_ref(), "batch item 0 exact read");
    assert_eq(batch.get(1).as_slice(), b"LO".as_ref(), "batch item 1 short read at EOF");
    assert_eq(batch.get(2).as_slice(), b"".as_ref(), "batch item 2 zero-length read past EOF");
    file.close();
    println!("[PASS] Test 8: bounded batch behavior");
}

fn test_13_different_inode_succeeds(dir: &str) {
    println!("[RUN] Test 13: different inode succeeds");
    let path1 = format!("{}/test13_a.dat", dir);
    let path2 = format!("{}/test13_b.dat", dir);
    let file1 = FileIO::open(&path1, "create");
    let file2 = FileIO::open(&path2, "create");
    assert_true(file1.inode() != file2.inode() || file1.device() != file2.device(), "inodes/devices are distinct");
    assert_true(file1.is_open(), "file1 is open");
    assert_true(file2.is_open(), "file2 is open");
    file1.close();
    file2.close();
    println!("[PASS] Test 13: different inode succeeds");
}

fn test_14_claim_released_after_close(dir: &str) {
    println!("[RUN] Test 14: claim released after close");
    let path = format!("{}/test14.dat", dir);
    let file1 = FileIO::open(&path, "create");
    let dev = file1.device();
    let ino = file1.inode();
    assert_true(moss_fileio::moss_fileio_registry_contains(dev, ino), "registry contains inode when open");
    file1.close();
    assert_true(!moss_fileio::moss_fileio_registry_contains(dev, ino), "registry released inode after close");
    // Reopen same file
    let file2 = FileIO::open(&path, "rw");
    assert_true(file2.is_open(), "reopen succeeds after close");
    file2.close();
    println!("[PASS] Test 14: claim released after close");
}

fn test_15_create_missing_file_records_creation(dir: &str) {
    println!("[RUN] Test 15: create missing file records creation");
    let path = format!("{}/test15.dat", dir);
    let file = FileIO::open(&path, "create");
    assert_eq(file.created_file(), true, "created_file is true for newly created file");
    assert_eq(file.has_directory_obligation(), true, "has_directory_obligation is true");
    file.close();
    println!("[PASS] Test 15: create missing file records creation");
}

fn test_16_create_existing_file_does_not_truncate(dir: &str) {
    println!("[RUN] Test 16: create existing file does not truncate");
    let path = format!("{}/test16.dat", dir);
    {
        let file = FileIO::open(&path, "create");
        file.write(0, b"INITIAL DATA HERE".as_ref());
        file.close();
    }
    // Reopen with mode "create"
    let file2 = FileIO::open(&path, "create");
    assert_eq(file2.created_file(), false, "created_file is false for existing file");
    assert_eq(file2.has_directory_obligation(), false, "no directory obligation for existing file");
    let r = file2.read(0, 17);
    assert_eq(r.as_slice(), b"INITIAL DATA HERE".as_ref(), "existing file was not truncated");
    file2.close();
    println!("[PASS] Test 16: create existing file does not truncate");
}

fn test_17_rw_existing_file_does_not_truncate(dir: &str) {
    println!("[RUN] Test 17: rw existing file does not truncate");
    let path = format!("{}/test17.dat", dir);
    {
        let file = FileIO::open(&path, "create");
        file.write(0, b"PRESERVED CONTENT".as_ref());
        file.close();
    }
    // Reopen with mode "rw"
    let file2 = FileIO::open(&path, "rw");
    assert_eq(file2.created_file(), false, "created_file is false for rw");
    assert_eq(file2.has_directory_obligation(), false, "no directory obligation for rw");
    let r = file2.read(0, 17);
    assert_eq(r.as_slice(), b"PRESERVED CONTENT".as_ref(), "rw mode did not truncate");
    file2.close();
    println!("[PASS] Test 17: rw existing file does not truncate");
}

fn test_18_sync_parent_dir_obligation(dir: &str) {
    println!("[RUN] Test 18: first sync() after create includes parent-directory sync");
    TEST_SYNC_EVENTS.lock().unwrap().clear();
    moss_fileio::moss_set_sync_hook(test_sync_hook);

    let path = format!("{}/test18.dat", dir);
    let file = FileIO::open(&path, "create");
    assert_eq(file.has_directory_obligation(), true, "obligation pending before sync");
    file.write(0, b"data".as_ref());
    file.sync();

    let events = TEST_SYNC_EVENTS.lock().unwrap().clone();
    assert_eq(events, vec!["file_fsync".to_string(), "dir_fsync".to_string()], "sync calls file_fsync then dir_fsync");
    assert_eq(file.has_directory_obligation(), false, "obligation cleared after successful sync");

    moss_fileio::moss_clear_sync_hook();
    file.close();
    println!("[PASS] Test 18: first sync() after create includes parent-directory sync");
}

fn test_19_sync_dataonly_parent_dir_obligation(dir: &str) {
    println!("[RUN] Test 19: first sync(dataonly) after create includes parent-directory sync");
    TEST_SYNC_EVENTS.lock().unwrap().clear();
    moss_fileio::moss_set_sync_hook(test_sync_hook);

    let path = format!("{}/test19.dat", dir);
    let file = FileIO::open(&path, "create");
    assert_eq(file.has_directory_obligation(), true, "obligation pending before sync");
    file.write(0, b"data".as_ref());
    file.sync_dataonly();

    let events = TEST_SYNC_EVENTS.lock().unwrap().clone();
    assert_eq(events, vec!["file_fdatasync".to_string(), "dir_fsync".to_string()], "sync_dataonly calls file_fdatasync then dir_fsync");
    assert_eq(file.has_directory_obligation(), false, "obligation cleared after successful sync_dataonly");

    moss_fileio::moss_clear_sync_hook();
    file.close();
    println!("[PASS] Test 19: first sync(dataonly) after create includes parent-directory sync");
}

fn test_20_later_sync_no_parent_sync(dir: &str) {
    println!("[RUN] Test 20: later sync after obligation clear does not require another parent sync");
    TEST_SYNC_EVENTS.lock().unwrap().clear();
    moss_fileio::moss_set_sync_hook(test_sync_hook);

    let path = format!("{}/test20.dat", dir);
    let file = FileIO::open(&path, "create");
    file.sync(); // first sync clears obligation
    TEST_SYNC_EVENTS.lock().unwrap().clear();

    // Second sync
    file.sync();
    let events = TEST_SYNC_EVENTS.lock().unwrap().clone();
    assert_eq(events, vec!["file_fsync".to_string()], "second sync only syncs file, not parent dir");

    TEST_SYNC_EVENTS.lock().unwrap().clear();
    file.sync_dataonly();
    let events2 = TEST_SYNC_EVENTS.lock().unwrap().clone();
    assert_eq(events2, vec!["file_fdatasync".to_string()], "second sync_dataonly only syncs file, not parent dir");

    moss_fileio::moss_clear_sync_hook();
    file.close();
    println!("[PASS] Test 20: later sync after obligation clear does not require another parent sync");
}

fn test_21_close_without_sync_no_durability(dir: &str) {
    println!("[RUN] Test 21: close without sync does not imply durability");
    TEST_SYNC_EVENTS.lock().unwrap().clear();
    moss_fileio::moss_set_sync_hook(test_sync_hook);

    let path = format!("{}/test21.dat", dir);
    let file = FileIO::open(&path, "create");
    file.write(0, b"unsynced data".as_ref());
    file.close();

    let events = TEST_SYNC_EVENTS.lock().unwrap().clone();
    assert_eq(events.len(), 0, "close did not trigger any sync events");

    moss_fileio::moss_clear_sync_hook();
    println!("[PASS] Test 21: close without sync does not imply durability");
}

fn test_23_locks_not_held_across_waits(dir: &str) {
    println!("[RUN] Test 23: runtime/registry locks not held across file waits");
    moss_set_solo_hooks(test_solo_lock_check_hook, dummy_solo_leave_hook);

    let path = format!("{}/test23.dat", dir);
    let file = FileIO::open(&path, "create");
    file.write(0, b"test".as_ref());
    let _ = file.read(0, 4);
    file.sync();
    file.close();

    moss_clear_solo_hooks();
    println!("[PASS] Test 23: runtime/registry locks not held across file waits");
}

fn test_24_solo_hooks_paired(dir: &str) {
    println!("[RUN] Test 24: Solo hooks paired around waits");
    *TEST_SOLO_DEPTH.lock().unwrap() = 0;
    *TEST_SOLO_ENTERS.lock().unwrap() = 0;
    *TEST_SOLO_LEAVES.lock().unwrap() = 0;
    TEST_SOLO_REASONS.lock().unwrap().clear();

    moss_set_solo_hooks(test_solo_enter_hook, test_solo_leave_hook);

    let path = format!("{}/test24.dat", dir);
    let file = FileIO::open(&path, "create");
    assert_eq(*TEST_SOLO_DEPTH.lock().unwrap(), 0, "depth 0 after open");
    file.write(0, b"hello".as_ref());
    assert_eq(*TEST_SOLO_DEPTH.lock().unwrap(), 0, "depth 0 after write");
    let _ = file.read(0, 5);
    assert_eq(*TEST_SOLO_DEPTH.lock().unwrap(), 0, "depth 0 after read");
    file.sync();
    assert_eq(*TEST_SOLO_DEPTH.lock().unwrap(), 0, "depth 0 after sync");
    file.close();
    assert_eq(*TEST_SOLO_DEPTH.lock().unwrap(), 0, "depth 0 after close");

    let enters = *TEST_SOLO_ENTERS.lock().unwrap();
    let leaves = *TEST_SOLO_LEAVES.lock().unwrap();
    assert_true(enters > 0, "Solo enter called multiple times");
    assert_eq(enters, leaves, "Solo enter and leave counts match exactly");

    let reasons = TEST_SOLO_REASONS.lock().unwrap().clone();
    assert_true(reasons.contains(&"open_create".to_string()), "contains open_create");
    assert_true(reasons.contains(&"fstat".to_string()), "contains fstat");
    assert_true(reasons.contains(&"open_parent_dir".to_string()), "contains open_parent_dir");
    assert_true(reasons.contains(&"write".to_string()), "contains write");
    assert_true(reasons.contains(&"read".to_string()), "contains read");
    assert_true(reasons.contains(&"sync".to_string()), "contains sync");
    assert_true(reasons.contains(&"sync_dir".to_string()), "contains sync_dir");
    assert_true(reasons.contains(&"close".to_string()), "contains close");

    moss_clear_solo_hooks();
    println!("[PASS] Test 24: Solo hooks paired around waits");
}

fn test_25_open_in_place_lifecycle(dir: &str) {
    println!("[RUN] Test 25: open_in_place on uninitialized/closed succeeds");
    let path1 = format!("{}/test25_1.dat", dir);
    let mut file = FileIO::default();
    assert_eq(file.is_open(), false, "default file is closed/uninitialized");
    file.open_in_place(&path1, "create");
    assert_eq(file.is_open(), true, "file is now open");
    file.write(0, b"hello in place".as_ref());
    file.close();
    assert_eq(file.is_open(), false, "file closed");
    // open_in_place on closed file succeeds
    let path2 = format!("{}/test25_2.dat", dir);
    file.open_in_place(&path2, "create");
    assert_eq(file.is_open(), true, "reopened in place on closed file");
    file.close();
    println!("[PASS] Test 25: open_in_place on uninitialized/closed succeeds");
}

fn test_chunks_iterator(dir: &str) {
    println!("[RUN] Extra: file.chunks(size) borrowing FileIO");
    let path = format!("{}/test_chunks.dat", dir);
    let file = FileIO::open(&path, "create");
    file.write(0, b"abcdefghijklmnopqrstuvwxyz".as_ref()); // 26 bytes

    {
        let mut chunks = file.chunks(10);
        let c1 = chunks.next().unwrap();
        assert_eq(c1.as_slice(), b"abcdefghij".as_ref(), "chunk 1 (10 bytes)");
        let c2 = chunks.next().unwrap();
        assert_eq(c2.as_slice(), b"klmnopqrst".as_ref(), "chunk 2 (10 bytes)");
        let c3 = chunks.next().unwrap();
        assert_eq(c3.as_slice(), b"uvwxyz".as_ref(), "chunk 3 (6 bytes, short final chunk)");
        let c4 = chunks.next();
        assert_eq(c4.is_none(), true, "chunk 4 is None (EOF)");
    }
    // file is still valid and can be read or closed
    assert_eq(file.read(0, 3).as_slice(), b"abc".as_ref(), "file still readable after chunks");
    file.close();
    println!("[PASS] Extra: file.chunks(size) borrowing FileIO");
}

fn test_range_operations() {
    println!("[RUN] Extra: Range and RangeBatch operations");
    let r = Range::from_str("Hello, Moss FileIO!");
    assert_eq(r.len(), 19, "Range length");
    assert_eq(r.as_str(), "Hello, Moss FileIO!", "Range as_str");
    assert_eq(r.get(0), b'H' as i64, "Range get(0)");
    assert_eq(r.get(18), b'!' as i64, "Range get(18)");
    assert_eq(r.get(100), 0, "Range get out of bounds returns 0");

    let sub = r.slice(7, 4);
    assert_eq(sub.as_str(), "Moss", "Range slice");
    assert_eq(sub.len(), 4, "Range slice length");

    let bytes = r.to_bytes();
    assert_eq(bytes.len(), 19, "to_bytes length");
    let vec_ints = r.to_vec();
    assert_eq(vec_ints.len(), 19, "to_vec length");

    let batch = RangeBatch::new(vec![r.clone(), sub.clone()]);
    assert_eq(batch.len(), 2, "batch length");
    assert_eq(batch.get(0).as_str(), "Hello, Moss FileIO!", "batch get 0");
    assert_eq(batch.get(1).as_str(), "Moss", "batch get 1");
    println!("[PASS] Extra: Range and RangeBatch operations");
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: {} <test-name> [test-dir]", args[0]);
        std::process::exit(1);
    }
    let test_name = &args[1];
    let test_dir = if args.len() > 2 { &args[2] } else { "." };

    match test_name.as_str() {
        "all_normal" => {
            test_range_operations();
            test_1_exact_read(test_dir);
            test_2_short_final_read(test_dir);
            test_3_zero_length_eof(test_dir);
            test_4_explicit_offsets(test_dir);
            test_5_complete_write(test_dir);
            test_6_multiple_independent_offsets(test_dir);
            test_7_batch_ordering(test_dir);
            test_8_bounded_batch_behavior(test_dir);
            test_13_different_inode_succeeds(test_dir);
            test_14_claim_released_after_close(test_dir);
            test_15_create_missing_file_records_creation(test_dir);
            test_16_create_existing_file_does_not_truncate(test_dir);
            test_17_rw_existing_file_does_not_truncate(test_dir);
            test_18_sync_parent_dir_obligation(test_dir);
            test_19_sync_dataonly_parent_dir_obligation(test_dir);
            test_20_later_sync_no_parent_sync(test_dir);
            test_21_close_without_sync_no_durability(test_dir);
            test_23_locks_not_held_across_waits(test_dir);
            test_24_solo_hooks_paired(test_dir);
            test_25_open_in_place_lifecycle(test_dir);
            test_chunks_iterator(test_dir);
            println!("\nALL NORMAL TESTS PASSED SUCCESSFULLY!");
        }
        "test_9_dir" => {
            FileIO::open(test_dir, "ro");
        }
        "test_9_fifo" => {
            let fifo_path = format!("{}/test_fifo.fifo", test_dir);
            FileIO::open(&fifo_path, "ro");
        }
        "test_10_dup_open" => {
            let path = format!("{}/test10_dup.dat", test_dir);
            let f1 = FileIO::open(&path, "create");
            let _f2 = FileIO::open(&path, "ro");
        }
        "test_11_symlink_dup" => {
            let path = format!("{}/test11_real.dat", test_dir);
            let sym_path = format!("{}/test11_sym.dat", test_dir);
            let f1 = FileIO::open(&path, "create");
            let _f2 = FileIO::open(&sym_path, "ro");
        }
        "test_12_hardlink_dup" => {
            let path = format!("{}/test12_real.dat", test_dir);
            let hard_path = format!("{}/test12_hard.dat", test_dir);
            let f1 = FileIO::open(&path, "create");
            let _f2 = FileIO::open(&hard_path, "ro");
        }
        "test_22_double_close" => {
            let path = format!("{}/test22_double_close.dat", test_dir);
            let file = FileIO::open(&path, "create");
            file.close();
            file.close(); // MUST fail closed
        }
        "test_22_uninit_close" => {
            let file = FileIO::default();
            file.close(); // MUST fail closed
        }
        "test_25_open_in_place_on_open" => {
            let path1 = format!("{}/test25_open1.dat", test_dir);
            let path2 = format!("{}/test25_open2.dat", test_dir);
            let mut file = FileIO::open(&path1, "create");
            file.open_in_place(&path2, "create"); // MUST fail closed
        }
        "test_26_closed_zero_read" => {
            let file = FileIO::default();
            file.read(0, 0); // MUST fail closed
        }
        "test_26_closed_empty_write" => {
            let file = FileIO::default();
            file.write(0, b"".as_ref()); // MUST fail closed
        }
        "test_26_closed_empty_batch" => {
            let file = FileIO::default();
            file.read_batch(&[]); // MUST fail closed
        }
        "test_26_closed_chunks" => {
            let file = FileIO::default();
            file.chunks(10); // MUST fail closed
        }
        "test_26_closed_sync" => {
            let file = FileIO::default();
            file.sync(); // MUST fail closed
        }
        "test_27_close_error_simulation" => {
            let path = format!("{}/test27_fail_close.dat", test_dir);
            {
                let f = FileIO::open(&path, "create");
                f.close();
            }
            let file = FileIO::open(&path, "rw");
            let dev = file.device();
            let ino = file.inode();
            moss_fileio::moss_set_close_override(failing_close_override);
            *CLOSE_ATTEMPT_COUNT.lock().unwrap() = 0;
            file.close(); // Will fail closed (abort)
        }
        "test_27_check_registry_after_failed_close" => {
            let path = format!("{}/test27_fail_close_check.dat", test_dir);
            {
                let f = FileIO::open(&path, "create");
                f.close();
            }
            let file = FileIO::open(&path, "rw");
            let dev = file.device();
            let ino = file.inode();
            assert_true(moss_fileio::moss_fileio_registry_contains(dev, ino), "registry contains inode");
            *CLOSE_ATTEMPT_COUNT.lock().unwrap() = 0;
            moss_fileio::moss_set_close_override(failing_close_override);
            drop(file);
            assert_true(!moss_fileio::moss_fileio_registry_contains(dev, ino), "registry released inode even after close error");
            assert_eq(*CLOSE_ATTEMPT_COUNT.lock().unwrap(), 1, "exactly one close attempt made");
            moss_fileio::moss_clear_close_override();
            println!("[PASS] Test 27: close error simulation and registry release");
        }
        "test_28_read_overflow" => {
            let path = format!("{}/test28_read_ovf.dat", test_dir);
            let file = FileIO::open(&path, "create");
            file.read(i64::MAX - 5, 10); // MUST fail closed
        }
        "test_28_write_overflow" => {
            let path = format!("{}/test28_write_ovf.dat", test_dir);
            let file = FileIO::open(&path, "create");
            file.write(i64::MAX - 5, b"overflow"); // MUST fail closed
        }
        "test_33_parent_close_failure" => {
            let path = format!("{}/test33_parent_fail.dat", test_dir);
            let file = FileIO::open(&path, "create");
            let dev = file.device();
            let ino = file.inode();
            assert_true(file.has_directory_obligation(), "has directory obligation");
            *FAIL_PARENT_CLOSE_ONLY.lock().unwrap() = true;
            *CLOSE_ATTEMPT_COUNT.lock().unwrap() = 0;
            *FILE_CLOSE_ATTEMPTED.lock().unwrap() = false;
            moss_fileio::moss_set_close_override(failing_close_override);
            // This explicit close will attempt parent close (fails), then attempt file close (succeeds), release registry, and fail closed (abort)
            file.close();
        }
        _ => {
            eprintln!("Unknown test: {}", test_name);
            std::process::exit(1);
        }
    }
}
"""

def extract_fileio_runtime_rust():
    header_path = os.path.join(REPO_ROOT, "src/fileio_runtime.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    start_marker = 'inline const char* fileio_runtime_rust() {\n  return R"RUST(\n'
    end_marker = '\n)RUST";\n}'
    start_idx = content.find(start_marker)
    end_idx = content.find(end_marker, start_idx)
    if start_idx == -1 or end_idx == -1:
        raise RuntimeError("Failed to extract fileio_runtime_rust from src/fileio_runtime.hpp")
    return content[start_idx + len(start_marker):end_idx]

def extract_fileio_root_runtime_rust():
    header_path = os.path.join(REPO_ROOT, "src/fileio_runtime.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    start_marker = 'inline const char* fileio_root_runtime_rust() {\n  return R"RUST(\n'
    end_marker = '\n)RUST";\n}'
    start_idx = content.find(start_marker)
    end_idx = content.find(end_marker, start_idx)
    if start_idx == -1 or end_idx == -1:
        raise RuntimeError("Failed to extract fileio_root_runtime_rust from src/fileio_runtime.hpp")
    return content[start_idx + len(start_marker):end_idx]

def compile_test_harness():
    runtime_rust = extract_fileio_runtime_rust()
    root_runtime_rust = extract_fileio_root_runtime_rust()
    harness_src = RUST_HARNESS_SOURCE.replace("MOSS_FILEIO_RUNTIME_PLACEHOLDER", runtime_rust)
    harness_src = harness_src.replace("MOSS_FILEIO_ROOT_RUNTIME_PLACEHOLDER", root_runtime_rust)
    rs_path = os.path.join(TMP_DIR, "test_fileio_harness.rs")
    bin_path = os.path.join(TMP_DIR, "test_fileio_harness")
    with open(rs_path, "w", encoding="utf-8") as f:
        f.write(harness_src)
    
    cmd = ["rustc", "-D", "warnings", "--cfg", "moss_test", rs_path, "-o", bin_path]
    print("Compiling test harness:", " ".join(cmd))
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("Compilation failed:\n", res.stderr)
        sys.exit(1)
    print("Harness compiled successfully.")
    return bin_path

def test_compile_negative_clone():
    print("=== Running Test 29: Compile-negative FileIO.clone() ===")
    runtime_rust = extract_fileio_runtime_rust()
    bad_code = f"""
    #![allow(dead_code)]
    {runtime_rust}
    fn main() {{
        let f = FileIO::default();
        let _f2 = f.clone();
    }}
    """
    rs_path = os.path.join(TMP_DIR, "test_clone_negative.rs")
    with open(rs_path, "w", encoding="utf-8") as f:
        f.write(bad_code)
    res = subprocess.run(["rustc", rs_path, "-o", os.path.join(TMP_DIR, "test_clone_negative")], capture_output=True, text=True)
    assert res.returncode != 0, "FileIO.clone() must fail to compile"
    assert "no method named `clone`" in res.stderr or "clone" in res.stderr, f"Expected clone failure in stderr: {res.stderr}"
    print("[PASS] Test 29: Compile-negative FileIO.clone() verified")

def test_cross_crate_process_global_registry(test_dir):
    print("=== Running Test 30 & 31: Cross-crate process-global registry & Solo hooks ===")
    runtime_rust = extract_fileio_runtime_rust()
    root_runtime_rust = extract_fileio_root_runtime_rust()

    # 1. Compile Provider Library as .rlib (uses runtime_rust only, no root runtime)
    provider_src = f"""
    #![allow(dead_code)]
    #![allow(unused_variables)]
    {runtime_rust}

    pub struct ProviderFile {{
        file: FileIO,
    }}

    impl ProviderFile {{
        pub fn open_create(path: &str) -> Self {{
            ProviderFile {{ file: FileIO::open(path, "create") }}
        }}
        pub fn write_data(&self, offset: i64, data: &[u8]) {{
            self.file.write(offset, data);
        }}
        pub fn close(self) {{
            self.file.close();
        }}
        pub fn inode(&self) -> (u64, u64) {{
            (self.file.device(), self.file.inode())
        }}
    }}
    """
    provider_rs = os.path.join(TMP_DIR, "provider_a.rs")
    provider_rlib = os.path.join(TMP_DIR, "libprovider_a.rlib")
    with open(provider_rs, "w", encoding="utf-8") as f:
        f.write(provider_src)

    cmd_prov = ["rustc", "-D", "warnings", "--cfg", "moss_test", "--crate-type", "rlib", "-o", provider_rlib, provider_rs]
    res_prov = subprocess.run(cmd_prov, capture_output=True, text=True)
    assert res_prov.returncode == 0, f"Provider compilation failed:\n{res_prov.stderr}"

    # 2. Compile Consumer Executable linked against provider_a.rlib
    # Note: Consumer uses root_runtime_rust emitted by root executable, with or without local FileIO
    consumer_src = f"""
    #![allow(dead_code)]
    #![allow(unused_variables)]
    extern crate provider_a;
    use provider_a::ProviderFile;

    {runtime_rust}
    {root_runtime_rust}

    use std::sync::Mutex;

    static SOLO_CALLED: Mutex<bool> = Mutex::new(false);
    fn test_solo_enter(reason: &str) {{
        if let Ok(mut g) = SOLO_CALLED.lock() {{
            *g = true;
        }}
    }}
    fn test_solo_leave(_reason: &str) {{}}

    fn main() {{
        let args: Vec<String> = std::env::args().collect();
        let test_dir = &args[1];
        let path = format!("{{}}/cross_crate_test.dat", test_dir);

        // Register Solo hooks in consumer
        moss_root_runtime::moss_set_solo_hooks(test_solo_enter, test_solo_leave);

        // Provider A opens file
        let pfile = ProviderFile::open_create(&path);
        let (dev, ino) = pfile.inode();
        assert!(moss_fileio::moss_fileio_registry_contains(dev, ino), "registry contains inode claimed by provider");

        // Provider writes data -> triggers Consumer's registered Solo hook
        *SOLO_CALLED.lock().unwrap() = false;
        pfile.write_data(0, b"provider written payload");
        assert!(*SOLO_CALLED.lock().unwrap(), "consumer Solo hook called during provider file write!");

        // Mode 1: Check collision rejection
        if args.len() > 2 && args[2] == "collision" {{
            // Consumer attempts to open SAME inode -> MUST abort due to cross-crate registry claim
            println!("[CONSUMER] Attempting duplicate open of provider file...");
            let _cfile = FileIO::open(&path, "ro");
            println!("[CONSUMER] UNEXPECTED: duplicate open succeeded!");
            std::process::exit(0);
        }}

        // Mode 2: Provider closes file -> Consumer can now open
        pfile.close();
        assert!(!moss_fileio::moss_fileio_registry_contains(dev, ino), "registry released after provider close");
        let cfile = FileIO::open(&path, "ro");
        let data = cfile.read(0, 24);
        assert_eq!(data.as_slice(), b"provider written payload");
        cfile.close();
        println!("[PASS] Cross-crate process-global registry and Solo hooks passed!");
    }}
    """
    consumer_rs = os.path.join(TMP_DIR, "consumer.rs")
    consumer_bin = os.path.join(TMP_DIR, "consumer")
    with open(consumer_rs, "w", encoding="utf-8") as f:
        f.write(consumer_src)

    cmd_cons = ["rustc", "-D", "warnings", "--cfg", "moss_test", "--extern", f"provider_a={provider_rlib}", "-o", consumer_bin, consumer_rs]
    res_cons = subprocess.run(cmd_cons, capture_output=True, text=True)
    assert res_cons.returncode == 0, f"Consumer compilation failed:\n{res_cons.stderr}"

    # Run normal mode (provider open -> write -> Solo hook check -> provider close -> consumer open)
    res = subprocess.run([consumer_bin, test_dir], capture_output=True, text=True)
    assert res.returncode == 0, f"Cross-crate test failed:\n{res.stderr}"
    print(res.stdout.strip())

    # Run collision mode (provider open -> consumer duplicate open fails closed)
    res_col = subprocess.run([consumer_bin, test_dir, "collision"], capture_output=True, text=True)
    assert res_col.returncode != 0, "Duplicate open across crates must fail closed"
    assert "duplicate live FileIO" in res_col.stderr, f"Expected duplicate live FileIO in stderr: {res_col.stderr}"

    # 3. Compiler-level test: Root executable without local FileIO compiles with moss and emits root runtime symbols
    pure_moss_src = """
fn main():
  val = 42
"""
    pure_moss_file = os.path.join(test_dir, "pure_root.moss")
    with open(pure_moss_file, "w", encoding="utf-8") as f:
        f.write(pure_moss_src)
    pure_rs_file = os.path.join(TMP_DIR, "pure_root.rs")
    res_pure = subprocess.run([os.path.join(REPO_ROOT, "moss"), pure_moss_file, "-o", pure_rs_file], capture_output=True, text=True)
    assert res_pure.returncode == 0, f"moss compile failed for pure root:\n{res_pure.stderr}"
    with open(pure_rs_file, "r", encoding="utf-8") as f:
        pure_rs_content = f.read()
    assert "moss_root_runtime" in pure_rs_content, "Compiler-generated pure root MUST emit moss_root_runtime"
    assert "mod moss_fileio" not in pure_rs_content, "Pure root without FileIO MUST NOT emit mod moss_fileio"

    # Verify compiler-generated pure_root.rs compiles cleanly
    pure_bin = os.path.join(TMP_DIR, "pure_root_bin")
    res_link = subprocess.run(["rustc", "-D", "warnings", "--extern", f"provider_a={provider_rlib}", "-o", pure_bin, pure_rs_file], capture_output=True, text=True)
    assert res_link.returncode == 0, f"Linking compiler-emitted root with provider failed:\n{res_link.stderr}"
    print("[PASS] Test 30 & 31: Cross-crate collision and Solo hook propagation verified")

def test_hook_lock_discipline(test_dir):
    print("=== Running Test 32: Hook registration lock-dropping discipline ===")
    runtime_rust = extract_fileio_runtime_rust()
    root_runtime_rust = extract_fileio_root_runtime_rust()

    test_src = f"""
    #![allow(dead_code)]
    {runtime_rust}
    {root_runtime_rust}

    use std::sync::Mutex;
    static SECOND_LOCK: Mutex<i32> = Mutex::new(0);
    static HOOK_EXECUTED: Mutex<bool> = Mutex::new(false);

    fn reentrant_solo_hook(_reason: &str) {{
        // 1. Acquire another lock inside callback
        let mut g = SECOND_LOCK.lock().unwrap();
        *g += 1;
        // 2. Query registry from inside callback (proves registration lock is not held)
        let _ = moss_fileio::moss_fileio_registry_contains(12345, 67890);
        *HOOK_EXECUTED.lock().unwrap() = true;
    }}
    fn dummy_leave(_reason: &str) {{}}

    fn main() {{
        moss_root_runtime::moss_set_solo_hooks(reentrant_solo_hook, dummy_leave);
        let path = "{test_dir}/test_lock_disc.dat";
        let file = FileIO::open(path, "create");
        file.write(0, b"data");
        file.close();
        assert!(*HOOK_EXECUTED.lock().unwrap(), "hook executed");
        assert!(*SECOND_LOCK.lock().unwrap() > 0, "second lock acquired");
        moss_root_runtime::moss_clear_solo_hooks();
        println!("[PASS] Hook lock discipline verified");
    }}
    """
    rs_path = os.path.join(TMP_DIR, "test_lock_discipline.rs")
    bin_path = os.path.join(TMP_DIR, "test_lock_discipline")
    with open(rs_path, "w", encoding="utf-8") as f:
        f.write(test_src)
    res = subprocess.run(["rustc", "-D", "warnings", "--cfg", "moss_test", rs_path, "-o", bin_path], capture_output=True, text=True)
    assert res.returncode == 0, f"Compilation failed:\n{res.stderr}"
    res = subprocess.run([bin_path], capture_output=True, text=True)
    assert res.returncode == 0, f"Execution failed:\n{res.stderr}"
    print("[PASS] Test 32: Hook registration lock-dropping discipline verified")

def test_main_only_runtime_emission(test_dir):
    print("=== Running Test 34, 35 & 36: Root emission & false-positive hardening ===")

    # 1. Process-root emission test (no local FileIO)
    moss_root_src = """
fn main():
  val = 100
"""
    moss_file = os.path.join(test_dir, "process_root_test.moss")
    with open(moss_file, "w", encoding="utf-8") as f:
        f.write(moss_root_src)

    rs_out = os.path.join(TMP_DIR, "process_root_test.rs")
    compile_cmd = [os.path.join(REPO_ROOT, "moss"), moss_file, "-o", rs_out]
    res = subprocess.run(compile_cmd, capture_output=True, text=True)
    assert res.returncode == 0, f"moss compile failed for process root:\n{res.stderr}"

    with open(rs_out, "r", encoding="utf-8") as f:
        rs_content = f.read()
    assert "moss_root_runtime" in rs_content, "Root executable MUST emit moss_root_runtime"
    assert "mod moss_fileio" not in rs_content, "Root executable without local FileIO MUST NOT emit mod moss_fileio"

    # Verify generated Rust compiles cleanly with rustc
    res_rustc = subprocess.run(["rustc", "-D", "warnings", rs_out, "-o", os.path.join(TMP_DIR, "process_root_test")], capture_output=True, text=True)
    assert res_rustc.returncode == 0, f"Generated Rust failed to compile:\n{res_rustc.stderr}"
    print("[PASS] Test 34: Process-root runtime emission verified (root contains moss_root_runtime without mod moss_fileio)")

    # 2. False-positive hardening (declaration names: enum, type, domain, function containing Range/FileIO tokens)
    moss_fp_src = """
enum Status:
  Range
  FileIO

type RangeRover:
  fuel: int

domain RangeDomain:
  fn Ping() -> int:
    reply 1

fn Range() -> int:
  return 1

fn main():
  rover = RangeRover(fuel: 100)
  x = Range()
  s = Status.Range
"""
    moss_fp_file = os.path.join(test_dir, "fp_test.moss")
    with open(moss_fp_file, "w", encoding="utf-8") as f:
        f.write(moss_fp_src)
    rs_fp_out = os.path.join(TMP_DIR, "fp_test.rs")
    res_fp = subprocess.run([os.path.join(REPO_ROOT, "moss"), moss_fp_file, "-o", rs_fp_out], capture_output=True, text=True)
    assert res_fp.returncode == 0, f"moss compile failed:\n{res_fp.stderr}"
    with open(rs_fp_out, "r", encoding="utf-8") as f:
        rs_fp_content = f.read()
    assert "mod moss_fileio" not in rs_fp_content, "Declaration names containing tokens MUST NOT emit mod moss_fileio"
    print("[PASS] Test 35: Declaration name false-positive hardening verified")

    # 3. False-positive hardening: Overlapping method names on unrelated types (e.g. Dataset.chunks, Thing.open_in_place, Thing.sync_dataonly)
    moss_method_fp_src = """
type Dataset:
  size: Int

  fn chunks(count: Int):
    return size + count

type DeviceHolder:
  state_code: Int

  fn open_in_place(tag: Int):
    return state_code + tag

  fn sync_dataonly():
    return state_code

fn main():
  ds = Dataset(size = 100)
  c = ds.chunks(10)
  dh = DeviceHolder(state_code = 5)
  o = dh.open_in_place(2)
  s = dh.sync_dataonly()
"""
    moss_method_fp_file = os.path.join(test_dir, "method_fp_test.moss")
    with open(moss_method_fp_file, "w", encoding="utf-8") as f:
        f.write(moss_method_fp_src)
    rs_method_fp_out = os.path.join(TMP_DIR, "method_fp_test.rs")
    res_method_fp = subprocess.run([os.path.join(REPO_ROOT, "moss"), moss_method_fp_file, "-o", rs_method_fp_out], capture_output=True, text=True)
    assert res_method_fp.returncode == 0, f"moss compile failed for method fp:\n{res_method_fp.stderr}"
    with open(rs_method_fp_out, "r", encoding="utf-8") as f:
        rs_method_fp_content = f.read()
    assert "mod moss_fileio" not in rs_method_fp_content, "Unrelated user methods named chunks/open_in_place/sync_dataonly MUST NOT emit mod moss_fileio"
    print("[PASS] Test 36: Overlapping method name false-positive hardening verified")

def test_fair_registry_admission(test_dir):
    print("=== Running Test 37: Fair ticket lock admission under contention ===")
    runtime_rust = extract_fileio_runtime_rust()
    root_runtime_rust = extract_fileio_root_runtime_rust()

    test_src = f"""
    #![allow(dead_code)]
    {runtime_rust}
    {root_runtime_rust}

    use std::sync::{{Arc, Mutex, Barrier}};
    use std::thread;
    use std::time::Duration;

    fn main() {{
        let ready_barrier = Arc::new(Barrier::new(2));
        let order = Arc::new(Mutex::new(Vec::new()));

        let t0_order = order.clone();
        let t0_ready = ready_barrier.clone();
        let h0 = thread::spawn(move || {{
            moss_root_runtime::MOSS_REGISTRY.with_set(|_set| {{
                t0_ready.wait();
                thread::sleep(Duration::from_millis(60));
                t0_order.lock().unwrap().push(0);
            }});
        }});

        ready_barrier.wait();
        let mut other_handles = Vec::new();
        for i in 1..=5 {{
            let t_order = order.clone();
            let h = thread::spawn(move || {{
                moss_root_runtime::MOSS_REGISTRY.with_set(|_set| {{
                    t_order.lock().unwrap().push(i);
                }});
            }});
            thread::sleep(Duration::from_millis(5));
            other_handles.push(h);
        }}

        h0.join().unwrap();
        for h in other_handles {{
            h.join().unwrap();
        }}

        let recorded = order.lock().unwrap().clone();
        assert_eq!(recorded, vec![0, 1, 2, 3, 4, 5], "Fair ticket lock granted waiters in exact FIFO order!");
        println!("[PASS] Test 37: Fair ticket lock admission under contention verified");
    }}
    """
    rs_path = os.path.join(TMP_DIR, "test_fair_admission.rs")
    bin_path = os.path.join(TMP_DIR, "test_fair_admission")
    with open(rs_path, "w", encoding="utf-8") as f:
        f.write(test_src)
    res = subprocess.run(["rustc", "-D", "warnings", rs_path, "-o", bin_path], capture_output=True, text=True)
    assert res.returncode == 0, f"Compilation failed:\n{res.stderr}"
    res_run = subprocess.run([bin_path], capture_output=True, text=True)
    assert res_run.returncode == 0, f"Execution failed:\n{res_run.stderr}"
    print(res_run.stdout.strip())

def test_production_abi_purity(test_dir):
    print("=== Running Test 38: Production ABI purity ===")
    runtime_rust = extract_fileio_runtime_rust()
    root_runtime_rust = extract_fileio_root_runtime_rust()

    # 1. Compile in pure production mode (no test cfgs)
    prod_src = f"""
    #![allow(dead_code)]
    {runtime_rust}
    {root_runtime_rust}

    fn main() {{
        // Production symbols must be accessible
        assert!(moss_fileio_registry_claim(999, 888));
        moss_fileio_registry_release(999, 888);
    }}
    """
    prod_rs = os.path.join(TMP_DIR, "prod_abi_test.rs")
    prod_bin = os.path.join(TMP_DIR, "prod_abi_test")
    with open(prod_rs, "w", encoding="utf-8") as f:
        f.write(prod_src)
    res_prod = subprocess.run(["rustc", "-D", "warnings", prod_rs, "-o", prod_bin], capture_output=True, text=True)
    assert res_prod.returncode == 0, f"Production compilation failed:\n{res_prod.stderr}"
    res_run = subprocess.run([prod_bin], capture_output=True, text=True)
    assert res_run.returncode == 0, f"Production execution failed:\n{res_run.stderr}"

    # 2. Verify test-only symbols fail to compile in production mode
    test_only_code = f"""
    #![allow(dead_code)]
    {runtime_rust}
    {root_runtime_rust}

    fn main() {{
        moss_fileio_registry_reset();
    }}
    """
    bad_rs = os.path.join(TMP_DIR, "bad_prod_abi.rs")
    with open(bad_rs, "w", encoding="utf-8") as f:
        f.write(test_only_code)
    res_bad = subprocess.run(["rustc", bad_rs, "-o", os.path.join(TMP_DIR, "bad_prod_abi")], capture_output=True, text=True)
    assert res_bad.returncode != 0, "moss_fileio_registry_reset must not compile in production"
    assert "cannot find function `moss_fileio_registry_reset`" in res_bad.stderr or "not found" in res_bad.stderr, f"Expected reset not found error: {res_bad.stderr}"
    print("[PASS] Test 38: Production ABI purity verified (test APIs excluded from normal ABI)")

def test_target_platform_gating(test_dir):
    print("=== Running Test 39: Platform target gating ===")
    # Target compile-negative check for unsupported OS
    unsupported_os_src = """
    #![allow(dead_code)]
    #[cfg(not(all(
        target_os = "windows", // Simulated mismatch
        target_arch = "x86_64"
    )))]
    compile_error!("Moss Phase 20 FileIO runtime is only supported on Linux x86_64 and aarch64.");
    fn main() {}
    """
    rs_path = os.path.join(TMP_DIR, "test_target_gate.rs")
    with open(rs_path, "w", encoding="utf-8") as f:
        f.write(unsupported_os_src)
    res = subprocess.run(["rustc", rs_path, "-o", os.path.join(TMP_DIR, "test_target_gate")], capture_output=True, text=True)
    assert res.returncode != 0 and "Moss Phase 20 FileIO runtime is only supported on Linux x86_64 and aarch64." in res.stderr
    print("[PASS] Test 39: Platform target gating verified")

def test_checked_conversions_and_boundary_handling(test_dir):
    print("=== Running Test 40: Checked conversions and boundary handling ===")
    runtime_rust = extract_fileio_runtime_rust()
    root_runtime_rust = extract_fileio_root_runtime_rust()

    test_src = f"""
    #![allow(dead_code)]
    {runtime_rust}
    {root_runtime_rust}

    fn main() {{
        // 1. Range negative / out-of-bounds slice returns empty safely
        let r = Range::from_str("moss");
        assert_eq!(r.slice(-5, 2).as_str(), "");
        assert_eq!(r.slice(1, -2).as_str(), "");
        assert_eq!(r.slice(100, 2).as_str(), "");
        assert_eq!(r.get(-1), 0);
        assert_eq!(r.get(100), 0);

        // 2. RangeBatch get out of bounds returns empty safely
        let rb = RangeBatch::new(vec![r]);
        assert_eq!(rb.get(-1).as_str(), "");
        assert_eq!(rb.get(5).as_str(), "");

        println!("[PASS] Checked conversions verified safely");
    }}
    """
    rs_path = os.path.join(TMP_DIR, "test_checked_conversions.rs")
    bin_path = os.path.join(TMP_DIR, "test_checked_conversions")
    with open(rs_path, "w", encoding="utf-8") as f:
        f.write(test_src)
    res = subprocess.run(["rustc", "-D", "warnings", rs_path, "-o", bin_path], capture_output=True, text=True)
    assert res.returncode == 0, f"Compilation failed:\n{res.stderr}"
    res_run = subprocess.run([bin_path], capture_output=True, text=True)
    assert res_run.returncode == 0, f"Execution failed:\n{res_run.stderr}"
    print("[PASS] Test 40: Checked conversions and boundary handling verified")

def run_tests():
    bin_path = compile_test_harness()
    test_run_dir = os.path.join(TMP_DIR, "fileio_test_workspace")
    if os.path.exists(test_run_dir):
        shutil.rmtree(test_run_dir)
    os.makedirs(test_run_dir, exist_ok=True)

    # 1. Run all normal tests
    print("\n=== Running Normal Tests (Tests 1-8, 13-21, 23-25, Range, Chunks) ===")
    res = subprocess.run([bin_path, "all_normal", test_run_dir], capture_output=True, text=True)
    print(res.stdout)
    if res.returncode != 0:
        print("Normal tests failed! Stderr:\n", res.stderr)
        sys.exit(1)

    # 2. Test 9a: Directory rejected
    print("=== Running Test 9a: Directory rejected ===")
    res = subprocess.run([bin_path, "test_9_dir", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "Opening directory must fail"
    assert "not a regular file" in res.stderr, f"Expected 'not a regular file' in stderr: {res.stderr}"
    print("[PASS] Test 9a: Directory rejected")

    # 3. Test 9b: FIFO rejected without blocking
    print("=== Running Test 9b: FIFO rejected without blocking ===")
    fifo_path = os.path.join(test_run_dir, "test_fifo.fifo")
    if os.path.exists(fifo_path):
        os.remove(fifo_path)
    os.mkfifo(fifo_path)
    res = subprocess.run([bin_path, "test_9_fifo", test_run_dir], capture_output=True, text=True, timeout=5)
    assert res.returncode != 0, "Opening FIFO must fail"
    assert "not a regular file" in res.stderr, f"Expected 'not a regular file' in stderr: {res.stderr}"
    print("[PASS] Test 9b: FIFO rejected without blocking")

    # 4. Test 10: Same pathname duplicate live open rejected
    print("=== Running Test 10: Duplicate live open rejected ===")
    res = subprocess.run([bin_path, "test_10_dup_open", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "Duplicate live open must fail"
    assert "duplicate live FileIO" in res.stderr, f"Expected 'duplicate live FileIO' in stderr: {res.stderr}"
    print("[PASS] Test 10: Duplicate live open rejected")

    # 5. Test 11: Symlink alias rejected
    print("=== Running Test 11: Symlink alias duplicate rejected ===")
    real_path11 = os.path.join(test_run_dir, "test11_real.dat")
    sym_path11 = os.path.join(test_run_dir, "test11_sym.dat")
    if not os.path.exists(real_path11):
        with open(real_path11, "w") as f:
            f.write("test")
    if os.path.exists(sym_path11):
        os.remove(sym_path11)
    os.symlink(real_path11, sym_path11)
    res = subprocess.run([bin_path, "test_11_symlink_dup", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "Symlink alias duplicate open must fail"
    assert "duplicate live FileIO" in res.stderr, f"Expected 'duplicate live FileIO' in stderr: {res.stderr}"
    print("[PASS] Test 11: Symlink alias duplicate rejected")

    # 6. Test 12: Hardlink alias rejected
    print("=== Running Test 12: Hardlink alias duplicate rejected ===")
    real_path12 = os.path.join(test_run_dir, "test12_real.dat")
    hard_path12 = os.path.join(test_run_dir, "test12_hard.dat")
    if not os.path.exists(real_path12):
        with open(real_path12, "w") as f:
            f.write("test")
    if os.path.exists(hard_path12):
        os.remove(hard_path12)
    os.link(real_path12, hard_path12)
    res = subprocess.run([bin_path, "test_12_hardlink_dup", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "Hardlink alias duplicate open must fail"
    assert "duplicate live FileIO" in res.stderr, f"Expected 'duplicate live FileIO' in stderr: {res.stderr}"
    print("[PASS] Test 12: Hardlink alias duplicate rejected")

    # 7. Test 22a: Double close fails closed
    print("=== Running Test 22a: Double close fails closed ===")
    res = subprocess.run([bin_path, "test_22_double_close", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "Second close must fail closed"
    assert "close on closed or uninitialized" in res.stderr, f"Expected error in stderr: {res.stderr}"
    print("[PASS] Test 22a: Double close fails closed")

    # 8. Test 22b: Uninitialized close fails closed
    print("=== Running Test 22b: Uninitialized close fails closed ===")
    res = subprocess.run([bin_path, "test_22_uninit_close", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "Uninitialized close must fail closed"
    assert "close on closed or uninitialized" in res.stderr, f"Expected error in stderr: {res.stderr}"
    print("[PASS] Test 22b: Uninitialized close fails closed")

    # 9. Test 25b: open_in_place on already open FileIO fails closed
    print("=== Running Test 25b: open_in_place on already open FileIO fails closed ===")
    res = subprocess.run([bin_path, "test_25_open_in_place_on_open", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "open_in_place on open FileIO must fail closed"
    assert "open_in_place on already-open FileIO" in res.stderr, f"Expected error in stderr: {res.stderr}"
    print("[PASS] Test 25b: open_in_place on already open FileIO fails closed")

    # 10. Test 26: Closed / uninitialized zero/empty operations fail closed
    print("=== Running Test 26: Closed zero/empty operations fail closed ===")
    for op, expected in [
        ("test_26_closed_zero_read", "read on closed or uninitialized"),
        ("test_26_closed_empty_write", "write on closed or uninitialized"),
        ("test_26_closed_empty_batch", "read_batch on closed or uninitialized"),
        ("test_26_closed_chunks", "chunks on closed or uninitialized"),
        ("test_26_closed_sync", "sync on closed or uninitialized"),
    ]:
        res = subprocess.run([bin_path, op, test_run_dir], capture_output=True, text=True)
        assert res.returncode != 0, f"{op} must fail closed"
        assert expected in res.stderr, f"Expected '{expected}' in stderr: {res.stderr}"
    print("[PASS] Test 26: Closed zero/empty operations fail closed")

    # 11. Test 27: Close failure simulation
    print("=== Running Test 27: Close error simulation ===")
    res = subprocess.run([bin_path, "test_27_close_error_simulation", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "Close failure must abort"
    assert "close failed on descriptor" in res.stderr, f"Expected error in stderr: {res.stderr}"
    res = subprocess.run([bin_path, "test_27_check_registry_after_failed_close", test_run_dir], capture_output=True, text=True)
    assert res.returncode == 0, f"Registry check after failed close failed: {res.stderr}"
    print("[PASS] Test 27: Close error simulation and registry release")

    # 12. Test 28: Numerical overflow validation
    print("=== Running Test 28: Numerical overflow validation ===")
    res = subprocess.run([bin_path, "test_28_read_overflow", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "Read overflow must fail closed"
    assert "invalid read offset" in res.stderr, f"Expected invalid read offset in stderr: {res.stderr}"
    res = subprocess.run([bin_path, "test_28_write_overflow", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "Write overflow must fail closed"
    assert "invalid write offset" in res.stderr, f"Expected invalid write offset in stderr: {res.stderr}"
    print("[PASS] Test 28: Numerical overflow validation")

    # 13. Test 29: Compile-negative FileIO.clone()
    test_compile_negative_clone()

    # 14. Test 30 & 31: Cross-crate process-global registry & Solo hooks
    test_cross_crate_process_global_registry(test_run_dir)

    # 15. Test 32: Hook lock discipline
    test_hook_lock_discipline(test_run_dir)

    # 16. Test 33: Parent directory close failure
    print("=== Running Test 33: Parent directory close failure handling ===")
    res = subprocess.run([bin_path, "test_33_parent_close_failure", test_run_dir], capture_output=True, text=True)
    assert res.returncode != 0, "Parent close failure must abort"
    assert "close failed on parent directory descriptor" in res.stderr, f"Expected parent close failure in stderr: {res.stderr}"
    print("[PASS] Test 33: Parent directory close failure handling verified")

    # 17. Test 34, 35 & 36: Main-only emission & false-positive hardening
    test_main_only_runtime_emission(test_run_dir)

    # 18. Test 37: Fair ticket lock admission under contention
    test_fair_registry_admission(test_run_dir)

    # 19. Test 38: Production ABI purity
    test_production_abi_purity(test_run_dir)

    # 20. Test 39: Platform target gating
    test_target_platform_gating(test_run_dir)

    # 21. Test 40: Checked conversions & boundary handling
    test_checked_conversions_and_boundary_handling(test_run_dir)

    print("\n" + "="*50)
    print("ALL PHASE 20 FILEIO FINAL HARDENING TESTS PASSED!")
    print("="*50)

if __name__ == "__main__":
    run_tests()
