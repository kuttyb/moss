// Appended to generated production Rust and compiled only with moss_perf.
// The hook observes and pauses generated guards; it does not replace them.
#[cfg(test)]
mod phase20_domain_physical {
    use super::*;
    use std::sync::{Condvar, Mutex, OnceLock};

    struct Observed {
        verify_acquired: usize,
        target_contended: usize,
        target_acquired: usize,
        open_exclusive: usize,
        release_first: bool,
        target: &'static str,
    }
    static EVENTS: OnceLock<(Mutex<Observed>, Condvar)> = OnceLock::new();

    fn observer(event: &str, _instance: &str, handler: &str,
                _domain: usize, class: usize, exclusive: bool) {
        if class != 0 { return; }
        let (lock, cv) = EVENTS.get().unwrap();
        let mut state = lock.lock().unwrap();
        if handler.ends_with(":handler:Open") && event == "lock_acquired" {
            assert!(exclusive);
            state.open_exclusive += 1;
            cv.notify_all();
        }
        if handler.ends_with(":handler:Verify") && event == "lock_acquired" {
            assert!(!exclusive);
            state.verify_acquired += 1;
            cv.notify_all();
            if state.verify_acquired == 1 {
                while !state.release_first { state = cv.wait(state).unwrap(); }
            }
        }
        if state.target != ":handler:Verify" && handler.ends_with(state.target) {
            assert!(exclusive);
            if event == "lock_contended" { state.target_contended += 1; }
            if event == "lock_acquired" { state.target_acquired += 1; }
            cv.notify_all();
        }
    }

    fn wait_for(predicate: impl Fn(&Observed) -> bool) {
        let (lock, cv) = EVENTS.get().unwrap();
        let mut state = lock.lock().unwrap();
        while !predicate(&state) { state = cv.wait(state).unwrap(); }
    }

    fn scenario(target: &'static str) {
        EVENTS.set((Mutex::new(Observed {
            verify_acquired: 0, target_contended: 0, target_acquired: 0,
            open_exclusive: 0, release_first: false, target,
        }), Condvar::new())).ok().unwrap();
        MOSS_LOCK_HOOK.set(observer).ok().unwrap();
        CONSTRUCT_STORE
        store.Open_shared().expect("open");
        store.Update_shared().expect("update");
        wait_for(|s| s.open_exclusive == 1);
        {
            let (lock, _) = EVENTS.get().unwrap();
            let mut state = lock.lock().unwrap();
            state.target_contended = 0;
            state.target_acquired = 0;
        }
        let executor = MossExecutor::new().threads(2).max_threads(2).start();
        let first = store.clone();
        executor.enqueue_root(MossRootDescriptor::with_target(next_root_id(), "Verify", move || {
            assert_eq!(first.Verify_shared().expect("first verify"), Some(100));
        }));
        wait_for(|s| s.verify_acquired == 1);
        if target == ":handler:Verify" {
            let second = store.clone();
            executor.enqueue_root(MossRootDescriptor::with_target(next_root_id(), "Verify", move || {
                assert_eq!(second.Verify_shared().expect("second verify"), Some(100));
            }));
            wait_for(|s| s.verify_acquired == 2);
        } else {
            let writer = store.clone();
            executor.enqueue_root(MossRootDescriptor::with_target(next_root_id(), target, move || {
                match target {
                    ":handler:Update" => writer.Update_shared(),
                    ":handler:Flush" => writer.Flush_shared(),
                    ":handler:Close" => writer.Close_shared(),
                    _ => unreachable!(),
                }.expect("writer operation");
            }));
            wait_for(|s| s.target_contended == 1);
            let (lock, _) = EVENTS.get().unwrap();
            assert_eq!(lock.lock().unwrap().target_acquired, 0);
        }
        let (lock, cv) = EVENTS.get().unwrap();
        lock.lock().unwrap().release_first = true;
        cv.notify_all();
        if target != ":handler:Verify" { wait_for(|s| s.target_acquired == 1); }
        executor.join();
        if target != ":handler:Close" { store.Close_shared().expect("close"); }
    }

    #[test] fn shared() { scenario(":handler:Verify"); }
    #[test] fn update() { scenario(":handler:Update"); }
    #[test] fn flush() { scenario(":handler:Flush"); }
    #[test] fn close() { scenario(":handler:Close"); }
}
