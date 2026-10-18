#![crate_name = "phase25_consumer_accessor"]

extern crate phase25_provider_accessor as provider_accessor;

pub fn increment(record: &mut provider_accessor::GenericRecord<i64>) {
    let next = *provider_accessor::record_read(record) + 1;
    provider_accessor::record_replace(record, next);
}
