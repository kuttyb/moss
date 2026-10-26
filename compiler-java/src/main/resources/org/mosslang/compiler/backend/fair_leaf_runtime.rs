// FIFO leaf lock. Each entrant atomically swaps its node into the tail and
// waits only on its predecessor. No entrant can bypass an enqueued waiter.
// park/unpark handles spurious wakeups and does not spin. The current holder
// never parks, joins, or takes another runtime lock while holding this lock.
struct MossFairNode {
    released: std::sync::atomic::AtomicBool,
    waiter: std::sync::atomic::AtomicPtr<std::thread::Thread>,
}

#[cfg(any(test, moss_perf))]
static MOSS_FAIR_ENQUEUE_HOOK: std::sync::OnceLock<fn(usize)> = std::sync::OnceLock::new();
#[cfg(any(test, moss_perf))]
pub fn moss_fair_set_enqueue_hook(hook: fn(usize)) {
    let _ = MOSS_FAIR_ENQUEUE_HOOK.set(hook);
}

pub struct MossFairMutex<T> {
    tail: std::sync::atomic::AtomicPtr<MossFairNode>,
    value: std::cell::UnsafeCell<T>,
    #[cfg(any(test, moss_perf))]
    parked: std::sync::atomic::AtomicUsize,
}

// SAFETY: the FIFO turn grants one mutable access at a time. T must be Send
// because a turn can be acquired on a different thread.
unsafe impl<T: Send> Send for MossFairMutex<T> {}
unsafe impl<T: Send> Sync for MossFairMutex<T> {}

pub struct MossFairGuard<'a, T> {
    lock: &'a MossFairMutex<T>,
    node: std::sync::Arc<MossFairNode>,
}

impl<T> MossFairMutex<T> {
    pub const fn new(value: T) -> Self {
        Self {
            tail: std::sync::atomic::AtomicPtr::new(std::ptr::null_mut()),
            value: std::cell::UnsafeCell::new(value),
            #[cfg(any(test, moss_perf))]
            parked: std::sync::atomic::AtomicUsize::new(0),
        }
    }

    pub fn lock(&self) -> MossFairGuard<'_, T> {
        use std::sync::atomic::Ordering;
        let node = std::sync::Arc::new(MossFairNode {
            released: std::sync::atomic::AtomicBool::new(false),
            waiter: std::sync::atomic::AtomicPtr::new(std::ptr::null_mut()),
        });
        let raw = std::sync::Arc::into_raw(std::sync::Arc::clone(&node)) as *mut MossFairNode;
        let previous = self.tail.swap(raw, Ordering::AcqRel);
        if !previous.is_null() {
            // SAFETY: swapping the tail transferred its owned Arc to us.
            let predecessor = unsafe { std::sync::Arc::from_raw(previous) };
            let waiter = std::sync::Arc::new(std::thread::current());
            let waiter_raw = std::sync::Arc::into_raw(waiter) as *mut std::thread::Thread;
            assert!(predecessor.waiter.compare_exchange(
                std::ptr::null_mut(), waiter_raw, Ordering::AcqRel, Ordering::Acquire).is_ok());
            #[cfg(any(test, moss_perf))]
            self.parked.fetch_add(1, Ordering::SeqCst);
            #[cfg(any(test, moss_perf))]
            if let Some(hook) = MOSS_FAIR_ENQUEUE_HOOK.get() { hook(self as *const Self as usize); }
            while !predecessor.released.load(Ordering::Acquire) {
                std::thread::park();
            }
            let remaining = predecessor.waiter.swap(std::ptr::null_mut(), Ordering::AcqRel);
            if !remaining.is_null() {
                // SAFETY: the successful swap transferred the waiter Arc.
                drop(unsafe { std::sync::Arc::from_raw(remaining) });
            }
            #[cfg(any(test, moss_perf))]
            self.parked.fetch_sub(1, Ordering::SeqCst);
        }
        MossFairGuard { lock: self, node }
    }

    #[cfg(any(test, moss_perf))]
    pub fn parked_waiters(&self) -> usize {
        self.parked.load(std::sync::atomic::Ordering::SeqCst)
    }
}

