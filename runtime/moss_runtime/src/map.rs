//! Policy-free, deterministic bucketed storage for Map and Set.
//!
//! Equality is supplied to each operation as a monomorphized call-site hook.
//! It is never retained in a collection value. The caller has already checked
//! the canonical KeyProof and invokes its exact Moss `isequal` target.

const INITIAL_BUCKETS: usize = 8;

struct Entry<K, V> {
    hash: i64,
    key: K,
    value: V,
}

/// Stable representation of a Moss Map. Only `K` and `V` affect its Rust type.
pub struct MossMap<K, V> {
    buckets: Vec<Vec<Entry<K, V>>>,
    len: usize,
}

impl<K, V> Default for MossMap<K, V> {
    fn default() -> Self {
        Self::new()
    }
}

impl<K, V> MossMap<K, V> {
    pub fn new() -> Self {
        Self::with_bucket_count(INITIAL_BUCKETS)
    }

    fn with_bucket_count(count: usize) -> Self {
        let buckets = (0..count).map(|_| Vec::new()).collect();
        Self { buckets, len: 0 }
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Hash mixing is deterministic and independent of key policy. It reduces
    /// clustering when a valid Moss hash uses only high or low bits.
    fn bucket_index(hash: i64, count: usize) -> usize {
        let mut bits = hash as u64;
        bits ^= bits >> 30;
        bits = bits.wrapping_mul(0xbf58_476d_1ce4_e5b9);
        bits ^= bits >> 27;
        bits = bits.wrapping_mul(0x94d0_49bb_1331_11eb);
        bits ^= bits >> 31;
        (bits as usize) & (count - 1)
    }

    fn grow(&mut self) {
        let count = self.buckets.len() * 2;
        let mut next: Vec<Vec<Entry<K, V>>> = (0..count).map(|_| Vec::new()).collect();
        for bucket in self.buckets.drain(..) {
            for entry in bucket {
                let index = Self::bucket_index(entry.hash, count);
                next[index].push(entry);
            }
        }
        self.buckets = next;
    }

    /// Lookup through the exact statically selected equality target. The
    /// caller supplies the already computed Moss hash of `key`.
    pub fn get_with<F>(&self, hash: i64, key: &K, mut isequal: F) -> Option<&V>
    where
        F: FnMut(&K, &K) -> bool,
    {
        let index = Self::bucket_index(hash, self.buckets.len());
        self.buckets[index]
            .iter()
            .find(|entry| entry.hash == hash && isequal(&entry.key, key))
            .map(|entry| &entry.value)
    }

    /// Replace an equal key's value, retaining the original stored key
    /// snapshot. Returns the replaced value when present.
    pub fn insert_with<F>(&mut self, hash: i64, key: K, value: V, mut isequal: F) -> Option<V>
    where
        F: FnMut(&K, &K) -> bool,
    {
        let index = Self::bucket_index(hash, self.buckets.len());
        if let Some(entry) = self.buckets[index]
            .iter_mut()
            .find(|entry| entry.hash == hash && isequal(&entry.key, &key))
        {
            return Some(std::mem::replace(&mut entry.value, value));
        }
        // Keep load at or below 3/4. Rehash uses stored hashes and never
        // invokes a key operation or changes an existing key snapshot.
        if (self.len + 1) * 4 > self.buckets.len() * 3 {
            self.grow();
        }
        let index = Self::bucket_index(hash, self.buckets.len());
        self.buckets[index].push(Entry { hash, key, value });
        self.len += 1;
        None
    }

    pub fn remove_with<F>(&mut self, hash: i64, key: &K, mut isequal: F) -> Option<V>
    where
        F: FnMut(&K, &K) -> bool,
    {
        let index = Self::bucket_index(hash, self.buckets.len());
        let position = self.buckets[index]
            .iter()
            .position(|entry| entry.hash == hash && isequal(&entry.key, key))?;
        self.len -= 1;
        Some(self.buckets[index].swap_remove(position).value)
    }

    /// Check for key existence through the exact statically selected equality target.
    pub fn contains_key_with<F>(&self, hash: i64, key: &K, isequal: F) -> bool
    where
        F: FnMut(&K, &K) -> bool,
    {
        self.get_with(hash, key, isequal).is_some()
    }

    /// Mutable lookup through the exact statically selected equality target.
    pub fn get_mut_with<F>(&mut self, hash: i64, key: &K, mut isequal: F) -> Option<&mut V>
    where
        F: FnMut(&K, &K) -> bool,
    {
        let index = Self::bucket_index(hash, self.buckets.len());
        self.buckets[index]
            .iter_mut()
            .find(|entry| entry.hash == hash && isequal(&entry.key, key))
            .map(|entry| &mut entry.value)
    }

    /// Clear all entries from the map, resetting length while keeping bucket allocation.
    pub fn clear(&mut self) {
        for bucket in &mut self.buckets {
            bucket.clear();
        }
        self.len = 0;
    }

    /// An eager, owned snapshot. Clone means the result cannot borrow a key
    /// from storage, including when the map changes after this call.
    pub fn keys(&self) -> Vec<K>
    where
        K: Clone,
    {
        let mut keys = Vec::with_capacity(self.len);
        for bucket in &self.buckets {
            for entry in bucket {
                keys.push(entry.key.clone());
            }
        }
        keys
    }

    /// An eager, owned snapshot of values.
    pub fn values(&self) -> Vec<V>
    where
        V: Clone,
    {
        let mut values = Vec::with_capacity(self.len);
        for bucket in &self.buckets {
            for entry in bucket {
                values.push(entry.value.clone());
            }
        }
        values
    }
}

/// Set uses the identical bucket and key-operation kernel as Map.
pub struct MossSet<K> {
    storage: MossMap<K, ()>,
}

impl<K> Default for MossSet<K> {
    fn default() -> Self {
        Self::new()
    }
}

impl<K> MossSet<K> {
    pub fn new() -> Self {
        Self { storage: MossMap::new() }
    }

