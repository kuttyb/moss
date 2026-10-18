fn main() {
    let mut map = provider::new_map();
    consumer_a::insert(&mut map, 41, 82);
    assert_eq!(consumer_b::read(&map, 41), Some(82));
    assert!(consumer_b::contains(&map, 41));
    assert!(!consumer_b::contains(&map, 99));

    let keys = consumer_a::snapshot(&map);
    let values = consumer_a::snapshot_values(&map);
    assert_eq!(keys.len(), 1);
    assert_eq!(keys[0].id, 41);
    assert_eq!(values.len(), 1);
    assert_eq!(values[0].field_0, 82);

    let removed = consumer_b::remove(&mut map, 41);
    assert_eq!(removed, Some(provider::GenericRecord { field_0: 82 }));
    assert_eq!(consumer_b::read(&map, 41), None);

    assert!(consumer_b::set_roundtrip(7));
    println!("shared moss_runtime Map and Set passed");
}
