#pragma once

inline const char* char_string_ordering_runtime_rust() {
  return R"RUST(
// ─── Begin Moss Char, String & Ordering Runtime ──────────────────────────
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

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MossString {
    pub bytes: String,
    pub len_codepoints: usize,
    pub is_ascii: bool,
    pub sparse_offsets: Vec<u32>,
}

impl MossString {
    pub fn from_str(s: &str) -> Self {
        let is_ascii = s.is_ascii();
        if is_ascii {
            return Self {
                bytes: s.to_string(),
                len_codepoints: s.len(),
                is_ascii: true,
                sparse_offsets: Vec::new(),
            };
        }
        let mut count = 0usize;
        let mut sparse = Vec::new();
        for (byte_idx, _) in s.char_indices() {
            if count > 0 && count % 64 == 0 {
                sparse.push(byte_idx as u32);
            }
            count += 1;
        }
        Self {
            bytes: s.to_string(),
            len_codepoints: count,
            is_ascii: false,
            sparse_offsets: sparse,
        }
    }

    #[inline]
    pub fn len(&self) -> usize {
        self.len_codepoints
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len_codepoints == 0
    }

    pub fn char_at(&self, idx: i64) -> char {
        if idx < 0 || (idx as usize) >= self.len_codepoints {
            panic!("String char_at out of bounds: index {} with length {}", idx, self.len_codepoints);
        }
        let uidx = idx as usize;
        if self.is_ascii {
            return self.bytes.as_bytes()[uidx] as char;
        }
        let (start_byte, skip_chars) = if uidx < 64 {
            (0usize, uidx)
        } else {
            let chunk = (uidx / 64) - 1;
            (self.sparse_offsets[chunk] as usize, uidx % 64)
        };
        self.bytes[start_byte..].chars().nth(skip_chars).unwrap()
    }

    pub fn chars(&self) -> Vec<char> {
        self.bytes.chars().collect()
    }

    pub fn append(&mut self, part: &str) {
        if part.is_empty() { return; }
        if self.is_ascii && part.is_ascii() {
            self.bytes.push_str(part);
            self.len_codepoints += part.len();
            return;
        }
        let prev_len = self.len_codepoints;
        let prev_bytes = self.bytes.len();
        self.bytes.push_str(part);
        self.is_ascii = false;
        let mut cur_cp = prev_len;
        for (byte_offset, _) in part.char_indices() {
            if cur_cp > 0 && cur_cp % 64 == 0 {
                self.sparse_offsets.push((prev_bytes + byte_offset) as u32);
            }
            cur_cp += 1;
        }
        self.len_codepoints = cur_cp;
    }
}

#[inline(always)]
pub fn __moss_string_char_at(s: &str, idx: i64) -> char {
    if idx < 0 {
        panic!("String char_at out of bounds: negative index {}", idx);
    }
    let uidx = idx as usize;
    if s.is_ascii() {
        if uidx >= s.len() {
            panic!("String char_at out of bounds: index {} with length {}", idx, s.len());
        }
        return s.as_bytes()[uidx] as char;
    }
    match s.chars().nth(uidx) {
        Some(c) => c,
        None => panic!("String char_at out of bounds: index {}", idx),
    }
}

// ─── Total Ordering, Equality & Hashing ──────────────────────────────────
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

#[repr(transparent)]
#[derive(Copy, Clone, Debug)]
pub struct MossFloatKey(pub f64);

impl PartialEq for MossFloatKey {
    #[inline(always)]
    fn eq(&self, other: &Self) -> bool {
        moss_float_isequal(self.0, other.0)
    }
}

impl Eq for MossFloatKey {}

impl PartialOrd for MossFloatKey {
    #[inline(always)]
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for MossFloatKey {
    #[inline(always)]
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        if moss_float_isequal(self.0, other.0) {
            std::cmp::Ordering::Equal
        } else if moss_float_less(self.0, other.0) {
            std::cmp::Ordering::Less
        } else {
            std::cmp::Ordering::Greater
        }
    }
}

impl std::hash::Hash for MossFloatKey {
    #[inline(always)]
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        state.write_u64(moss_float_hash(self.0) as u64);
    }
}

pub trait MossLess<Rhs: ?Sized = Self> {
    fn moss_less(&self, other: &Rhs) -> bool;
}

pub trait MossIsequal<Rhs: ?Sized = Self> {
    fn moss_isequal(&self, other: &Rhs) -> bool;
}

