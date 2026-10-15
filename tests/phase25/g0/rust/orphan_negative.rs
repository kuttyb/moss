extern crate phase25_provider as provider;

use std::hash::{Hash, Hasher};

// Must fail with E0117: both Hash and UserKey are foreign to this crate.
impl Hash for provider::UserKey {
    fn hash<H: Hasher>(&self, state: &mut H) {
        state.write_i64(provider::moss_hash(self));
    }
}

fn main() {}
