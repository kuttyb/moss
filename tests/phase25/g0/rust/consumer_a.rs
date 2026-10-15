#![crate_name = "phase25_consumer_a"]

extern crate phase25_provider as provider;

use std::collections::HashMap;
use std::hash::{Hash, Hasher};

// A consumer-owned free function specializes operations over the provider's
// nominal type. It does not redeclare the record.
pub fn increment(record: &mut provider::GenericRecord<i64>) {
    let next = *provider::record_read(record) + 1;
    provider::record_replace(record, next);
}

// The artifact that instantiates Map owns this adapter type. Implementing the
// foreign Eq/Hash traits is legal because the self type is local; calls remain
// statically dispatched to the provider's Moss semantics.
pub struct UserKeyAdapter(pub provider::UserKey);

impl PartialEq for UserKeyAdapter {
    fn eq(&self, other: &Self) -> bool {
        provider::moss_isequal(&self.0, &other.0)
    }
}

impl Eq for UserKeyAdapter {}

impl Hash for UserKeyAdapter {
    fn hash<H: Hasher>(&self, state: &mut H) {
        state.write_i64(provider::moss_hash(&self.0));
    }
}

pub fn map_key_probe() -> i64 {
    let mut values = HashMap::new();
    values.insert(UserKeyAdapter(provider::user_key(7)), 41_i64);
    values[&UserKeyAdapter(provider::user_key(7))]
}