pub trait MossHash {
    fn moss_hash(&self) -> i64;
}

impl MossLess for i64 {
    #[inline(always)] fn moss_less(&self, other: &i64) -> bool { *self < *other }
}
impl MossIsequal for i64 {
    #[inline(always)] fn moss_isequal(&self, other: &i64) -> bool { *self == *other }
}
impl MossHash for i64 {
    #[inline(always)] fn moss_hash(&self) -> i64 { *self }
}

impl MossLess for f64 {
    #[inline(always)] fn moss_less(&self, other: &f64) -> bool { moss_float_less(*self, *other) }
}
impl MossIsequal for f64 {
    #[inline(always)] fn moss_isequal(&self, other: &f64) -> bool { moss_float_isequal(*self, *other) }
}
impl MossHash for f64 {
    #[inline(always)] fn moss_hash(&self) -> i64 { moss_float_hash(*self) }
}

impl MossLess for bool {
    #[inline(always)] fn moss_less(&self, other: &bool) -> bool { !*self && *other }
}
impl MossIsequal for bool {
    #[inline(always)] fn moss_isequal(&self, other: &bool) -> bool { *self == *other }
}
impl MossHash for bool {
    #[inline(always)] fn moss_hash(&self) -> i64 { if *self { 1 } else { 0 } }
}

impl MossLess for char {
    #[inline(always)] fn moss_less(&self, other: &char) -> bool { (*self as u32) < (*other as u32) }
}
impl MossIsequal for char {
    #[inline(always)] fn moss_isequal(&self, other: &char) -> bool { *self == *other }
}
impl MossHash for char {
    #[inline(always)] fn moss_hash(&self) -> i64 { *self as u32 as i64 }
}

// Char == String / &str compatibility
impl MossIsequal<String> for char {
    #[inline(always)]
    fn moss_isequal(&self, other: &String) -> bool {
        other.len() == self.len_utf8() && other.starts_with(*self)
    }
}
impl MossIsequal<&str> for char {
    #[inline(always)]
    fn moss_isequal(&self, other: &&str) -> bool {
        other.len() == self.len_utf8() && other.starts_with(*self)
    }
}
impl MossIsequal<char> for String {
    #[inline(always)]
    fn moss_isequal(&self, other: &char) -> bool {
        self.len() == other.len_utf8() && self.starts_with(*other)
    }
}
impl MossIsequal<char> for &str {
    #[inline(always)]
    fn moss_isequal(&self, other: &char) -> bool {
        self.len() == other.len_utf8() && self.starts_with(*other)
    }
}

impl MossLess for String {
    #[inline(always)] fn moss_less(&self, other: &String) -> bool { *self < *other }
}
impl MossIsequal for String {
    #[inline(always)] fn moss_isequal(&self, other: &String) -> bool { *self == *other }
}
impl MossHash for String {
    #[inline(always)]
    fn moss_hash(&self) -> i64 {
        let mut h = 14695981039346656037_u64;
        for b in self.as_bytes() {
            h ^= *b as u64;
            h = h.wrapping_mul(1099511628211_u64);
        }
        h as i64
    }
}

impl MossLess for &str {
    #[inline(always)] fn moss_less(&self, other: &&str) -> bool { *self < *other }
}
impl MossIsequal for &str {
    #[inline(always)] fn moss_isequal(&self, other: &&str) -> bool { *self == *other }
}
impl MossHash for &str {
    #[inline(always)]
    fn moss_hash(&self) -> i64 {
        let mut h = 14695981039346656037_u64;
        for b in self.as_bytes() {
            h ^= *b as u64;
            h = h.wrapping_mul(1099511628211_u64);
        }
        h as i64
    }
}

#[inline(always)]
pub fn __moss_less<T: MossLess<U> + ?Sized, U: ?Sized>(a: &T, b: &U) -> bool {
    a.moss_less(b)
}

#[inline(always)]
pub fn __moss_isequal<T: MossIsequal<U> + ?Sized, U: ?Sized>(a: &T, b: &U) -> bool {
    a.moss_isequal(b)
}

#[inline(always)]
pub fn __moss_hash<T: MossHash + ?Sized>(a: &T) -> i64 {
    a.moss_hash()
}

pub struct __MossReverse<F>(pub F);

impl<F> __MossReverse<F> {
    #[inline(always)]
    pub fn new(f: F) -> Self {
        __MossReverse(f)
    }
}
// ─── End Moss Char, String & Ordering Runtime ────────────────────────────
)RUST";
}
