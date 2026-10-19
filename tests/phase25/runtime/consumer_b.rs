use moss_runtime::{MossMap, MossSet};
use provider::{GenericRecord, UserKey};

pub fn read(map: &MossMap<UserKey, GenericRecord<i64>>, id: i64) -> Option<i64> {
    map.get_with(id, &UserKey { id }, |a, b| a.id == b.id).map(|rec| rec.field_0)
}

pub fn remove(map: &mut MossMap<UserKey, GenericRecord<i64>>, id: i64) -> Option<GenericRecord<i64>> {
    map.remove_with(id, &UserKey { id }, |a, b| a.id == b.id)
}

pub fn contains(map: &MossMap<UserKey, GenericRecord<i64>>, id: i64) -> bool {
    map.contains_key_with(id, &UserKey { id }, |a, b| a.id == b.id)
}

pub fn set_roundtrip(id: i64) -> bool {
    let mut set: MossSet<UserKey> = MossSet::new();
    let key = UserKey { id };
    set.insert_with(id, key.clone(), |a, b| a.id == b.id)
        && set.contains_with(id, &key, |a, b| a.id == b.id)
        && set.remove_with(id, &key, |a, b| a.id == b.id)
}
