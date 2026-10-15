#![crate_name = "phase25_provider"]

extern crate phase25_moss_map_runtime as map_runtime;

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

#[derive(Clone)]
pub struct UserKey {
    code: i64,
}

pub fn user_key(code: i64) -> UserKey {
    UserKey { code }
}

pub fn moss_isequal(left: &UserKey, right: &UserKey) -> bool {
    left.code % 10 == right.code % 10
}

pub fn moss_hash(value: &UserKey) -> i64 {
    value.code % 10
}

pub fn user_key_code(value: &UserKey) -> i64 {
    value.code
}

pub type UserMap<V> = map_runtime::MossMap<UserKey, V>;

pub fn user_map<V>() -> UserMap<V> {
    UserMap::new()
}
