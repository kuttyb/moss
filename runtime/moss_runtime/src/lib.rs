//! Compiler-owned runtime types shared by every generated Moss crate.
//!
//! The crate is built once for a provider/consumer graph. A Map or Set value
//! contains storage only; the compiler specializes its key operations at each
//! call site. In particular, no Rust `Eq` or `Hash` implementation is required
//! for a Moss key type.
//!
//! At the joint ABI-v10 cutover under Team I coordination, Team S's String
//! and scalar key-protocol implementations are relocated into `string` and
//! `key_protocol` respectively.

pub mod map;
pub mod string;
pub mod key_protocol;

pub use map::{MossMap, MossSet};
pub use string::MossString;

/// ABI name and source version. The compiler also records the exact runtime
/// artifact content hash in the ABI-v10 binary contract.
pub const RUNTIME_NAME: &str = "moss_runtime";
pub const RUNTIME_VERSION: &str = "0.1.0";
