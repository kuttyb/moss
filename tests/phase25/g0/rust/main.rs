extern crate phase25_consumer_a as consumer_a;
extern crate phase25_consumer_b as consumer_b;
extern crate phase25_provider as provider;

fn main() {
    let mut number = provider::record(41_i64);
    consumer_a::increment(&mut number);
    consumer_b::double(&mut number);

    let mut text = provider::record(String::from("moss"));
    consumer_b::append(&mut text, "-g0");

    println!(
        "{} {} {}",
        provider::record_read(&number),
        provider::record_read(&text),
        consumer_a::map_key_probe()
    );
}
