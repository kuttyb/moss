#![crate_name = "phase25_provider_accessor"]

// Evaluated alternative: the defining crate owns the nominal declaration with
// private fields; access across crates occurs via provider-generated accessors.
// This is preserved as an evaluated alternative for Phase 25.5 (D1/D2); the
// primary selected Phase 25 strategy is public mechanical backend fields.
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
