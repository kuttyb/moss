package org.mosslang.compiler.backend;

public class MossRuntimeSupport {
    public static final String FLOAT_RUNTIME = """
// ─── Begin Moss Float Format Runtime ─────────────────────────────────────
#[allow(dead_code)]
fn __moss_format_float(value: f64) -> String {
    if value.is_nan() { return "NaN".to_string(); }
    if value == f64::INFINITY { return "inf".to_string(); }
    if value == f64::NEG_INFINITY { return "-inf".to_string(); }
    if value == 0.0 {
        return if value.is_sign_negative() { "-0.0" } else { "0.0" }.to_string();
    }
    let sci = format!("{:e}", value.abs());
    let (mant_str, exp_str) = sci.split_once('e').unwrap_or((&sci, "0"));
    let digits: String = mant_str.chars().filter(|&c| c != '.').collect();
    let exponent: i32 = exp_str.parse().unwrap_or(0);
    let mut output = String::new();
    if value.is_sign_negative() { output.push('-'); }
    if (-4..16).contains(&exponent) {
        let decimal_at = exponent + 1;
        if decimal_at <= 0 {
            output.push_str("0.");
            for _ in 0..-decimal_at { output.push('0'); }
            output.push_str(&digits);
        } else if decimal_at as usize >= digits.len() {
            output.push_str(&digits);
            for _ in digits.len()..decimal_at as usize { output.push('0'); }
            output.push_str(".0");
        } else {
            let at = decimal_at as usize;
            output.push_str(&digits[..at]);
            output.push('.');
            output.push_str(&digits[at..]);
        }
    } else {
        output.push_str(&digits[..1]);
        if digits.len() > 1 {
            output.push('.');
            output.push_str(&digits[1..]);
        }
        output.push('e');
        output.push_str(&exponent.to_string());
    }
    output
}
#[inline(always)]
fn moss_float_less(a: f64, b: f64) -> bool {
    if a.is_nan() { return false; }
    if b.is_nan() { return true; }
    if a == 0.0 && b == 0.0 {
        return (a.to_bits() >> 63) > (b.to_bits() >> 63);
    }
    a < b
}
#[inline(always)]
fn moss_float_total_cmp(a: f64, b: f64) -> std::cmp::Ordering {
    if moss_float_less(a, b) { std::cmp::Ordering::Less }
    else if moss_float_less(b, a) { std::cmp::Ordering::Greater }
    else { std::cmp::Ordering::Equal }
}
#[inline(always)]
fn moss_float_isequal(a: f64, b: f64) -> bool {
    if a.is_nan() && b.is_nan() { return true; }
    if a == 0.0 && b == 0.0 { return a.to_bits() == b.to_bits(); }
    a == b
}
#[inline(always)]
fn moss_float_hash(value: f64) -> i64 {
    if value.is_nan() { 0x7ff8000000000000u64 as i64 }
    else { value.to_bits() as i64 }
}
// ─── End Moss Float Format Runtime ───────────────────────────────────────
""";

    public static final String EXECUTOR_RUNTIME = """
// Phase 20 Executor Runtime  (moss executor_runtime_rust: process root)
#[allow(dead_code, non_camel_case_types)]
#[derive(Clone, Debug, Default)]
pub struct __MossRange { pub len: i64 }
impl __MossRange {
    pub fn length(&self) -> i64 { self.len }
}
#[allow(dead_code, non_camel_case_types)]
#[derive(Clone, Debug, Default)]
pub struct __MossFile { pub total_len: i64 }
impl __MossFile {
    pub fn open(path: MossStr) -> Result<__MossFile, __MossRaised> {
        match std::fs::read(&*path) {
            Ok(bytes) => Ok(__MossFile { total_len: bytes.len() as i64 }),
            Err(_) => Err(__moss_raise("FileError::NotFound".to_string())),
        }
    }
    pub fn chunks(&self, size: i64) -> std::vec::Vec<__MossRange> {
        let mut out = std::vec::Vec::new();
        let mut rem = self.total_len;
        while rem > 0 {
            let take = if rem >= size { size } else { rem };
            out.push(__MossRange { len: take });
            rem -= take;
        }
        out
    }
    pub fn read(&self, offset: i64, size: i64) -> __MossRange {
        let avail = (self.total_len - offset).max(0);
        __MossRange { len: avail.min(size) }
    }
    pub fn close(&self) {}
}
#[allow(dead_code, non_camel_case_types)]
#[derive(Clone, Debug, Default)]
pub struct __MossExecutor;
impl __MossExecutor {
    pub fn threads(self, _n: i64) -> Self { self }
    pub fn start(self) -> Self { self }
    pub fn invoke<T>(&self, _val: T) {}
    pub fn join(&self) {}
}
// ─── End Phase 20 Executor Runtime ───────────────────────────────────────
""";

