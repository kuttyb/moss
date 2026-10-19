use moss_runtime::MossMap;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UserKey {
    pub id: i64,
}

// Provider-owned generic record definition (A1 lowering per TYPE_IR_CONTRACT §7).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GenericRecord<T> {
    pub field_0: T,
}

pub fn new_map() -> MossMap<UserKey, GenericRecord<i64>> {
    MossMap::new()
}

pub fn make_record<T>(value: T) -> GenericRecord<T> {
    GenericRecord { field_0: value }
}
