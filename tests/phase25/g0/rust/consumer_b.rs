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
