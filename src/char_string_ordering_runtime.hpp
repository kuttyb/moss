#pragma once

#include <string>

// Moss String representation selected for one build.
//   plain       — MossStr = Rust String (native ABI v9 representation).
//   indexed-v1  — MossStr = MossString: UTF-8 bytes plus an eager code-point
//                 count and a byte offset every 64 code points, so length()
//                 is O(1) and char_at() decodes at most 63 scalars. This is
//                 the Team S String record staged for the joint ABI-v10 cut;
//                 artifacts carry it in their .mossi header and backend
//                 fingerprint, and the two representations never link.
// Generated code calls only the helpers below, so call sites are identical
// for both representations.

inline const char* char_ordering_runtime_rust() {
  return R"RUST(
// ─── Begin Moss Char & Ordering Runtime ──────────────────────────────────
#[inline(always)]
pub fn __moss_char_from_codepoint(cp: i64) -> char {
    if cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) {
        panic!("Char codepoint out of Unicode scalar range: {}", cp);
    }
    match std::char::from_u32(cp as u32) {
        Some(c) => c,
        None => panic!("Char codepoint invalid: {}", cp),
    }
}

// Julia-like total order: -inf < ... < -0.0 < +0.0 < ... < +inf < NaN.
#[inline(always)]
pub fn moss_float_less(a: f64, b: f64) -> bool {
    if a.is_nan() { return false; }
    if b.is_nan() { return true; }
    if a == 0.0 && b == 0.0 {
        return (a.to_bits() >> 63) > (b.to_bits() >> 63);
    }
    a < b
}

#[inline(always)]
pub fn moss_float_isequal(a: f64, b: f64) -> bool {
    if a.is_nan() && b.is_nan() { return true; }
    if a == 0.0 && b == 0.0 {
        return a.to_bits() == b.to_bits();
    }
    a == b
}

#[inline(always)]
pub fn moss_float_hash(a: f64) -> i64 {
    if a.is_nan() {
        0x7ff8000000000000_u64 as i64
    } else if a == 0.0 {
        (a.to_bits() & 0x8000000000000000_u64) as i64
    } else {
        a.to_bits() as i64
    }
}

#[inline(always)]
pub fn moss_float_total_cmp(a: f64, b: f64) -> std::cmp::Ordering {
    if moss_float_less(a, b) {
        std::cmp::Ordering::Less
    } else if moss_float_less(b, a) {
        std::cmp::Ordering::Greater
    } else {
        std::cmp::Ordering::Equal
    }
}

// Moss String hash: FNV-1a over UTF-8 bytes.
#[inline]
pub fn moss_string_hash(s: &str) -> i64 {
    let mut h = 14695981039346656037_u64;
    for b in s.as_bytes() {
        h ^= *b as u64;
        h = h.wrapping_mul(1099511628211_u64);
    }
    h as i64
}

// Operator `==` between a Char and a one-scalar String.
#[inline(always)]
pub fn moss_char_string_eq(c: char, s: &str) -> bool {
    s.len() == c.len_utf8() && s.starts_with(c)
}

#[cold]
fn __moss_char_at_out_of_bounds(idx: i64, length: usize) -> ! {
    panic!("String char_at out of bounds: index {} with length {}", idx, length)
}
// ─── End Moss Char & Ordering Runtime ────────────────────────────────────
)RUST";
}

inline const char* plain_string_runtime_rust() {
  return R"RUST(
// ─── Begin Moss String Runtime (plain) ───────────────────────────────────
pub type MossStr = String;

#[inline]
pub fn __moss_string_length(s: &str) -> i64 {
    s.chars().count() as i64
}

#[inline]
pub fn __moss_string_char_at(s: &str, idx: i64) -> char {
    if idx < 0 {
        __moss_char_at_out_of_bounds(idx, s.chars().count());
    }
    let uidx = idx as usize;
    if s.is_ascii() {
        if uidx >= s.len() {
            __moss_char_at_out_of_bounds(idx, s.len());
        }
        return s.as_bytes()[uidx] as char;
    }
    match s.chars().nth(uidx) {
        Some(c) => c,
        None => __moss_char_at_out_of_bounds(idx, s.chars().count()),
    }
}

pub fn __moss_string_slice(s: &str, start: i64, length: i64) -> MossStr {
    if start < 0 || length < 0 {
        return String::new();
    }
    let end = match start.checked_add(length) {
        Some(end) => end,
        None => return String::new(),
    };
    if s.is_ascii() {
        return if end <= s.len() as i64 { s[start as usize..end as usize].to_string() } else { String::new() };
    }
    let mut start_byte = None;
    let mut end_byte = None;
    let mut index = 0_i64;
    for (byte, _) in s.char_indices() {
        if index == start { start_byte = Some(byte); }
        if index == end { end_byte = Some(byte); break; }
        index += 1;
    }
    if index == start && start_byte.is_none() { start_byte = Some(s.len()); }
    if index == end && end_byte.is_none() { end_byte = Some(s.len()); }
    match (start_byte, end_byte) {
        (Some(from), Some(to)) => s[from..to].to_string(),
        _ => String::new(),
    }
}

pub fn __moss_string_find(s: &str, pattern: &str) -> i64 {
    match s.find(pattern) {
        Some(byte) => s[..byte].chars().count() as i64,
        None => -1_i64,
    }
}
// ─── End Moss String Runtime (plain) ─────────────────────────────────────
)RUST";
}

