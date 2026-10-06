#![allow(dead_code)]
FAIR_LEAF_RUNTIME

use std::sync::{Arc, Mutex, OnceLock};
use std::sync::mpsc::{self, Sender};

static ENQUEUED: OnceLock<Mutex<Sender<usize>>> = OnceLock::new();

fn on_enqueue(_lock: usize) {
    let name = std::thread::current().name().unwrap().to_owned();
    let ticket = name.strip_prefix("fair-").unwrap().parse::<usize>().unwrap();
    ENQUEUED.get().unwrap().lock().unwrap().send(ticket).unwrap();
}

fn main() {
    let lock = Arc::new(MossFairMutex::new(0usize));
    let held = lock.lock();
    let (queued_tx, queued_rx) = mpsc::channel();
    ENQUEUED.set(Mutex::new(queued_tx)).ok().unwrap();
    moss_fair_set_enqueue_hook(on_enqueue);
    let (entered_tx, entered_rx) = mpsc::channel();
    let mut threads = Vec::new();
    for i in 0..8 {
        let lock = Arc::clone(&lock);
        let entered_tx = entered_tx.clone();
        threads.push(std::thread::Builder::new().name(format!("fair-{i}"))
            .spawn(move || {
                let mut guard = lock.lock();
                *guard += 1;
                entered_tx.send(i).unwrap();
            }).unwrap());
        // The test hook fires immediately after the atomic queue insertion.
        // This fixes admission order without a timer, polling, or sleeps.
        assert_eq!(queued_rx.recv().unwrap(), i);
    }
    assert_eq!(lock.parked_waiters(), 8);
    drop(held);
    for i in 0..8 { assert_eq!(entered_rx.recv().unwrap(), i); }
    for thread in threads { thread.join().unwrap(); }
    assert_eq!(*lock.lock(), 8);
    println!("fair leaf FIFO admission passed");
}
