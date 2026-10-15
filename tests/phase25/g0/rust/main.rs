extern crate phase25_consumer_a as consumer_a;
extern crate phase25_consumer_b as consumer_b;
extern crate phase25_provider as provider;

fn main() {
    let mut number = provider::record(41_i64);
    consumer_a::increment(&mut number);
    consumer_b::double(&mut number);

    let mut text = provider::record(String::from("moss"));
    consumer_b::append(&mut text, "-g0");

    // One provider-created map value crosses both independently compiled
    // consumer boundaries. Both signatures name provider::UserMap<i64>, which
    // aliases the shared-runtime MossMap representation.
    let mut values = provider::user_map();
    consumer_a::insert_initial(&mut values);
    let before = consumer_b::lookup(&values, 17).unwrap();
    let overwritten = consumer_a::overwrite(&mut values);
    let after = consumer_b::lookup(&values, 37).unwrap();
    let keys = consumer_b::key_sum(&values);
    let deleted = consumer_b::delete(&mut values, 18).unwrap();
    let remaining = consumer_b::len(&values);
    let missing = consumer_b::lookup(&values, 28).is_none();

    println!(
        "{} {} map={},{},{},{},{},{},{}",
        number.field_0,
        text.field_0,
        before,
        overwritten,
        after,
        keys,
        deleted,
        remaining,
        missing
    );
}
