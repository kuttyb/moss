// Isolated layout experiment, not a production backend or a proposed default.
use std::collections::BTreeMap;
use std::sync::{RwLock,Barrier};
use std::time::Instant;
#[repr(align(128))]
struct Separated(RwLock<BTreeMap<&'static str,i64>>);
fn trial(a: &RwLock<BTreeMap<&'static str,i64>>, b: &RwLock<BTreeMap<&'static str,i64>>) -> f64 {
    let gate=Barrier::new(3);
    std::thread::scope(|s| {
        let handles: Vec<_>=[a,b].into_iter().map(|lock| { let gate=&gate;s.spawn(move || {gate.wait();for _ in 0..200000 {let mut x=lock.write().unwrap();let n=x.get_mut("v").unwrap();*n=n.wrapping_add(1);std::hint::black_box(*n);}})}).collect();
        let start=Instant::now();gate.wait();for h in handles {h.join().unwrap();}start.elapsed().as_nanos() as f64/400000.0
    })
}
fn main() {
    let lock=|| RwLock::new(BTreeMap::from([("v",0i64)]));
    println!("LAYOUT|class_bytes|{}|class_alignment|{}|separated_bytes|{}",std::mem::size_of::<RwLock<BTreeMap<&str,i64>>>(),std::mem::align_of::<RwLock<BTreeMap<&str,i64>>>(),std::mem::size_of::<Separated>());
    for n in 0..9 {
        let packed=vec![lock(),lock()];let separate=vec![Separated(lock()),Separated(lock())];
        let packed_line_overlap=((&packed[0] as *const _ as usize + std::mem::size_of_val(&packed[0])-1)/64)==((&packed[1] as *const _ as usize)/64);
        if n%2==0 {println!("LAYOUT|packed|{}|{}",trial(&packed[0],&packed[1]),packed_line_overlap);println!("LAYOUT|separated|{}",trial(&separate[0].0,&separate[1].0));}
        else {println!("LAYOUT|separated|{}",trial(&separate[0].0,&separate[1].0));println!("LAYOUT|packed|{}|{}",trial(&packed[0],&packed[1]),packed_line_overlap);}
    }
}
