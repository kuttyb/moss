#![crate_name = "phase25_consumer_a"]

extern crate phase25_provider as provider;

// A consumer-owned free function specializes operations over the provider's
// nominal type. It does not redeclare the record.
pub fn increment(record: &mut provider::GenericRecord<i64>) {
    let next = *provider::record_read(record) + 1;
    provider::record_replace(record, next);
}

fn find(values: &provider::UserMap<i64>, key: &provider::UserKey) -> Option<usize> {
    let hash = provider::moss_hash(key);
    (0..values.len()).find(|index| {
        values.entry_hash(*index) == hash && provider::moss_isequal(values.entry_key(*index), key)
    })
}

fn insert(values: &mut provider::UserMap<i64>, key: provider::UserKey, value: i64) -> Option<i64> {
    if let Some(index) = find(values, &key) {
        return Some(values.replace_value(index, value));
    }
    let hash = provider::moss_hash(&key);
    values.push_entry(hash, key, value);
    None
}

pub fn insert_initial(values: &mut provider::UserMap<i64>) {
    assert_eq!(insert(values, provider::user_key(7), 10), None);
    assert_eq!(insert(values, provider::user_key(8), 80), None);
}

pub fn overwrite(values: &mut provider::UserMap<i64>) -> i64 {
    insert(values, provider::user_key(27), 11).unwrap()
}
