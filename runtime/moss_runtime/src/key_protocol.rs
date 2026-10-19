//! Key-protocol runtime module placeholder for `moss_runtime`.
//!
//! Team S owns the scalar key-protocol functions (`moss_float_less`,
//! `moss_float_isequal`, `moss_float_hash`, `moss_float_total_cmp`,
//! `moss_string_hash`, `__moss_char_from_codepoint`), which are relocated into
//! this module during joint ABI-v10 cutover under Team I coordination
//! (TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md §1.3, §2).
//!
//! Team B provides this module definition and linkage boundary to receive them
//! without independently rewriting Team S's scalar key semantics.

/// Canonical key protocol version identifier.
pub const KEY_PROTOCOL_VERSION: &str = "key-protocol-v1";
pub const KEY_LAWS_VERSION: &str = "key-laws-v1";

/// Canonical FNV-1a 64-bit hash over UTF-8 bytes for String keys.
/// Unseeded, deterministic, and identical across crates.
pub fn moss_string_hash(s: &str) -> i64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for &b in s.as_bytes() {
        h ^= b as u64;
        h = h.wrapping_mul(0x100000001b3);
    }
    h as i64
}

/// Float equality under key-protocol-v1:
/// - NaN == NaN is true
/// - -0.0 == +0.0 is false
#[inline(always)]
pub fn moss_float_isequal(a: f64, b: f64) -> bool {
    if a.is_nan() && b.is_nan() {
        return true;
    }
    if a == 0.0 && b == 0.0 {
        return a.to_bits() == b.to_bits();
    }
    a == b
}

/// Float hash under key-protocol-v1:
/// All NaNs produce the same canonical hash; -0.0 and +0.0 produce distinct hashes.
#[inline(always)]
pub fn moss_float_hash(a: f64) -> i64 {
    if a.is_nan() {
        return 0x7ff8000000000000_u64 as i64;
    }
    if a == 0.0 {
        return if a.is_sign_negative() {
            0x8000000000000000_u64 as i64
        } else {
            0
        };
    }
    a.to_bits() as i64
}

/// Float total order: -inf < ... < -0.0 < +0.0 < ... < +inf < NaN.
#[inline(always)]
pub fn moss_float_less(a: f64, b: f64) -> bool {
    if a.is_nan() {
        return false;
    }
    if b.is_nan() {
        return true;
    }
    if a == 0.0 && b == 0.0 {
        return (a.to_bits() >> 63) > (b.to_bits() >> 63);
    }
    a < b
}

/// Unicode scalar validator for Char constructor.
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
