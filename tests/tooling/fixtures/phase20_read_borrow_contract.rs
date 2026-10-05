// TEST ONLY -- integration contract for the reserved compiler-internal
// FileIO Branch read token. Appended after the FileIO shim; compiles only if
// `moss_fileio_branch_read_borrow(&FileIO)` returns an owned token that is
// `Send + 'static` (so generated Branch work never captures `&FileIO`). This
// checks the seam's shape, not Agent B's implementation.
fn moss_test_require_send_static<T: Send + 'static>(_: &T) {}

#[allow(dead_code)]
fn moss_test_read_borrow_contract(file: &FileIO) {
    let token: MossFileIOReadBorrow = moss_fileio_branch_read_borrow(file);
    moss_test_require_send_static(&token);
    let _: Range = token.read(0, 0);
}
