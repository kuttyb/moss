#pragma once

namespace moss {
// Safe Rust storage: each synchronization class owns its leaves. A handler
// moves exclusive leaves into a private working value and restores them before
// releasing guards. READ leaves are borrowed under retained shared guards.
// No reference to an entire shared domain State is ever manufactured.
inline const char* handler_runtime_rust() {
  return R"RUST(
type MossLeaves<V> = std::collections::BTreeMap<&'static str, V>;
type MossHandlerPlan = (&'static str, Vec<(usize, bool)>, Vec<&'static str>, Vec<&'static str>, &'static str);
// Private generated calling convention, not a .mossi synchronization ABI.
type MossPhysicalPlan = (&'static str, usize, Vec<Vec<&'static str>>, Vec<MossHandlerPlan>);
struct MossClassRuntime<V> {
    identity: &'static str,
    rank: usize,
    classes: Vec<std::sync::RwLock<MossLeaves<V>>>,
    immutable: MossLeaves<V>,
    leaf_classes: std::collections::BTreeMap<&'static str, usize>,
    handlers: Vec<MossHandlerPlan>,
}
enum MossClassGuard<'a, V> {
    Shared(std::sync::RwLockReadGuard<'a, MossLeaves<V>>),
    Exclusive(std::sync::RwLockWriteGuard<'a, MossLeaves<V>>),
}
impl<V> MossClassGuard<'_, V> {
    fn leaves(&self) -> &MossLeaves<V> {
        match self { Self::Shared(g) => g, Self::Exclusive(g) => g }
    }
    fn writable(&mut self) -> &mut MossLeaves<V> {
        match self { Self::Exclusive(g) => g, _ => std::process::abort() }
    }
}
struct MossAbortOnUnwind;
impl Drop for MossAbortOnUnwind {
    fn drop(&mut self) { if std::thread::panicking() { std::process::abort(); } }
}
#[cfg(any(test, moss_perf))]
static MOSS_LOCK_HOOK: std::sync::OnceLock<fn(&str, &str, &str, usize, usize, bool)> = std::sync::OnceLock::new();
fn moss_lock_event(event: &str, instance: &str, handler: &str, domain: usize, class: usize, exclusive: bool) {
    #[cfg(any(test, moss_perf))]
    if let Some(hook) = MOSS_LOCK_HOOK.get() { hook(event, instance, handler, domain, class, exclusive); }
}
impl<V> MossClassRuntime<V> {
    fn new(plan: MossPhysicalPlan, mut leaves: MossLeaves<V>) -> Self {
        let (identity, rank, classes, handlers) = plan;
        let mut leaf_classes = std::collections::BTreeMap::new();
        let classes = classes.into_iter().enumerate().map(|(index, members)| {
            let mut storage = MossLeaves::new();
            for leaf in members {
                if leaf_classes.insert(leaf, index).is_some() { std::process::abort(); }
                storage.insert(leaf, leaves.remove(leaf).unwrap_or_else(|| std::process::abort()));
            }
            std::sync::RwLock::new(storage)
        }).collect();
        Self { identity, rank, classes, immutable: leaves, leaf_classes, handlers }
    }
    fn enter(&self, name: &str) -> MossHandlerFrame<'_, V> {
        let handler = self.handlers.iter().find(|h| h.0 == name).unwrap_or_else(|| std::process::abort());
        moss_lock_event("handler_enter", self.identity, handler.4, self.rank, usize::MAX, false);
        let mut guards = std::collections::BTreeMap::new();
        let _abort = MossAbortOnUnwind;
        let mut previous = None;
        for &(rank, exclusive) in &handler.1 {
            if previous.is_some_and(|p| p >= rank) { std::process::abort(); }
            previous = Some(rank);
            moss_lock_event("lock_acquire", self.identity, handler.4, self.rank, rank, exclusive);
            let lock = self.classes.get(rank).unwrap_or_else(|| std::process::abort());
            // Instrumented builds distinguish actual WouldBlock from uncontended
            // acquisition latency. They retain the same order/mode/lifetime.
            #[cfg(moss_perf)]
            let guard = if exclusive {
                let value = match lock.try_write() {
                    Ok(g) => g,
                    Err(std::sync::TryLockError::WouldBlock) => {
                        moss_lock_event("lock_contended", self.identity, handler.4, self.rank, rank, true);
                        lock.write().unwrap_or_else(|_| std::process::abort())
                    },
                    Err(_) => std::process::abort(),
                };
                MossClassGuard::Exclusive(value)
            } else {
                let value = match lock.try_read() {
                    Ok(g) => g,
                    Err(std::sync::TryLockError::WouldBlock) => {
                        moss_lock_event("lock_contended", self.identity, handler.4, self.rank, rank, false);
                        lock.read().unwrap_or_else(|_| std::process::abort())
                    },
                    Err(_) => std::process::abort(),
                };
                MossClassGuard::Shared(value)
            };
            #[cfg(not(moss_perf))]
            let guard = if exclusive {
                MossClassGuard::Exclusive(lock.write().unwrap_or_else(|_| std::process::abort()))
            } else {
                MossClassGuard::Shared(lock.read().unwrap_or_else(|_| std::process::abort()))
            };
            guards.insert(rank, guard);
            moss_lock_event("lock_acquired", self.identity, handler.4, self.rank, rank, exclusive);
        }
        MossHandlerFrame { runtime: self, handler, guards, evacuated: std::collections::BTreeSet::new() }
    }
}
struct MossHandlerFrame<'a, V> {
    runtime: &'a MossClassRuntime<V>,
    handler: &'a MossHandlerPlan,
    guards: std::collections::BTreeMap<usize, MossClassGuard<'a, V>>,
    evacuated: std::collections::BTreeSet<&'static str>,
}
impl<V> MossHandlerFrame<'_, V> {
    fn take_exclusive(&mut self) -> MossLeaves<V> {
        let mut values = MossLeaves::new();
        for &leaf in &self.handler.3 {
            let rank = self.runtime.leaf_classes.get(leaf).unwrap_or_else(|| std::process::abort());
            let value = self.guards.get_mut(rank).unwrap_or_else(|| std::process::abort())
                .writable().remove(leaf).unwrap_or_else(|| std::process::abort());
            self.evacuated.insert(leaf);
            values.insert(leaf, value);
        }
        values
    }
    // The borrow is bounded by &self, hence by the retained guards. Neither a
    // protected nor an immutable READ creates an owned user value.
    fn read(&self, leaf: &'static str) -> Option<&V> {
        if !self.handler.2.contains(&leaf) { return None; }
        let storage = match self.runtime.leaf_classes.get(leaf) {
            Some(rank) => self.guards.get(rank).unwrap_or_else(|| std::process::abort()).leaves(),
            None => &self.runtime.immutable,
        };
        Some(storage.get(leaf).unwrap_or_else(|| std::process::abort()))
    }
    fn restore(&mut self, leaf: &'static str, value: V) {
        if !self.handler.3.contains(&leaf) { return; }
        if !self.evacuated.remove(leaf) { std::process::abort(); }
        let rank = self.runtime.leaf_classes.get(leaf).unwrap_or_else(|| std::process::abort());
        self.guards.get_mut(rank).unwrap_or_else(|| std::process::abort()).writable().insert(leaf, value);
    }
}
impl<V> Drop for MossHandlerFrame<'_, V> {
    fn drop(&mut self) {
        // Abort BEFORE any class guard is released: recovery of torn state is
        // deliberately undefined until supervision semantics are designed.
        if std::thread::panicking() || !self.evacuated.is_empty() { std::process::abort(); }
        moss_lock_event("handler_complete", self.runtime.identity, self.handler.4, self.runtime.rank, usize::MAX, false);
        for &(rank, exclusive) in self.handler.1.iter().rev() {
            moss_lock_event("lock_releasing", self.runtime.identity, self.handler.4, self.runtime.rank, rank, exclusive);
            drop(self.guards.remove(&rank));
            moss_lock_event("lock_release", self.runtime.identity, self.handler.4, self.runtime.rank, rank, exclusive);
        }
        moss_lock_event("handler_exit", self.runtime.identity, self.handler.4, self.runtime.rank, usize::MAX, false);
    }
}
// Access slots own only evacuated EXCLUSIVE values. Absent is metadata, never
// a fabricated/default source value. READ references cannot outlive the frame.
enum MossSlot<'a, T> { Read(&'a T), Exclusive(T), Absent }
impl<T> MossSlot<'_, T> {
    fn into_exclusive(self) -> Option<T> {
        match self { Self::Exclusive(value) => Some(value), _ => None }
    }
}
impl<T> std::ops::Deref for MossSlot<'_, T> {
    type Target = T;
    fn deref(&self) -> &T {
        match self { Self::Read(value) => value, Self::Exclusive(value) => value,
                     Self::Absent => std::process::abort() }
    }
}
impl<T> std::ops::DerefMut for MossSlot<'_, T> {
    fn deref_mut(&mut self) -> &mut T {
        match self { Self::Exclusive(value) => value, _ => std::process::abort() }
    }
}
impl<T: std::fmt::Debug> std::fmt::Debug for MossSlot<'_, T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(&**self, f)
    }
}
)RUST";
}
} // namespace moss
