#![crate_name = "phase25_consumer_b"]

extern crate phase25_provider as provider;

// A second independently compiled consumer accepts the exact same concrete
// provider instance as consumer A. Passing one value through both crates is
// the cross-crate nominal-identity proof.
pub fn double(record: &mut provider::GenericRecord<i64>) {
    let next = *provider::record_read(record) * 2;
    provider::record_replace(record, next);
}

// A different concrete instance still uses the one provider declaration.
pub fn append(record: &mut provider::GenericRecord<String>, suffix: &str) {
    let next = provider::record_read(record).clone() + suffix;
    provider::record_replace(record, next);
}

fn find(values: &provider::UserMap<i64>, key: &provider::UserKey) -> Option<usize> {
    let hash = provider::moss_hash(key);
    (0..values.len()).find(|index| {
        values.entry_hash(*index) == hash && provider::moss_isequal(values.entry_key(*index), key)
    })
}

pub fn lookup(values: &provider::UserMap<i64>, code: i64) -> Option<i64> {
    let key = provider::user_key(code);
    find(values, &key).map(|index| *values.entry_value(index))
}

pub fn key_sum(values: &provider::UserMap<i64>) -> i64 {
    values.keys().iter().map(provider::user_key_code).sum()
}

pub fn delete(values: &mut provider::UserMap<i64>, code: i64) -> Option<i64> {
    let key = provider::user_key(code);
    let index = find(values, &key)?;
    Some(values.remove_entry(index).1)
}

pub fn len(values: &provider::UserMap<i64>) -> usize {
    values.len()
}
