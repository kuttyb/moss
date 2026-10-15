extern crate phase25_provider_accessor as provider_accessor;

fn main() {
    let value = provider_accessor::record(1_i64);
    // Must fail with E0616: consumers manipulate records only through the
    // provider's generated native accessors under the accessor alternative.
    println!("{}", value.value);
}
