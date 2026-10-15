#![crate_name = "phase25_consumer_a"]

extern crate phase25_provider as provider;

// A consumer-owned free function specializes operations over the provider's
// nominal type. It does not redeclare the record.
pub fn increment(record: &mut provider::GenericRecord<i64>) {
    let next = *provider::record_read(record) + 1;
    provider::record_replace(record, next);
}

pub fn insert_initial(values: &mut provider::UserMap<i64>) {
    assert_eq!(values.insert(provider::user_key(7), 10), None);
    assert_eq!(values.insert(provider::user_key(8), 80), None);
}

pub fn overwrite(values: &mut provider::UserMap<i64>) -> i64 {
    values.insert(provider::user_key(27), 11).unwrap()
}
