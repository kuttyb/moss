package org.mosslang.compiler.backend;

public class MossRuntimeSupport {
    public static final String FLOAT_RUNTIME = """
// ─── Begin Moss Float Format Runtime ─────────────────────────────────────
// Float formatting runtime
// ─── End Moss Float Format Runtime ───────────────────────────────────────
""";

    public static final String EXECUTOR_RUNTIME = """
// Phase 20 Executor Runtime  (moss executor_runtime_rust: process root)
// Executor runtime
// ─── End Phase 20 Executor Runtime ───────────────────────────────────────
""";

    public static final String HANDLER_RUNTIME = """
#[derive(Clone, Debug)]
pub struct __MossRaised {
    pub message: String,
    pub captures: std::collections::BTreeMap<String, String>,
}
impl __MossRaised {
    pub fn get_code(&self) -> i64 {
        for token in self.message.split(|c: char| !c.is_numeric() && c != '-') {
            if let Ok(v) = token.parse::<i64>() { return v; }
        }
        0
    }
    pub fn get_string(&self) -> String {
        self.message.clone()
    }
    pub fn get_capture(&self, name: &str) -> String {
        self.captures.get(name).cloned().unwrap_or_else(|| self.message.clone())
    }
}
#[inline]
pub fn __moss_raise(val: impl std::string::ToString) -> __MossRaised {
    __MossRaised {
        message: val.to_string(),
        captures: std::collections::BTreeMap::new(),
    }
}
impl From<String> for __MossRaised {
    fn from(s: String) -> Self { __moss_raise(s) }
}
impl From<&str> for __MossRaised {
    fn from(s: &str) -> Self { __moss_raise(s) }
}

struct MossAbortOnUnwind;
impl Drop for MossAbortOnUnwind {
    fn drop(&mut self) { if std::thread::panicking() { std::process::abort(); } }
}
#[cfg(any(test, moss_perf))]
static MOSS_LOCK_HOOK: std::sync::OnceLock<fn(&str, &str, &str, usize, usize, bool)> = std::sync::OnceLock::new();
#[inline]
fn moss_lock_event(_event: &str, _instance: &str, _handler: &str, _domain: usize, _class: usize, _exclusive: bool) {
    #[cfg(any(test, moss_perf))]
    if let Some(hook) = MOSS_LOCK_HOOK.get() { hook(_event, _instance, _handler, _domain, _class, _exclusive); }
}
#[inline]
fn moss_read_or_abort<T>(lock: &std::sync::RwLock<T>) -> std::sync::RwLockReadGuard<'_, T> {
    lock.read().unwrap_or_else(|_| std::process::abort())
}
#[inline]
fn moss_write_or_abort<T>(lock: &std::sync::RwLock<T>) -> std::sync::RwLockWriteGuard<'_, T> {
    lock.write().unwrap_or_else(|_| std::process::abort())
}
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
""";

    public static final String DISPLAY_RUNTIME = """
#[allow(dead_code)]
fn __moss_display_int(out: &mut String, value: &i64) {
    out.push_str(&value.to_string());
}
#[allow(dead_code)]
fn __moss_display_float(out: &mut String, value: &f64) {
    out.push_str(&value.to_string());
}
#[allow(dead_code)]
fn __moss_display_bool(out: &mut String, value: &bool) {
    out.push_str(if *value { "true" } else { "false" });
}
#[allow(dead_code)]
fn __moss_display_char(out: &mut String, value: &char) {
    out.push(*value);
}
#[allow(dead_code)]
fn __moss_display_string(out: &mut String, value: &MossStr) {
    use std::fmt::Write as _;
    let _ = write!(out, "{}", value);
}
""";

    public static final String STRING_RUNTIME = """
#[derive(Clone, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct MossStr(String);

impl MossStr {
    pub fn from(s: &str) -> Self {
        MossStr(s.to_string())
    }
    pub fn length(&self) -> i64 {
        self.0.len() as i64
    }
}

impl std::ops::Deref for MossStr {
    type Target = str;
    fn deref(&self) -> &str {
        &self.0
    }
}

impl std::fmt::Display for MossStr {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl std::fmt::Debug for MossStr {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.0)
    }
}
""";
}
