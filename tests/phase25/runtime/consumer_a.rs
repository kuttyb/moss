use moss_runtime::MossMap;
use provider::{GenericRecord, UserKey};

// This is the shape of a consumer-owned operation specialization: the key
// proof's exact equality/hash target is called statically at this call site.
pub fn insert(map: &mut MossMap<UserKey, GenericRecord<i64>>, id: i64, value: i64) {
    map.insert_with(id, UserKey { id }, provider::make_record(value), |a, b| a.id == b.id);
}

pub fn snapshot(map: &MossMap<UserKey, GenericRecord<i64>>) -> Vec<UserKey> {
    map.keys()
}

pub fn snapshot_values(map: &MossMap<UserKey, GenericRecord<i64>>) -> Vec<GenericRecord<i64>> {
    map.values()
}
