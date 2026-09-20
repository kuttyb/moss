// Optional event aggregation is thread-local. The hot path takes no global lock.
// Durations are diagnostic and include instrumentation overhead; production
// throughput is always measured in a separate, uninstrumented binary.
#[derive(Default)]
struct Totals { instance: String, calls: u64, shared: u64, exclusive: u64, contended: u64, wait: u128, hold: u128, elapsed: u128, nested: u128, depth: usize }
struct Active { start: std::time::Instant, children: u128, requested: std::time::Instant, locks: std::collections::BTreeMap<usize,std::time::Instant> }
thread_local! {
    static PERF_STACK: std::cell::RefCell<Vec<Active>> = const { std::cell::RefCell::new(Vec::new()) };
    static PERF_TOTALS: std::cell::RefCell<std::collections::BTreeMap<String, Totals>> = const { std::cell::RefCell::new(std::collections::BTreeMap::new()) };
}
static ACTIVE: [std::sync::atomic::AtomicUsize;64] = [const { std::sync::atomic::AtomicUsize::new(0) };64];
static WRITERS: [std::sync::atomic::AtomicUsize;64] = [const { std::sync::atomic::AtomicUsize::new(0) };64];
static MAX_ACTIVE: [std::sync::atomic::AtomicUsize;64] = [const { std::sync::atomic::AtomicUsize::new(0) };64];
static ACTIVE_ALL: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
static MAX_ALL: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
fn performance_hook(event: &str, instance: &str, handler: &str, domain: usize, class: usize, exclusive: bool) {
    use std::sync::atomic::Ordering::SeqCst;
    if event == "lock_acquired" {
        let index = domain*8+class;
        let active = ACTIVE[index].fetch_add(1,SeqCst)+1;
        if exclusive { assert_eq!(active,1); assert_eq!(WRITERS[index].fetch_add(1,SeqCst),0); }
        else { assert_eq!(WRITERS[index].load(SeqCst),0); }
        MAX_ACTIVE[index].fetch_max(active,SeqCst);
        MAX_ALL.fetch_max(ACTIVE_ALL.fetch_add(1,SeqCst)+1,SeqCst);
    } else if event == "lock_releasing" {
        let index=domain*8+class;
        if exclusive { assert_eq!(WRITERS[index].fetch_sub(1,SeqCst),1); }
        assert!(ACTIVE[index].fetch_sub(1,SeqCst)>0);
        ACTIVE_ALL.fetch_sub(1,SeqCst);
    }
    let now = std::time::Instant::now();
    PERF_STACK.with(|stack| { PERF_TOTALS.with(|totals| {
        let mut stack = stack.borrow_mut(); let mut totals = totals.borrow_mut();
        if !totals.contains_key(handler) { totals.insert(handler.to_owned(), Totals { instance: instance.to_owned(), ..Totals::default() }); }
        let value = totals.get_mut(handler).unwrap();
        match event {
            "handler_enter" => { stack.push(Active { start: now, children: 0, requested: now, locks: std::collections::BTreeMap::new() }); value.depth = value.depth.max(stack.len()); },
            "lock_acquire" => { stack.last_mut().unwrap().requested = now; },
            "lock_contended" => value.contended += 1,
            "lock_acquired" => { let f = stack.last_mut().unwrap(); value.wait += now.duration_since(f.requested).as_nanos(); f.locks.insert(class,now); if exclusive { value.exclusive += 1; } else { value.shared += 1; } },
            "lock_release" => { value.hold += now.duration_since(stack.last_mut().unwrap().locks.remove(&class).unwrap()).as_nanos(); },
            "handler_exit" => { let f = stack.pop().unwrap(); assert!(f.locks.is_empty()); let elapsed = now.duration_since(f.start).as_nanos(); value.elapsed += elapsed; value.nested += f.children; value.calls += 1; if let Some(parent) = stack.last_mut() { parent.children += elapsed; } },
            _ => {}
        }
    }); });
}
fn report_instrumentation(label: &str) {
    PERF_STACK.with(|s| assert!(s.borrow().is_empty()));
    PERF_TOTALS.with(|totals| { for (identity,t) in std::mem::take(&mut *totals.borrow_mut()) {
        println!("MOSS_LOCK|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", label,format!("{}:{}",t.instance,identity),t.calls,t.shared,t.exclusive,t.contended,t.wait,t.hold,t.elapsed,t.nested,t.depth);
    } });
}
#[test]
fn instrumented() {
    MOSS_LOCK_HOOK.set(performance_hook).unwrap();
    let bench = make_bench(4096);
    for _ in 0..2000 { moss_call(&bench,Case::Nested,0); moss_call(&bench,Case::Siblings,0); }
    report_instrumentation("nested");
    for x in &MAX_ACTIVE { x.store(0,std::sync::atomic::Ordering::SeqCst); }
    MAX_ALL.store(0,std::sync::atomic::Ordering::SeqCst);
    for case in [Case::Readers,Case::Writers,Case::Mixed,Case::Disjoint] {
        let gate = std::sync::Barrier::new(4);
        std::thread::scope(|scope| { for lane in 0..4 { let gate = &gate; let bench = &bench; scope.spawn(move || {
            gate.wait(); for _ in 0..4000 { moss_call(bench,case,lane); }
            report_instrumentation(match case { Case::Readers => "readers", Case::Writers => "writers", Case::Mixed => "mixed", _ => "disjoint" });
        }); } });
        use std::sync::atomic::Ordering::SeqCst;
        println!("MOSS_OVERLAP|{}|{}|{}", match case { Case::Readers => "readers", Case::Writers => "writers", Case::Mixed => "mixed", _ => "disjoint" }, MAX_ACTIVE[0].swap(0,SeqCst), MAX_ALL.swap(0,SeqCst));
        assert_eq!(ACTIVE_ALL.load(SeqCst),0);
    }
}
