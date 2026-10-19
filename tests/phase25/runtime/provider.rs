use moss_runtime::MossMap;

#[derive(Clone)]
pub struct UserKey {
    pub id: i64,
}

pub fn new_map() -> MossMap<UserKey, i64> {
    MossMap::new()
}
