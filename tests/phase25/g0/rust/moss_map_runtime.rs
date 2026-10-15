#![crate_name = "phase25_moss_map_runtime"]

// One canonical native representation shared by the provider and every
// consumer. It contains no key policy, callback, vtable, or policy type.
pub struct MossMap<K, V> {
    entries: Vec<(i64, K, V)>,
}

impl<K, V> MossMap<K, V> {
    pub fn new() -> Self {
        Self {
            entries: Vec::new(),
        }
    }

    pub fn push_entry(&mut self, hash: i64, key: K, value: V) {
        self.entries.push((hash, key, value));
    }

    pub fn entry_hash(&self, index: usize) -> i64 {
        self.entries[index].0
    }

    pub fn entry_key(&self, index: usize) -> &K {
        &self.entries[index].1
    }

    pub fn entry_value(&self, index: usize) -> &V {
        &self.entries[index].2
    }

    pub fn replace_value(&mut self, index: usize, value: V) -> V {
        std::mem::replace(&mut self.entries[index].2, value)
    }

    pub fn remove_entry(&mut self, index: usize) -> (K, V) {
        let (_, key, value) = self.entries.remove(index);
        (key, value)
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
