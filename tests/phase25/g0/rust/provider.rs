#![crate_name = "phase25_provider"]

// Prototype only: the defining crate owns the one Rust nominal declaration.
// Its field stays private; generated public accessors are native ABI surface,
// not Moss exports.
pub struct GenericRecord<T> {
    value: T,
}

pub fn record<T>(value: T) -> GenericRecord<T> {
    GenericRecord { value }
}

pub fn record_read<T>(record: &GenericRecord<T>) -> &T {
    &record.value
}

pub fn record_replace<T>(record: &mut GenericRecord<T>, value: T) -> T {
    std::mem::replace(&mut record.value, value)
}

pub struct UserKey {
    code: i64,
}

pub fn user_key(code: i64) -> UserKey {
    UserKey { code }
}

pub fn moss_isequal(left: &UserKey, right: &UserKey) -> bool {
    left.code == right.code
}

pub fn moss_hash(value: &UserKey) -> i64 {
    value.code.wrapping_mul(1_000_003)
}
