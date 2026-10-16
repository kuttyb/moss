use moss_runtime::MossMap;
use provider::UserKey;

// This is the shape of a consumer-owned operation specialization: the key
// proof's exact equality/hash target is called statically at this call site.
pub fn insert(map: &mut MossMap<UserKey, i64>, id: i64, value: i64) {
    map.insert_with(id, UserKey { id }, value, |a, b| a.id == b.id);
}

pub fn snapshot(map: &MossMap<UserKey, i64>) -> Vec<UserKey> {
    map.keys()
}