inline const char* indexed_string_runtime_rust() {
  return R"RUST(
// ─── Begin Moss String Runtime (indexed-v1) ──────────────────────────────
pub type MossStr = MossString;

// UTF-8 text plus code-point metadata. Invariants:
//   ASCII  <=> codepoints == bytes.len(), and then offsets is empty;
//   otherwise offsets[k] is the byte offset of code point 64 * (k + 1) for
//   every such code point below `codepoints`.
#[derive(Clone, Default)]
pub struct MossString {
    bytes: String,
    codepoints: usize,
    offsets: Vec<u32>,
}

const MOSS_STRING_STRIDE: usize = 64;

impl MossString {
    #[inline]
    pub fn new() -> Self {
        Self::default()
    }

    #[inline]
    pub fn with_capacity(capacity: usize) -> Self {
        Self { bytes: String::with_capacity(capacity), codepoints: 0, offsets: Vec::new() }
    }

    #[inline]
    pub fn as_str(&self) -> &str {
        &self.bytes
    }

    #[inline]
    pub fn codepoint_len(&self) -> usize {
        self.codepoints
    }

    #[inline]
    fn is_ascii_text(&self) -> bool {
        self.codepoints == self.bytes.len()
    }

    #[inline]
    pub fn reserve(&mut self, additional: usize) {
        self.bytes.reserve(additional);
    }

    #[inline]
    fn offset(byte: usize) -> u32 {
        <u32 as std::convert::TryFrom<usize>>::try_from(byte).unwrap_or_else(|_| panic!("String exceeds the 4 GiB indexed offset limit"))
    }

    // Appends `part`, scanning only `part`; amortized O(part.len()).
    pub fn push_str(&mut self, part: &str) {
        if part.is_empty() {
            return;
        }
        let base = self.bytes.len();
        let was_ascii = self.is_ascii_text();
        self.bytes.push_str(part);
        if was_ascii && part.is_ascii() {
            self.codepoints += part.len();
            return;
        }
        if was_ascii {
            // The ASCII prefix had no offsets; its code points are its bytes.
            let mut cp = MOSS_STRING_STRIDE;
            while cp < self.codepoints {
                self.offsets.push(Self::offset(cp));
                cp += MOSS_STRING_STRIDE;
            }
        }
        let mut cp = self.codepoints;
        for (byte, _) in part.char_indices() {
            if cp > 0 && cp % MOSS_STRING_STRIDE == 0 {
                self.offsets.push(Self::offset(base + byte));
            }
            cp += 1;
        }
        self.codepoints = cp;
    }

    // Byte offset of code point `index` (0 <= index <= codepoints).
    fn byte_of(&self, index: usize) -> usize {
        if self.is_ascii_text() || index == self.codepoints {
            return if index == self.codepoints { self.bytes.len() } else { index };
        }
        let chunk = index / MOSS_STRING_STRIDE;
        let start = if chunk == 0 { 0 } else { self.offsets[chunk - 1] as usize };
        let skip = index % MOSS_STRING_STRIDE;
        match self.bytes[start..].char_indices().nth(skip) {
            Some((byte, _)) => start + byte,
            None => self.bytes.len(),
        }
    }

    // Code-point index of a byte offset that starts a scalar.
    fn codepoint_of(&self, byte: usize) -> usize {
        if self.is_ascii_text() {
            return byte;
        }
        let chunk = self.offsets.partition_point(|offset| (*offset as usize) <= byte);
        let start = if chunk == 0 { 0 } else { self.offsets[chunk - 1] as usize };
        chunk * MOSS_STRING_STRIDE + self.bytes[start..byte].chars().count()
    }

    // Bytes of index metadata beyond the UTF-8 payload and String header.
    pub fn metadata_bytes(&self) -> usize {
        std::mem::size_of::<usize>() + std::mem::size_of::<Vec<u32>>()
            + self.offsets.capacity() * std::mem::size_of::<u32>()
    }
}

impl From<&str> for MossString {
    fn from(text: &str) -> Self {
        let mut result = MossString::with_capacity(text.len());
        result.push_str(text);
        result
    }
}

impl From<String> for MossString {
    fn from(text: String) -> Self {
        if text.is_ascii() {
            let codepoints = text.len();
            return MossString { bytes: text, codepoints, offsets: Vec::new() };
        }
        let mut offsets = Vec::new();
        let mut cp = 0usize;
        for (byte, _) in text.char_indices() {
            if cp > 0 && cp % MOSS_STRING_STRIDE == 0 {
                offsets.push(MossString::offset(byte));
            }
            cp += 1;
        }
        MossString { bytes: text, codepoints: cp, offsets }
    }
}

impl From<&String> for MossString {
    fn from(text: &String) -> Self {
        MossString::from(text.as_str())
    }
}

impl From<&MossString> for MossString {
    fn from(text: &MossString) -> Self {
        text.clone()
    }
}

impl From<MossString> for String {
    fn from(text: MossString) -> Self {
        text.bytes
    }
}

impl std::ops::Deref for MossString {
    type Target = str;
    #[inline]
    fn deref(&self) -> &str {
        &self.bytes
    }
}

impl AsRef<str> for MossString {
    fn as_ref(&self) -> &str {
        &self.bytes
    }
}

impl std::borrow::Borrow<str> for MossString {
    fn borrow(&self) -> &str {
        &self.bytes
    }
}

impl std::fmt::Display for MossString {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Display::fmt(self.bytes.as_str(), f)
    }
}

