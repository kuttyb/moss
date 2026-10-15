#![crate_name = "phase25_moss_map_runtime"]

use std::marker::PhantomData;

// Compiler/runtime-owned static key contract. Implementations use a local
// provider marker, so Rust coherence never requires a foreign implementation
// for the provider-owned key itself.
pub trait MossKeyOps<K> {
    fn isequal(left: &K, right: &K) -> bool;
    fn hash(value: &K) -> i64;
}

// One canonical native representation shared by the provider and every
// consumer. The prototype stores the checked Moss hash beside each key and
// uses statically dispatched Moss equality to resolve collisions.
pub struct MossMap<K, V, Ops> {
    entries: Vec<(i64, K, V)>,
    marker: PhantomData<Ops>,
}

impl<K, V, Ops: MossKeyOps<K>> MossMap<K, V, Ops> {
    pub fn new() -> Self {
        Self {
            entries: Vec::new(),
            marker: PhantomData,
        }
    }

    pub fn insert(&mut self, key: K, value: V) -> Option<V> {
        let hash = Ops::hash(&key);
        for (stored_hash, stored_key, stored_value) in &mut self.entries {
            if *stored_hash == hash && Ops::isequal(stored_key, &key) {
                return Some(std::mem::replace(stored_value, value));
            }
        }
        self.entries.push((hash, key, value));
        None
    }

    pub fn get(&self, key: &K) -> Option<&V> {
        let hash = Ops::hash(key);
        self.entries.iter().find_map(|(stored_hash, stored_key, value)| {
            (*stored_hash == hash && Ops::isequal(stored_key, key)).then_some(value)
        })
    }

    pub fn delete(&mut self, key: &K) -> Option<V> {
        let hash = Ops::hash(key);
        let index = self.entries.iter().position(|(stored_hash, stored_key, _)| {
            *stored_hash == hash && Ops::isequal(stored_key, key)
        })?;
        Some(self.entries.remove(index).2)
    }

    pub fn keys(&self) -> Vec<K>
    where
        K: Clone,
    {
        self.entries.iter().map(|(_, key, _)| key.clone()).collect()
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }
}
