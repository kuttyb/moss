#pragma once

namespace moss {
// Production contains typed locks and borrows only. The compiler consumes the
// SynchronizationPlan; no physical plan/leaf/guard maps are emitted.
inline const char* handler_runtime_rust() {
  return R"RUST(
struct MossAbortOnUnwind;
impl Drop for MossAbortOnUnwind {
    fn drop(&mut self) { if std::thread::panicking() { std::process::abort(); } }
}
#[cfg(any(test, moss_perf))]
static MOSS_LOCK_HOOK: std::sync::OnceLock<fn(&str, &str, &str, usize, usize, bool)> = std::sync::OnceLock::new();
#[inline]
fn moss_lock_event(event: &str, instance: &str, handler: &str, domain: usize, class: usize, exclusive: bool) {
    #[cfg(any(test, moss_perf))]
    if let Some(hook) = MOSS_LOCK_HOOK.get() { hook(event, instance, handler, domain, class, exclusive); }
}
#[inline]
fn moss_read_or_abort<T>(lock: &std::sync::RwLock<T>) -> std::sync::RwLockReadGuard<'_, T> {
    lock.read().unwrap_or_else(|_| std::process::abort())
}
#[inline]
fn moss_write_or_abort<T>(lock: &std::sync::RwLock<T>) -> std::sync::RwLockWriteGuard<'_, T> {
    lock.write().unwrap_or_else(|_| std::process::abort())
}
// Static object projections implement the private access trait used by owned
// objects too. Access capabilities are monomorphized, never selected at runtime.
// Checked effects make mutable access through a READ or absent projection
// unreachable; those trait methods fail closed if compiler invariants fail.
struct MossRead<'a, T>(&'a T);
struct MossWrite<'a, T>(&'a mut T);
struct MossAbsent<T>(std::marker::PhantomData<T>);
impl<T> std::ops::Deref for MossRead<'_, T> { type Target = T; fn deref(&self) -> &T { self.0 } }
impl<T> std::ops::DerefMut for MossRead<'_, T> { fn deref_mut(&mut self) -> &mut T { std::process::abort() } }
impl<T> std::ops::Deref for MossWrite<'_, T> { type Target = T; fn deref(&self) -> &T { self.0 } }
impl<T> std::ops::DerefMut for MossWrite<'_, T> { fn deref_mut(&mut self) -> &mut T { self.0 } }
impl<T> std::ops::Deref for MossAbsent<T> { type Target = T; fn deref(&self) -> &T { std::process::abort() } }
impl<T> std::ops::DerefMut for MossAbsent<T> { fn deref_mut(&mut self) -> &mut T { std::process::abort() } }
impl<T: std::fmt::Debug> std::fmt::Debug for MossRead<'_, T> { fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result { self.0.fmt(f) } }
impl<T: std::fmt::Debug> std::fmt::Debug for MossWrite<'_, T> { fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result { self.0.fmt(f) } }
impl<T> std::fmt::Debug for MossAbsent<T> { fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result { f.write_str("<unused>") } }
)RUST";
}
} // namespace moss