    pub fn len(&self) -> usize {
        self.storage.len()
    }

    pub fn is_empty(&self) -> bool {
        self.storage.is_empty()
    }

    pub fn contains_with<F>(&self, hash: i64, key: &K, isequal: F) -> bool
    where
        F: FnMut(&K, &K) -> bool,
    {
        self.storage.get_with(hash, key, isequal).is_some()
    }

    pub fn insert_with<F>(&mut self, hash: i64, key: K, isequal: F) -> bool
    where
        F: FnMut(&K, &K) -> bool,
    {
        self.storage.insert_with(hash, key, (), isequal).is_none()
    }

    pub fn remove_with<F>(&mut self, hash: i64, key: &K, isequal: F) -> bool
    where
        F: FnMut(&K, &K) -> bool,
    {
        self.storage.remove_with(hash, key, isequal).is_some()
    }

    pub fn clear(&mut self) {
        self.storage.clear();
    }

    pub fn keys(&self) -> Vec<K>
    where
        K: Clone,
    {
        self.storage.keys()
    }
}

#[cfg(test)]
mod tests {
    use super::{MossMap, MossSet};

    #[test]
    fn bucket_growth_replacement_and_deletion() {
        let mut map = MossMap::new();
        for n in 0..4096 {
            assert_eq!(map.insert_with(n, n, n * 2, |a, b| a == b), None);
        }
        assert_eq!(map.len(), 4096);
        for n in 0..4096 {
            assert_eq!(map.get_with(n, &n, |a, b| a == b), Some(&(n * 2)));
        }
        assert_eq!(map.insert_with(12, 12, 99, |a, b| a == b), Some(24));
        assert_eq!(map.remove_with(12, &12, |a, b| a == b), Some(99));
        assert_eq!(map.remove_with(12, &12, |a, b| a == b), None);
    }

    #[test]
    fn collisions_use_selected_equality() {
        let mut map = MossMap::new();
        map.insert_with(7, "a", 1, |a, b| a == b);
        map.insert_with(7, "b", 2, |a, b| a == b);
        assert_eq!(map.get_with(7, &"b", |a, b| a == b), Some(&2));
        assert_eq!(map.remove_with(7, &"a", |a, b| a == b), Some(1));
        assert_eq!(map.get_with(7, &"b", |a, b| a == b), Some(&2));
    }

    #[test]
    fn keys_are_detached_and_set_reuses_kernel() {
        let mut map = MossMap::new();
        map.insert_with(1, String::from("first"), 1, |a, b| a == b);
        let keys = map.keys();
        map.remove_with(1, &String::from("first"), |a, b| a == b);
        assert_eq!(keys, ["first"]);

        let mut set = MossSet::new();
        assert!(set.insert_with(4, 4, |a, b| a == b));
        assert!(!set.insert_with(4, 4, |a, b| a == b));
        assert!(set.contains_with(4, &4, |a, b| a == b));
        assert!(set.remove_with(4, &4, |a, b| a == b));
        assert!(set.is_empty());
    }

    #[test]
    fn test_values_get_mut_contains_and_clear() {
        let mut map = MossMap::new();
        map.insert_with(10, 1, "one".to_string(), |a, b| a == b);
        map.insert_with(20, 2, "two".to_string(), |a, b| a == b);
        assert!(map.contains_key_with(10, &1, |a, b| a == b));
        assert!(!map.contains_key_with(30, &3, |a, b| a == b));
        if let Some(v) = map.get_mut_with(10, &1, |a, b| a == b) {
            v.push_str("_updated");
        }
        assert_eq!(map.get_with(10, &1, |a, b| a == b), Some(&"one_updated".to_string()));

        let values = map.values();
        assert_eq!(values.len(), 2);
        assert!(values.contains(&"one_updated".to_string()));
        assert!(values.contains(&"two".to_string()));

        map.clear();
        assert_eq!(map.len(), 0);
        assert!(map.is_empty());
        assert_eq!(map.get_with(10, &1, |a, b| a == b), None);

        let mut set = MossSet::new();
        set.insert_with(1, 10, |a, b| a == b);
        set.clear();
        assert_eq!(set.len(), 0);
        assert!(set.is_empty());
    }
}
