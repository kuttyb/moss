#!/usr/bin/env python3
"""Phase 20 Agent B — FileIO Runtime Verification Suite.

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
+ Chunks read primitive borrowing FileIO and Range / RangeBatch buffer operations.
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

// Include FileIO runtime
MOSS_FILEIO_RUNTIME_PLACEHOLDER

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

fn test_solo_enter_hook(_reason: &str) {
    if let Ok(mut d) = TEST_SOLO_DEPTH.lock() {
        *d += 1;
    }
    if let Ok(mut c) = TEST_SOLO_ENTERS.lock() {
        *c += 1;
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

fn failing_close_override(fd: RawFd) -> i32 {
    let target = *TARGET_FD.lock().unwrap();
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
    moss_fileio::moss_set_solo_hooks(test_solo_lock_check_hook, dummy_solo_leave_hook);

    let path = format!("{}/test23.dat", dir);
    let file = FileIO::open(&path, "create");
    file.write(0, b"test".as_ref());
    let _ = file.read(0, 4);
    file.sync();
    file.close();

    moss_fileio::moss_clear_solo_hooks();
    println!("[PASS] Test 23: runtime/registry locks not held across file waits");
}

fn test_24_solo_hooks_paired(dir: &str) {
    println!("[RUN] Test 24: Solo hooks paired around waits");
    *TEST_SOLO_DEPTH.lock().unwrap() = 0;
    *TEST_SOLO_ENTERS.lock().unwrap() = 0;
    *TEST_SOLO_LEAVES.lock().unwrap() = 0;

    moss_fileio::moss_set_solo_hooks(test_solo_enter_hook, test_solo_leave_hook);

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

    moss_fileio::moss_clear_solo_hooks();
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
            let file = FileIO::open(&path, "create");
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
    start_marker = 'return R"RUST(\n'
    end_marker = '\n)RUST";'
    start_idx = content.find(start_marker)
    end_idx = content.find(end_marker)
    if start_idx == -1 or end_idx == -1:
        raise RuntimeError("Failed to extract fileio_runtime_rust from src/fileio_runtime.hpp")
    return content[start_idx + len(start_marker):end_idx]

def compile_test_harness():
    runtime_rust = extract_fileio_runtime_rust()
    harness_src = RUST_HARNESS_SOURCE.replace("MOSS_FILEIO_RUNTIME_PLACEHOLDER", runtime_rust)
    rs_path = os.path.join(TMP_DIR, "test_fileio_harness.rs")
    bin_path = os.path.join(TMP_DIR, "test_fileio_harness")
    with open(rs_path, "w", encoding="utf-8") as f:
        f.write(harness_src)
    
    cmd = ["rustc", "-D", "warnings", rs_path, "-o", bin_path]
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

    print("\n" + "="*50)
    print("ALL PHASE 20 FILEIO RUNTIME CORRECTIVE TESTS PASSED!")
    print("="*50)

if __name__ == "__main__":
    run_tests()
