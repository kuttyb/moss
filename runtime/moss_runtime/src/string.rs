//! String runtime module placeholder for `moss_runtime`.
//!
//! Team S owns the `MossString` (indexed-v1) implementation and its helper
//! functions (`__moss_string_length`, `__moss_string_char_at`, `__moss_string_slice`,
//! and `__moss_string_find`), which are relocated into this module during joint
//! ABI-v10 cutover under Team I coordination (TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md §1.3).
//!
//! Team B provides this module definition and linkage boundary to receive them
//! without independently rewriting Team S's String semantics.

/// Canonical ABI-v10 String representation name.
pub const STRING_REPRESENTATION: &str = "moss-string-indexed-v1";
pub const STRING_STRIDE: usize = 64;
pub const STRING_OFFSET_WIDTH: usize = 32;

/// Indexed string representation placeholder matching Team S's layout.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MossString {
    pub bytes: String,
    pub codepoints: usize,
    pub offsets: Vec<u32>,
}

impl MossString {
    pub fn from_str(s: &str) -> Self {
        let codepoints = s.chars().count();
        let mut offsets = Vec::new();
        if codepoints != s.len() {
            let mut cp = 0;
            for (byte_idx, _) in s.char_indices() {
                if cp > 0 && cp % STRING_STRIDE == 0 {
                    offsets.push(byte_idx as u32);
                }
                cp += 1;
            }
        }
        Self {
            bytes: s.to_string(),
            codepoints,
            offsets,
        }
    }

    pub fn len(&self) -> usize {
        self.codepoints
    }

    pub fn is_empty(&self) -> bool {
        self.codepoints == 0
    }

    pub fn as_str(&self) -> &str {
        &self.bytes
    }
}
