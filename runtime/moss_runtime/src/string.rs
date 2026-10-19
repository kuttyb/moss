//! Shared indexed-v1 Moss String representation and exact Team S operations.
//! Kept byte-for-byte aligned with the staged compiler prelude until the v10 cutover.

#[cold]
fn __moss_char_at_out_of_bounds(idx: i64, length: usize) -> ! {
    panic!(
        "String char_at out of bounds: index {} with length {}",
        idx, length
    )
}

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
        Self {
            bytes: String::with_capacity(capacity),
            codepoints: 0,
            offsets: Vec::new(),
        }
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
        <u32 as std::convert::TryFrom<usize>>::try_from(byte)
            .unwrap_or_else(|_| panic!("String exceeds the 4 GiB indexed offset limit"))
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
            return if index == self.codepoints {
                self.bytes.len()
            } else {
                index
            };
        }
        let chunk = index / MOSS_STRING_STRIDE;
        let start = if chunk == 0 {
            0
        } else {
            self.offsets[chunk - 1] as usize
        };
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
        let chunk = self
            .offsets
            .partition_point(|offset| (*offset as usize) <= byte);
        let start = if chunk == 0 {
            0
        } else {
            self.offsets[chunk - 1] as usize
        };
        chunk * MOSS_STRING_STRIDE + self.bytes[start..byte].chars().count()
    }

    // Bytes of index metadata beyond the UTF-8 payload and String header.
    pub fn metadata_bytes(&self) -> usize {
        std::mem::size_of::<usize>()
            + std::mem::size_of::<Vec<u32>>()
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
            return MossString {
                bytes: text,
                codepoints,
                offsets: Vec::new(),
            };
        }
        let mut offsets = Vec::new();
        let mut cp = 0usize;
        for (byte, _) in text.char_indices() {
            if cp > 0 && cp % MOSS_STRING_STRIDE == 0 {
                offsets.push(MossString::offset(byte));
            }
            cp += 1;
        }
        MossString {
            bytes: text,
            codepoints: cp,
            offsets,
        }
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
    let start = if chunk == 0 {
        0
    } else {
        s.offsets[chunk - 1] as usize
    };
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
