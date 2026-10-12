use std::alloc::{GlobalAlloc, Layout, System};
use std::hint::black_box;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

struct CountingAllocator;

static TRACKING: AtomicBool = AtomicBool::new(false);
static ALLOCATIONS: AtomicUsize = AtomicUsize::new(0);
static ALLOCATED_BYTES: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if TRACKING.load(Ordering::Relaxed) {
            ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
            ALLOCATED_BYTES.fetch_add(layout.size(), Ordering::Relaxed);
        }
        // SAFETY: this allocator forwards the unchanged layout to System.
        unsafe { System.alloc(layout) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        // SAFETY: ptr/layout came from the forwarded System allocation.
        unsafe { System.dealloc(ptr, layout) }
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, size: usize) -> *mut u8 {
        if TRACKING.load(Ordering::Relaxed) {
            ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
            ALLOCATED_BYTES.fetch_add(size, Ordering::Relaxed);
        }
        // SAFETY: ptr/layout came from System and size is forwarded unchanged.
        unsafe { System.realloc(ptr, layout, size) }
    }
}

#[global_allocator]
static GLOBAL: CountingAllocator = CountingAllocator;

fn measured(label: &str, body: impl FnOnce()) {
    ALLOCATIONS.store(0, Ordering::Relaxed);
    ALLOCATED_BYTES.store(0, Ordering::Relaxed);
    TRACKING.store(true, Ordering::Relaxed);
    body();
    TRACKING.store(false, Ordering::Relaxed);
    println!(
        "{label}: allocations={} allocated_bytes={}",
        ALLOCATIONS.load(Ordering::Relaxed),
        ALLOCATED_BYTES.load(Ordering::Relaxed)
    );
}

fn equivalent_segment_builder(count: usize) -> String {
    let mut parts = Vec::<String>::new();
    for _ in 0..count {
        let part = String::from("abcdefgh");
        black_box(parts.push(black_box(part.clone())));
    }
    parts.join("")
}

fn reserved_string_builder(count: usize) -> String {
    let mut result = String::with_capacity(count * 8);
    for _ in 0..count {
        black_box(result.push_str(black_box("abcdefgh")));
    }
    result
}

fn main() {
    measured("Rust ASCII lower 4096 bytes", || {
        let value = black_box("AbCdEfGh".repeat(512));
        let result = black_box(value.to_ascii_lowercase());
        assert_eq!(result.len(), 4_096);
    });
    measured("Rust equivalent segment builder 128", || {
        let result = black_box(equivalent_segment_builder(128));
        assert_eq!(result.len(), 1_024);
    });
    measured("Rust reserved String builder 128", || {
        let result = black_box(reserved_string_builder(128));
        assert_eq!(result.len(), 1_024);
    });
}
