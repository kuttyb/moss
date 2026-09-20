// Benchmark-only baselines. Never linked into ordinary Moss production output.
use std::hint::black_box;
use std::sync::{Barrier, RwLock};
use std::time::Instant;

fn make_bench(size: usize) -> BenchRef {
    CONSTRUCTORS
    bench
}
#[derive(Clone, Copy)]
enum Case { Empty, Immutable, Read, Write, Multiple, Readers, Writers, Mixed, Disjoint, Text, Vector, Object, MessageText, MessageVector, ReplyText, ReplyVector, Nested, Siblings }
fn cases() -> Vec<(&'static str, Case)> {
    use Case::*;
    vec![("empty", Empty), ("immutable", Immutable), ("shared", Read), ("write_helper", Write),
         ("multiple", Multiple), ("readers", Readers), ("writers", Writers), ("reader_writer", Mixed),
         ("disjoint", Disjoint), ("string_read", Text), ("vector_read", Vector), ("object_read", Object),
         ("message_string", MessageText), ("message_vector", MessageVector), ("reply_string", ReplyText),
         ("reply_vector", ReplyVector), ("nested", Nested), ("siblings", Siblings)]
}
#[inline(never)]
fn work(mut x: i64) -> i64 { for i in 0..500 { x = x.wrapping_mul(3).wrapping_add(i); } x }
fn moss_call(b: &BenchRef, case: Case, lane: usize) {
    use Case::*;
    match case {
        Empty => { black_box(b.Empty_shared()); }, Immutable => { black_box(b.Immutable_shared()); },
        Read => { black_box(b.Read_shared()); }, Write => b.Write_shared(),
        Multiple => { black_box(b.Multiple_shared()); }, Readers => { black_box(b.ReadWork_shared()); },
        Writers => b.WriteWork_shared(), Mixed if lane % 2 == 0 => { black_box(b.ReadWork_shared()); },
        Mixed => b.WriteWork_shared(), Disjoint if lane % 2 == 0 => b.WriteWork_shared(),
        Disjoint => b.OtherWork_shared(), Text => { black_box(b.TextRead_shared()); },
        Vector => { black_box(b.VectorRead_shared()); }, Object => { black_box(b.ObjectRead_shared()); },
        MessageText => { black_box(b.ForwardText_shared()); }, MessageVector => { black_box(b.ForwardVector_shared()); },
        ReplyText => { black_box(b.ReplyText_shared()); }, ReplyVector => { black_box(b.ReplyVector_shared()); },
        Nested => { black_box(b.Nested_shared()); }, Siblings => { black_box(b.Siblings_shared()); },
    }
}
struct PlainState { left: i64, right: i64, text: String, values: Vec<i64> }
struct Coarse { state: RwLock<PlainState>, middle: RwLock<i64>, child: RwLock<i64>, sibling: RwLock<i64>, fixed: i64 }
struct Fine { left: RwLock<i64>, right: RwLock<i64>, text: RwLock<String>, values: RwLock<Vec<i64>>, middle: RwLock<i64>, child: RwLock<i64>, sibling: RwLock<i64>, fixed: i64 }
fn bump(x: &mut i64) { *x = x.wrapping_add(1); }
// Message sinks own their input, as Moss message payloads do.
#[inline(never)] fn sink_text(x: String) -> i64 { if x.is_empty() { 0 } else { 1 } }
#[inline(never)] fn sink_values(x: Vec<i64>) -> i64 { x[0] }
fn nested(left: &mut i64, middle: &RwLock<i64>, child: &RwLock<i64>, sibling: &RwLock<i64>, siblings: bool) -> i64 {
    bump(left);
    let higher = if siblings { let mut s = sibling.write().unwrap(); bump(&mut s); *s } else { 0 };
    let mut m = middle.write().unwrap(); bump(&mut m);
    let mut c = child.write().unwrap(); bump(&mut c);
    left.wrapping_add(*m).wrapping_add(*c).wrapping_add(higher)
}
impl Coarse {
    fn new(size: usize) -> Self { Self { state: RwLock::new(PlainState { left: 0, right: 0, text: "x".repeat(size), values: vec![7;size] }), middle: RwLock::new(0), child: RwLock::new(0), sibling: RwLock::new(0), fixed: 7 } }
    fn call(&self, case: Case, lane: usize) {
        use Case::*;
        match case {
            Empty => { black_box(7i64); }, Immutable => { black_box(self.fixed); },
            Write | Multiple | Writers | Disjoint | Nested | Siblings => {
                let mut s = self.state.write().unwrap();
                match case { Write => bump(&mut s.left), Multiple => { bump(&mut s.left); bump(&mut s.right); black_box(s.left.wrapping_add(s.right)); },
                    Writers => s.left = work(s.left), Disjoint if lane % 2 == 0 => s.left = work(s.left), Disjoint => s.right = work(s.right),
                    _ => { black_box(nested(&mut s.left, &self.middle, &self.child, &self.sibling, matches!(case, Siblings))); } }
            },
            Mixed if lane % 2 != 0 => { let mut s = self.state.write().unwrap(); s.left = work(s.left); },
            _ => { let s = self.state.read().unwrap(); match case {
                Read => { black_box(s.left); }, Readers | Mixed => { black_box(work(s.left)); },
                Text => { black_box(if s.text.is_empty() { 0i64 } else { 1 }); }, Vector | Object => { black_box(s.values[0]); },
                MessageText => { black_box(sink_text(s.text.clone())); }, MessageVector => { black_box(sink_values(s.values.clone())); },
                ReplyText => { black_box(s.text.clone()); }, ReplyVector => { black_box(s.values.clone()); }, _ => unreachable!() }
            }
        }
    }
}
impl Fine {
    fn new(size: usize) -> Self { Self { left: RwLock::new(0), right: RwLock::new(0), text: RwLock::new("x".repeat(size)), values: RwLock::new(vec![7;size]), middle: RwLock::new(0), child: RwLock::new(0), sibling: RwLock::new(0), fixed: 7 } }
    fn call(&self, case: Case, lane: usize) {
        use Case::*;
        match case {
            Empty => { black_box(7i64); }, Immutable => { black_box(self.fixed); }, Read => { black_box(*self.left.read().unwrap()); },
            Write => bump(&mut self.left.write().unwrap()),
            Multiple => { let mut a = self.left.write().unwrap(); let mut b = self.right.write().unwrap(); bump(&mut a); bump(&mut b); black_box(a.wrapping_add(*b)); },
            Readers => { black_box(work(*self.left.read().unwrap())); },
            Mixed if lane % 2 == 0 => { black_box(work(*self.left.read().unwrap())); },
            Writers | Mixed => { let mut a = self.left.write().unwrap(); *a = work(*a); },
            Disjoint => { let lock = if lane % 2 == 0 { &self.left } else { &self.right }; let mut a = lock.write().unwrap(); *a = work(*a); },
            Text => { black_box(if self.text.read().unwrap().is_empty() { 0i64 } else { 1 }); },
            Vector | Object => { black_box(self.values.read().unwrap()[0]); },
            MessageText => { let x = self.text.read().unwrap(); black_box(sink_text(x.clone())); },
            MessageVector => { let x = self.values.read().unwrap(); black_box(sink_values(x.clone())); },
            ReplyText => { black_box(self.text.read().unwrap().clone()); }, ReplyVector => { black_box(self.values.read().unwrap().clone()); },
            Nested | Siblings => { let mut a = self.left.write().unwrap(); black_box(nested(&mut a, &self.middle, &self.child, &self.sibling, matches!(case, Siblings))); }
        }
    }
}
fn sample<F: Fn(usize) + Sync>(threads: usize, n: usize, f: F) -> f64 {
    let start = Barrier::new(threads + 1);
    std::thread::scope(|scope| {
        let mut joins = Vec::new();
        for lane in 0..threads { let f = &f; let start = &start; joins.push(scope.spawn(move || { start.wait(); for _ in 0..n { f(lane); } })); }
        let t = Instant::now(); start.wait(); for j in joins { j.join().unwrap(); }
        t.elapsed().as_nanos() as f64 / (n * threads) as f64
    })
}
#[test]
fn measure() {
    let iterations: usize = std::env::var("MOSS_BENCH_ITERATIONS").unwrap().parse().unwrap();
    let max_threads: usize = std::env::var("MOSS_BENCH_THREADS").unwrap().parse().unwrap();
    let repeat: usize = std::env::var("MOSS_BENCH_REPEATS").unwrap().parse().unwrap();
    for (name, case) in cases() {
        let sized = matches!(case, Case::Text | Case::Vector | Case::Object | Case::MessageText | Case::MessageVector | Case::ReplyText | Case::ReplyVector);
        let concurrent = matches!(case, Case::Readers | Case::Writers | Case::Mixed | Case::Disjoint);
        for size in if sized { vec![16,4096,1048576] } else { vec![16] } {
            for threads in [1,2,4,8].into_iter().filter(|t| *t <= max_threads && (concurrent || *t == 1)) {
                let n = if sized && size > 4096 && !matches!(case, Case::Text | Case::Vector | Case::Object) { iterations.min(128) } else { iterations };
                for iteration in 0..repeat {
                    // Rotate order between repetitions to limit systematic thermal/order bias.
                    for backend in (0..3).map(|i| (i + iteration) % 3) {
                        let elapsed = match backend {
                            0 => { let b = make_bench(size); sample(threads,n,|lane| moss_call(black_box(&b),case,lane)) },
                            1 => { let b = Coarse::new(size); sample(threads,n,|lane| b.call(case,lane)) },
                            _ => { let b = Fine::new(size); sample(threads,n,|lane| b.call(case,lane)) },
                        };
                        println!("MOSS_PERF|{}|{}|{}|{}|{}|{:.3}", name, ["moss","coarse","rust"][backend], threads,size,iteration,elapsed);
                    }
                }
            }
        }
    }
}