impl<T> std::ops::Deref for MossFairGuard<'_, T> {
    type Target = T;
    fn deref(&self) -> &T { unsafe { &*self.lock.value.get() } }
}
impl<T> std::ops::DerefMut for MossFairGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut T { unsafe { &mut *self.lock.value.get() } }
}
impl<T> Drop for MossFairGuard<'_, T> {
    fn drop(&mut self) {
        use std::sync::atomic::Ordering;
        self.node.released.store(true, Ordering::Release);
        let waiter = self.node.waiter.swap(std::ptr::null_mut(), Ordering::AcqRel);
        if !waiter.is_null() {
            // SAFETY: the successful swap transferred the waiter Arc.
            unsafe { std::sync::Arc::from_raw(waiter) }.unpark();
        }
        let raw = std::sync::Arc::as_ptr(&self.node) as *mut MossFairNode;
        if self.lock.tail.compare_exchange(raw, std::ptr::null_mut(),
                Ordering::AcqRel, Ordering::Acquire).is_ok() {
            // SAFETY: the successful CAS removed the tail-owned Arc.
            drop(unsafe { std::sync::Arc::from_raw(raw) });
        }
    }
}

struct MossFairWaiter {
    thread: std::thread::Thread,
    notified: std::sync::atomic::AtomicBool,
}

pub struct MossFairCondvar {
    owner: std::sync::atomic::AtomicPtr<()>,
    waiters: std::cell::UnsafeCell<std::collections::VecDeque<std::sync::Arc<MossFairWaiter>>>,
}
// SAFETY: every access to waiters requires a guard of the one bound fair lock.
unsafe impl Send for MossFairCondvar {}
unsafe impl Sync for MossFairCondvar {}

impl MossFairCondvar {
    pub const fn new() -> Self {
        Self { owner: std::sync::atomic::AtomicPtr::new(std::ptr::null_mut()),
               waiters: std::cell::UnsafeCell::new(std::collections::VecDeque::new()) }
    }
    fn check<T>(&self, guard: &MossFairGuard<'_, T>) {
        use std::sync::atomic::Ordering;
        let lock = guard.lock as *const MossFairMutex<T> as *mut ();
        let owner = self.owner.load(Ordering::Acquire);
        if owner.is_null() {
            let _ = self.owner.compare_exchange(std::ptr::null_mut(), lock,
                                                Ordering::AcqRel, Ordering::Acquire);
        }
        assert_eq!(self.owner.load(Ordering::Acquire), lock,
                   "fair condition used with a different state lock");
    }
    pub fn wait<'a, T>(&self, guard: MossFairGuard<'a, T>) -> MossFairGuard<'a, T> {
        self.check(&guard);
        let waiter = std::sync::Arc::new(MossFairWaiter {
            thread: std::thread::current(),
            notified: std::sync::atomic::AtomicBool::new(false),
        });
        // SAFETY: the associated fair guard exclusively protects this queue.
        unsafe { &mut *self.waiters.get() }.push_back(std::sync::Arc::clone(&waiter));
        let lock = guard.lock;
        drop(guard);
        while !waiter.notified.load(std::sync::atomic::Ordering::Acquire) {
            std::thread::park();
        }
        lock.lock()
    }
    pub fn notify_one<T>(&self, guard: &MossFairGuard<'_, T>) {
        self.check(guard);
        // SAFETY: the associated fair guard exclusively protects this queue.
        if let Some(waiter) = unsafe { &mut *self.waiters.get() }.pop_front() {
            waiter.notified.store(true, std::sync::atomic::Ordering::Release);
            waiter.thread.unpark();
        }
    }
    pub fn notify_all<T>(&self, guard: &MossFairGuard<'_, T>) {
        self.check(guard);
        // SAFETY: the associated fair guard exclusively protects this queue.
        for waiter in unsafe { &mut *self.waiters.get() }.drain(..) {
            waiter.notified.store(true, std::sync::atomic::Ordering::Release);
            waiter.thread.unpark();
        }
    }
}
