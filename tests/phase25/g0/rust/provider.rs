#![crate_name = "phase25_provider"]

extern crate phase25_moss_map_runtime as map_runtime;

// Selected Phase 25 A1 strategy: provider-owned generic Rust record with
// mechanical public backend fields. Rust-level pub field visibility allows
// consumer crates to generate direct field-access free functions without
// accessor function overhead. Source-level visibility (private fields) is
// enforced strictly by the Moss compiler frontend/typechecker before lowering.
pub struct GenericRecord<T> {
    pub field_0: T,
}

pub fn record<T>(value: T) -> GenericRecord<T> {
    GenericRecord { field_0: value }
}

#[derive(Clone)]
pub struct UserKey {
    pub field_0: i64,
}

pub fn user_key(code: i64) -> UserKey {
    UserKey { field_0: code }
}

pub fn moss_isequal(left: &UserKey, right: &UserKey) -> bool {
    left.field_0 % 10 == right.field_0 % 10
}

pub fn moss_hash(value: &UserKey) -> i64 {
    value.field_0 % 10
}

pub fn user_key_code(value: &UserKey) -> i64 {
    value.field_0
}

pub type UserMap<V> = map_runtime::MossMap<UserKey, V>;

pub fn user_map<V>() -> UserMap<V> {
    UserMap::new()
}