    public static final String HANDLER_RUNTIME = """
pub trait __MossCloneCapture: std::fmt::Debug + Send + Sync {
    fn clone_capture(&self) -> Box<dyn __MossCloneCapture>;
    fn as_any(&self) -> &dyn std::any::Any;
}
impl<T> __MossCloneCapture for T
where T: Clone + std::fmt::Debug + Send + Sync + 'static {
    fn clone_capture(&self) -> Box<dyn __MossCloneCapture> { Box::new(self.clone()) }
    fn as_any(&self) -> &dyn std::any::Any { self }
}
impl Clone for Box<dyn __MossCloneCapture> {
    fn clone(&self) -> Self { self.clone_capture() }
}

#[derive(Clone, Debug)]
pub struct __MossRaised {
    pub message: String,
    pub captures: std::collections::BTreeMap<String, Box<dyn __MossCloneCapture>>,
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
    pub fn get_capture<T: Clone + 'static>(&self, name: &str) -> T {
        self.captures.get(name)
            .and_then(|value| value.as_any().downcast_ref::<T>())
            .cloned()
            .unwrap_or_else(|| std::process::abort())
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
trait __MossDisplay {
    fn __moss_display(&self) -> String;
}
impl __MossDisplay for i64 {
    fn __moss_display(&self) -> String { self.to_string() }
}
impl __MossDisplay for f64 {
    fn __moss_display(&self) -> String { __moss_format_float(*self) }
}
impl __MossDisplay for bool {
    fn __moss_display(&self) -> String { if *value_bool(*self) { "true".to_string() } else { "false".to_string() } }
}
#[inline]
fn value_bool(b: bool) -> &'static bool { if b { &true } else { &false } }
impl __MossDisplay for char {
    fn __moss_display(&self) -> String { self.to_string() }
}
impl __MossDisplay for MossStr {
    fn __moss_display(&self) -> String { self.to_string() }
}
impl __MossDisplay for String {
    fn __moss_display(&self) -> String { self.clone() }
}
impl __MossDisplay for &str {
    fn __moss_display(&self) -> String { (*self).to_string() }
}
impl<T: __MossDisplay> __MossDisplay for std::vec::Vec<T> {
    fn __moss_display(&self) -> String {
        let mut out = String::from("[");
        for (i, item) in self.iter().enumerate() {
            if i > 0 { out.push_str(", "); }
            out.push_str(&item.__moss_display());
        }
        out.push(']');
        out
    }
}
#[allow(dead_code)]
fn __moss_display_int(out: &mut String, value: &i64) {
    out.push_str(&value.to_string());
}
#[allow(dead_code)]
fn __moss_display_float(out: &mut String, value: &f64) {
    out.push_str(&__moss_format_float(*value));
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
        self.0.chars().count() as i64
    }
    pub fn char_at(&self, idx: i64) -> char {
        if idx < 0 { panic!("String character index out of bounds"); }
        self.0.chars().nth(idx as usize).expect("String character index out of bounds")
    }
    pub fn chars(&self) -> std::vec::Vec<char> {
        self.0.chars().collect()
    }
    pub fn find(&self, pattern: MossStr) -> i64 {
        match self.0.find(&pattern.0) {
            Some(byte) => self.0[..byte].chars().count() as i64,
            None => -1,
        }
    }
    pub fn slice(&self, start: i64, length: i64) -> MossStr {
        if start < 0 || length < 0 { return MossStr::from(""); }
        let chars: std::vec::Vec<char> = self.0.chars().collect();
        let start = start as usize;
        let length = length as usize;
        if start > chars.len() || length > chars.len() - start { return MossStr::from(""); }
        MossStr(chars[start..start + length].iter().collect())
    }
    pub fn ascii_lower(&self) -> MossStr {
        MossStr(self.0.to_ascii_lowercase())
    }
    pub fn ascii_upper(&self) -> MossStr {
        MossStr(self.0.to_ascii_uppercase())
    }
    pub fn reserve(&mut self, additional_bytes: i64) {
        if additional_bytes > 0 { self.0.reserve(additional_bytes as usize); }
    }
    pub fn append(&mut self, part: MossStr) {
        self.0.push_str(&part.0);
    }
    pub fn split(&self, sep: MossStr) -> std::vec::Vec<MossStr> {
        if sep.0.is_empty() { panic!("String split requires a nonempty separator"); }
        self.0.split(&sep.0).map(|s| MossStr(s.to_string())).collect()
    }
    pub fn join(&self, parts: std::vec::Vec<MossStr>) -> MossStr {
        MossStr(parts.iter().map(|part| part.0.as_str()).collect::<std::vec::Vec<_>>().join(&self.0))
    }
}

impl std::ops::Add for MossStr {
    type Output = MossStr;
    fn add(self, rhs: MossStr) -> MossStr {
        let mut s = String::with_capacity(self.0.len() + rhs.0.len());
        s.push_str(&self.0);
        s.push_str(&rhs.0);
        MossStr(s)
    }
}
#[allow(dead_code)]
fn moss_string_hash(value: &MossStr) -> i64 {
    let mut hash = 0xcbf29ce484222325u64;
    for byte in value.0.as_bytes() {
        hash ^= *byte as u64;
        hash = hash.wrapping_mul(0x100000001b3u64);
    }
    hash as i64
}

#[allow(dead_code)]
trait __MossCollectionExt<T> {
    fn __moss_push(&mut self, item: T);
    fn __moss_pop(&mut self) -> T;
}
impl<T> __MossCollectionExt<T> for std::vec::Vec<T> {
    fn __moss_push(&mut self, item: T) { self.push(item); }
    fn __moss_pop(&mut self) -> T { self.pop().unwrap() }
}
impl<T> __MossCollectionExt<T> for std::collections::VecDeque<T> {
    fn __moss_push(&mut self, item: T) { self.push_back(item); }
    fn __moss_pop(&mut self) -> T { self.pop_front().unwrap() }
}
#[allow(dead_code)]
trait __MossMapExt<K> {
    fn __moss_keys(&self) -> std::vec::Vec<K>;
}
impl<K: Clone, V> __MossMapExt<K> for std::collections::BTreeMap<K, V> {
    fn __moss_keys(&self) -> std::vec::Vec<K> { self.keys().cloned().collect() }
}
#[allow(dead_code)]
trait __MossMapValuesExt<V> {
    fn __moss_values(&self) -> std::vec::Vec<V>;
}
impl<K, V: Clone> __MossMapValuesExt<V> for std::collections::BTreeMap<K, V> {
    fn __moss_values(&self) -> std::vec::Vec<V> { self.values().cloned().collect() }
}
#[allow(dead_code)]
trait __MossLengthExt {
    fn length(&self) -> i64;
}
impl<T> __MossLengthExt for std::vec::Vec<T> {
    fn length(&self) -> i64 { self.len() as i64 }
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

#[allow(dead_code)]
pub trait MossAdd<Rhs = Self> {
    type Output;
    fn __moss_add(self, rhs: Rhs) -> Self::Output;
}
impl MossAdd<i64> for i64 {
    type Output = i64;
    #[inline(always)]
    fn __moss_add(self, rhs: i64) -> i64 { self.wrapping_add(rhs) }
}
impl MossAdd<i64> for f64 {
    type Output = f64;
    #[inline(always)]
    fn __moss_add(self, rhs: i64) -> f64 { self + (rhs as f64) }
}
impl MossAdd<f64> for f64 {
    type Output = f64;
    #[inline(always)]
    fn __moss_add(self, rhs: f64) -> f64 { self + rhs }
}
impl MossAdd<f64> for i64 {
    type Output = f64;
    #[inline(always)]
    fn __moss_add(self, rhs: f64) -> f64 { (self as f64) + rhs }
}
impl MossAdd<&MossStr> for MossStr {
    type Output = MossStr;
    #[inline(always)]
    fn __moss_add(mut self, rhs: &MossStr) -> MossStr { self.0.push_str(&rhs.0); self }
}
impl MossAdd<MossStr> for MossStr {
    type Output = MossStr;
    #[inline(always)]
    fn __moss_add(mut self, rhs: MossStr) -> MossStr { self.0.push_str(&rhs.0); self }
}
impl MossAdd<&str> for MossStr {
    type Output = MossStr;
    #[inline(always)]
    fn __moss_add(mut self, rhs: &str) -> MossStr { self.0.push_str(rhs); self }
}
impl<T: Clone> MossAdd<&std::vec::Vec<T>> for std::vec::Vec<T> {
    type Output = std::vec::Vec<T>;
    #[inline(always)]
    fn __moss_add(mut self, rhs: &std::vec::Vec<T>) -> std::vec::Vec<T> { self.extend(rhs.iter().cloned()); self }
}
impl<T: Clone> MossAdd<std::vec::Vec<T>> for std::vec::Vec<T> {
    type Output = std::vec::Vec<T>;
    #[inline(always)]
    fn __moss_add(mut self, rhs: std::vec::Vec<T>) -> std::vec::Vec<T> { self.extend(rhs); self }
}

#[allow(dead_code)]
pub trait MossSub<Rhs = Self> {
    type Output;
    fn __moss_sub(self, rhs: Rhs) -> Self::Output;
}
impl MossSub<i64> for i64 {
    type Output = i64;
    #[inline(always)]
    fn __moss_sub(self, rhs: i64) -> i64 { self.wrapping_sub(rhs) }
}
impl MossSub<i64> for f64 {
    type Output = f64;
    #[inline(always)]
    fn __moss_sub(self, rhs: i64) -> f64 { self - (rhs as f64) }
}
impl MossSub<f64> for f64 {
    type Output = f64;
    #[inline(always)]
    fn __moss_sub(self, rhs: f64) -> f64 { self - rhs }
}
impl MossSub<f64> for i64 {
    type Output = f64;
    #[inline(always)]
    fn __moss_sub(self, rhs: f64) -> f64 { (self as f64) - rhs }
}

#[allow(dead_code)]
pub trait MossMul<Rhs = Self> {
    type Output;
    fn __moss_mul(self, rhs: Rhs) -> Self::Output;
}
impl MossMul<i64> for i64 {
    type Output = i64;
    #[inline(always)]
    fn __moss_mul(self, rhs: i64) -> i64 { self.wrapping_mul(rhs) }
}
impl MossMul<i64> for f64 {
    type Output = f64;
    #[inline(always)]
    fn __moss_mul(self, rhs: i64) -> f64 { self * (rhs as f64) }
}
impl MossMul<f64> for f64 {
    type Output = f64;
    #[inline(always)]
    fn __moss_mul(self, rhs: f64) -> f64 { self * rhs }
}
impl MossMul<f64> for i64 {
    type Output = f64;
    #[inline(always)]
    fn __moss_mul(self, rhs: f64) -> f64 { (self as f64) * rhs }
}
""";
}
