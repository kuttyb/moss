//! Policy-free, deterministic robin-hood storage for Map and Set.
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
    slots: Vec<Option<Entry<K, V>>>,
    len: usize,
}

impl<K, V> Default for MossMap<K, V> {
    fn default() -> Self {
        Self::new()
    }
}

impl<K, V> MossMap<K, V> {
    pub fn new() -> Self {
        Self::with_slot_count(INITIAL_BUCKETS)
    }

    fn with_slot_count(count: usize) -> Self {
        debug_assert!(count.is_power_of_two());
        let slots = (0..count).map(|_| None).collect();
        Self { slots, len: 0 }
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Hash mixing is deterministic and independent of key policy. It reduces
    /// clustering when a valid Moss hash uses only high or low bits.
    fn home_slot(hash: i64, count: usize) -> usize {
        let mut bits = hash as u64;
        bits ^= bits >> 30;
        bits = bits.wrapping_mul(0xbf58_476d_1ce4_e5b9);
        bits ^= bits >> 27;
        bits = bits.wrapping_mul(0x94d0_49bb_1331_11eb);
        bits ^= bits >> 31;
        (bits as usize) & (count - 1)
    }

    fn probe_distance(slot: usize, home: usize, count: usize) -> usize {
        (slot + count - home) & (count - 1)
    }

    fn insert_rehashed(&mut self, mut entry: Entry<K, V>) {
        let count = self.slots.len();
        let mut slot = Self::home_slot(entry.hash, count);
        let mut distance = 0;
        loop {
            match &mut self.slots[slot] {
                None => {
                    self.slots[slot] = Some(entry);
                    self.len += 1;
                    return;
                }
                Some(occupant) => {
                    let occupant_home = Self::home_slot(occupant.hash, count);
                    let occupant_distance = Self::probe_distance(slot, occupant_home, count);
                    if occupant_distance < distance {
                        std::mem::swap(occupant, &mut entry);
                        distance = occupant_distance;
                    }
                }
            }
            slot = (slot + 1) & (count - 1);
            distance += 1;
        }
    }

    fn resize(&mut self, next_count: usize) {
        let old = std::mem::replace(&mut self.slots, (0..next_count).map(|_| None).collect());
        self.len = 0;
        for entry in old.into_iter().flatten() {
            self.insert_rehashed(entry);
        }
    }

    fn grow(&mut self) {
        self.resize(self.slots.len() * 2);
    }

    fn find_slot<F>(&self, hash: i64, key: &K, mut isequal: F) -> Option<usize>
    where
        F: FnMut(&K, &K) -> bool,
    {
        let count = self.slots.len();
        let mut slot = Self::home_slot(hash, count);
        let mut distance = 0;
        loop {
            let entry = self.slots[slot].as_ref()?;
            let occupant_home = Self::home_slot(entry.hash, count);
            if Self::probe_distance(slot, occupant_home, count) < distance {
                return None;
            }
            if entry.hash == hash && isequal(&entry.key, key) {
                return Some(slot);
            }
            slot = (slot + 1) & (count - 1);
            distance += 1;
        }
    }

    /// Lookup through the exact statically selected equality target. The
    /// caller supplies the already computed Moss hash of `key`.
    pub fn get_with<F>(&self, hash: i64, key: &K, mut isequal: F) -> Option<&V>
    where
        F: FnMut(&K, &K) -> bool,
    {
        self.find_slot(hash, key, &mut isequal)
            .map(|slot| &self.slots[slot].as_ref().expect("occupied map slot").value)
    }

    /// Replace an equal key's value, retaining the original stored key
    /// snapshot. Returns the replaced value when present.
    pub fn insert_with<F>(&mut self, hash: i64, key: K, value: V, mut isequal: F) -> Option<V>
    where
        F: FnMut(&K, &K) -> bool,
    {
        if let Some(slot) = self.find_slot(hash, &key, &mut isequal) {
            return Some(std::mem::replace(
                &mut self.slots[slot].as_mut().expect("occupied map slot").value,
                value,
            ));
        }
        // Keep load at or below 3/4. Rehash uses stored hashes and never
        // invokes a key operation or changes an existing key snapshot.
        if (self.len + 1) * 4 > self.slots.len() * 3 {
            self.grow();
        }
        self.insert_rehashed(Entry { hash, key, value });
        None
    }

    pub fn remove_with<F>(&mut self, hash: i64, key: &K, mut isequal: F) -> Option<V>
    where
        F: FnMut(&K, &K) -> bool,
    {
        let slot = self.find_slot(hash, key, &mut isequal)?;
        let removed = self.slots[slot].take().expect("occupied map slot");
        let count = self.slots.len();
        let mut hole = slot;
        loop {
            let next = (hole + 1) & (count - 1);
            let shift = self.slots[next].as_ref().is_some_and(|entry| {
                Self::probe_distance(next, Self::home_slot(entry.hash, count), count) > 0
            });
            if !shift {
                break;
            }
            self.slots[hole] = self.slots[next].take();
            hole = next;
        }
        self.len -= 1;
        // Keep the bucket scan in keys()/values() proportional to the number
        // of live entries, even after a map once held many more keys. Shrink
        // below 1/8 load; the resulting load is at most 1/4, so repeated
        // deletes do not cause a rehash per operation.
        if self.slots.len() > INITIAL_BUCKETS && self.len < self.slots.len() / 8 {
            self.resize((self.slots.len() / 2).max(INITIAL_BUCKETS));
        }
        Some(removed.value)
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
        self.find_slot(hash, key, &mut isequal)
            .map(|slot| &mut self.slots[slot].as_mut().expect("occupied map slot").value)
    }

    /// Clear all entries and release excess buckets so an empty snapshot is
    /// constant-time even after the map reached a large peak size.
    pub fn clear(&mut self) {
        *self = Self::new();
    }

    /// An eager, owned snapshot. Clone means the result cannot borrow a key
    /// from storage, including when the map changes after this call.
    pub fn keys(&self) -> Vec<K>
    where
        K: Clone,
    {
        let mut keys = Vec::with_capacity(self.len);
        for entry in self.slots.iter().flatten() {
            keys.push(entry.key.clone());
        }
        keys
    }

    /// An eager, owned snapshot of values.
    pub fn values(&self) -> Vec<V>
    where
        V: Clone,
    {
        let mut values = Vec::with_capacity(self.len);
        for entry in self.slots.iter().flatten() {
            values.push(entry.value.clone());
        }
        values
    }
}

/// Set uses the identical robin-hood key-operation kernel as Map.
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
        Self {
            storage: MossMap::new(),
        }
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
    fn sparse_map_snapshot_scans_and_clones_only_linear_live_storage() {
        use std::cell::Cell;
        use std::rc::Rc;

        struct CountedKey {
            id: i64,
            clones: Rc<Cell<usize>>,
        }

        impl Clone for CountedKey {
            fn clone(&self) -> Self {
                self.clones.set(self.clones.get() + 1);
                Self {
                    id: self.id,
                    clones: Rc::clone(&self.clones),
                }
            }
        }

        let clones = Rc::new(Cell::new(0));
        let mut map = MossMap::new();
        for id in 0..8192 {
            map.insert_with(
                id,
                CountedKey {
                    id,
                    clones: Rc::clone(&clones),
                },
                id,
                |a, b| a.id == b.id,
            );
        }
        for id in 0..8191 {
            let key = CountedKey {
                id,
                clones: Rc::clone(&clones),
            };
            assert_eq!(map.remove_with(id, &key, |a, b| a.id == b.id), Some(id));
        }
        assert_eq!(map.len(), 1);
        assert!(
            map.slots.len() <= 8 * map.len(),
            "keys() must scan O(live entries), not O(peak entries)"
        );
        let snapshot = map.keys();
        assert_eq!(snapshot.len(), 1);
        assert_eq!(snapshot[0].id, 8191);
        assert_eq!(clones.get(), 1, "snapshot must clone each live key once");

        let last = CountedKey {
            id: 8191,
            clones: Rc::clone(&clones),
        };
        assert_eq!(
            map.remove_with(8191, &last, |a, b| a.id == b.id),
            Some(8191)
        );
        assert_eq!(map.slots.len(), super::INITIAL_BUCKETS);
        assert!(map.keys().is_empty());
        assert_eq!(snapshot[0].id, 8191, "snapshot must own its key");

        for id in 0..8192 {
            map.insert_with(
                id,
                CountedKey {
                    id,
                    clones: Rc::clone(&clones),
                },
                id,
                |a, b| a.id == b.id,
            );
        }
        map.clear();
        assert_eq!(map.slots.len(), super::INITIAL_BUCKETS);
        assert!(map.keys().is_empty());
        assert_eq!(clones.get(), 1);
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
        assert_eq!(
            map.get_with(10, &1, |a, b| a == b),
            Some(&"one_updated".to_string())
        );

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

    #[test]
    fn collision_heavy_updates_match_an_independent_ordered_map() {
        #[derive(Clone)]
        struct Key(i32);
        let mut map = MossMap::new();
        let mut oracle = std::collections::BTreeMap::new();
        for step in 0..1200 {
            let number = ((step * 73) % 97) as i32;
            let key = Key(number);
            let hash = (number % 4) as i64;
            if step % 5 == 0 {
                assert_eq!(
                    map.remove_with(hash, &key, |a, b| a.0 == b.0),
                    oracle.remove(&number)
                );
            } else {
                assert_eq!(
                    map.insert_with(hash, key, step, |a, b| a.0 == b.0),
                    oracle.insert(number, step)
                );
            }
            assert_eq!(map.len(), oracle.len());
            for probe in 0..97 {
                let candidate = Key(probe);
                assert_eq!(
                    map.get_with((probe % 4) as i64, &candidate, |a, b| a.0 == b.0)
                        .copied(),
                    oracle.get(&probe).copied()
                );
            }
        }
    }
}