impl std::fmt::Debug for MossString {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self.bytes.as_str(), f)
    }
}

impl PartialEq for MossString {
    fn eq(&self, other: &Self) -> bool {
        self.bytes == other.bytes
    }
}
impl Eq for MossString {}
impl PartialEq<str> for MossString {
    fn eq(&self, other: &str) -> bool {
        self.bytes == other
    }
}
impl PartialEq<&str> for MossString {
    fn eq(&self, other: &&str) -> bool {
        self.bytes == *other
    }
}
impl PartialOrd for MossString {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}
impl Ord for MossString {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.bytes.as_str().cmp(other.bytes.as_str())
    }
}
impl std::hash::Hash for MossString {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.bytes.as_str().hash(state)
    }
}

#[inline]
pub fn __moss_string_length(s: &MossString) -> i64 {
    s.codepoints as i64
}

#[inline]
pub fn __moss_string_char_at(s: &MossString, idx: i64) -> char {
    if idx < 0 || (idx as u64) >= s.codepoints as u64 {
        __moss_char_at_out_of_bounds(idx, s.codepoints);
    }
    let index = idx as usize;
    if s.is_ascii_text() {
        return s.bytes.as_bytes()[index] as char;
    }
    let chunk = index / MOSS_STRING_STRIDE;
    let start = if chunk == 0 { 0 } else { s.offsets[chunk - 1] as usize };
    match s.bytes[start..].chars().nth(index % MOSS_STRING_STRIDE) {
        Some(c) => c,
        None => __moss_char_at_out_of_bounds(idx, s.codepoints),
    }
}

pub fn __moss_string_slice(s: &MossString, start: i64, length: i64) -> MossStr {
    if start < 0 || length < 0 {
        return MossString::new();
    }
    let end = match start.checked_add(length) {
        Some(end) => end,
        None => return MossString::new(),
    };
    if end as u64 > s.codepoints as u64 {
        return MossString::new();
    }
    let from = s.byte_of(start as usize);
    let to = s.byte_of(end as usize);
    MossString::from(&s.bytes[from..to])
}

pub fn __moss_string_find(s: &MossString, pattern: &str) -> i64 {
    match s.bytes.find(pattern) {
        Some(byte) => s.codepoint_of(byte) as i64,
        None => -1_i64,
    }
}
// ─── End Moss String Runtime (indexed-v1) ────────────────────────────────
)RUST";
}

inline std::string char_string_ordering_runtime_rust(bool indexed_strings) {
  return std::string(char_ordering_runtime_rust()) +
      (indexed_strings ? indexed_string_runtime_rust() : plain_string_runtime_rust());
}
