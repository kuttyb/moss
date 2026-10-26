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
