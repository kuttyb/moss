//! Compiler-owned runtime types shared by every generated Moss crate.
//!
//! The crate is built once for a provider/consumer graph. A Map or Set value
//! contains storage only; the compiler specializes its key operations at each
//! call site. In particular, no Rust `Eq` or `Hash` implementation is required
//! for a Moss key type.

pub mod map;

pub use map::{MossMap, MossSet};

/// ABI name and source version. The compiler also records the exact runtime
/// artifact content hash in the ABI-v10 binary contract.
pub const RUNTIME_NAME: &str = "moss_runtime";
pub const RUNTIME_VERSION: &str = "0.1.0";
