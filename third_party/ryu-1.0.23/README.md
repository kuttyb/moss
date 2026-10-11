Ryū 1.0.23, commit f0b52bb194befe6fd242154f2182fafd43a819b8, from https://github.com/dtolnay/ryu.

Moss embeds Ryū's f64 shortest-decimal core in `src/float_format_runtime.hpp` under Apache-2.0. `crate::` paths are rewritten and `core::` imports mapped to `std::` for the generated Rust edition. Inactive Cargo feature attributes are removed; the full lookup table is fixed for Moss builds. Moss applies its own plain/scientific thresholds and special-value spellings to the returned decimal digits. The embedded source retains upstream copyright notices.
