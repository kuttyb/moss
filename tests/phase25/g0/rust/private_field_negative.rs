extern crate phase25_provider as provider;

fn main() {
    let value = provider::record(1_i64);
    // Must fail with E0616: consumers manipulate records only through the
    // provider's generated native accessors.
    println!("{}", value.value);
}
