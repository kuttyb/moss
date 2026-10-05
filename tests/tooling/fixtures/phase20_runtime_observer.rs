
// ---------------------------------------------------------------------------
// TEST-ONLY observer for tests/tooling/check_phase20_executor_fileio.py.
// Never emitted by the compiler. Appended to generated code and compiled with
// `--cfg moss_perf`, it installs Agent C's instrumentation hook before `main`
// (Linux .init_array) and prints Branch publication/execution and Solo entry to stdout so the
// harness can check the K-window publish-before-join shape on the real
// runtime.
// ---------------------------------------------------------------------------
fn moss_test_observer(event: MossRtEvent, _root: u64, _branch: u64, _worker: usize) {
    match event {
        MossRtEvent::BranchPublished => println!("moss-branch publish"),
        MossRtEvent::BranchCompleted => println!("moss-branch run"),
        MossRtEvent::SoloEnter => println!("moss-solo enter"),
        MossRtEvent::WorkerSpawned => println!("moss-worker spawned {}", _worker),
        _ => {}
    }
}

extern "C" fn moss_test_install_observer() {
    moss_rt_set_hook(moss_test_observer);
}

#[used]
#[unsafe(link_section = ".init_array")]
static MOSS_TEST_INSTALL_OBSERVER: extern "C" fn() = moss_test_install_observer;
