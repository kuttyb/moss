// F.1 microcases: the same std::sync::RwLock, logical operations, and lifetime.
use std::hint::black_box;
use std::sync::RwLock;
use std::time::Instant;
fn make_account() -> AccountRef { CONSTRUCTORS
    account
}
struct Manual { balance: RwLock<i64>, fills: RwLock<i64>, limit: RwLock<i64> }
impl Manual {
    fn new() -> Self { Self { balance: RwLock::new(0), fills: RwLock::new(0), limit: RwLock::new(10) } }
    #[inline] fn call(&self, n: usize) -> i64 {
        match n {
            0 => 7,
            1 => *moss_read_or_abort(&self.balance),
            2 => { let mut b = moss_write_or_abort(&self.balance); *b += 1; 0 },
            3 => { let mut b = moss_write_or_abort(&self.balance); let mut f = moss_write_or_abort(&self.fills); *b += 1; *f += 1; *b + *f },
            4 => { let mut b = moss_write_or_abort(&self.balance); let l = moss_read_or_abort(&self.limit); *b += 1; *b + *l },
            _ => { let mut b = moss_write_or_abort(&self.balance); let mut f = moss_write_or_abort(&self.fills); let l = moss_read_or_abort(&self.limit); *b += 1; *f += 1; *b + *f + *l },
        }
    }
}
#[inline] fn generated(b: &AccountRef, n: usize) -> i64 {
    match n { 0 => b.Empty_shared().unwrap(), 1 => b.Read_shared().unwrap(),
        2 => { b.Write_shared(); 0 }, 3 => b.Two_shared().unwrap(),
        4 => b.Mixed_shared().unwrap(), _ => b.Three_shared().unwrap() }
}
// Stable roots for optimized assembly inspection. They also expose the entire
// wrapper to LLVM, exactly as a direct Rust caller would.
#[no_mangle] fn probe_empty(b: &AccountRef) -> i64 { b.Empty_shared().unwrap() }
#[no_mangle] fn probe_read(b: &AccountRef) -> i64 { b.Read_shared().unwrap() }
#[no_mangle] fn probe_write(b: &AccountRef) { b.Write_shared() }
#[no_mangle] fn probe_two(b: &AccountRef) -> i64 { b.Two_shared().unwrap() }
#[no_mangle] fn probe_mixed(b: &AccountRef) -> i64 { b.Mixed_shared().unwrap() }
#[no_mangle] fn probe_three(b: &AccountRef) -> i64 { b.Three_shared().unwrap() }
#[test] fn measure_typed() {
    let n: usize = std::env::var("MOSS_TYPED_ITERATIONS").unwrap().parse().unwrap();
    let repeats: usize = std::env::var("MOSS_TYPED_REPEATS").unwrap().parse().unwrap();
    for (case, name) in ["empty", "shared", "exclusive", "two", "mixed", "three"].iter().enumerate() {
        for rep in 0..repeats {
            for backend in (0..2).map(|i| (i+rep)%2) {
                let moss = make_account(); let rust = Manual::new();
                let start = Instant::now();
                for _ in 0..n { black_box(if backend == 0 { generated(black_box(&moss), case) } else { black_box(&rust).call(case) }); }
                println!("TYPED|{}|{}|{}|{:.4}", name, ["moss","rust"][backend], rep, start.elapsed().as_nanos() as f64/n as f64);
            }
        }
    }
}
