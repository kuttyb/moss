# Moss compiler v0.1

This is the latest implemented Moss compiler currently available. It is a dependency-free C++17 front end that validates Moss source and emits standalone Rust.

## Requirements

- A C++17 compiler (`g++`, `clang++`, or Apple Clang)
- `make`
- Rust (`rustc`) to compile the generated program

## Build

```sh
make
```

This creates `./moss`.

## Use

```sh
./moss --check examples/counter.moss
mkdir -p build
./moss examples/counter.moss -o build/counter.rs
rustc build/counter.rs -o build/counter
./build/counter
```

Expected output:

```text
counter: 41
counter: 42
```

Run the complete smoke test with `make check`.

## Implemented language slice

- Domain-owned mutable state
- Serialized, run-to-completion domain handlers
- Cross-domain asynchronous message sends
- `spawn` from `main`
- Primitive and object value payloads
- `let`, `var`, `if`/`else`, `while`, `echo`, and bare `return`
- Nim-style `and`, `or`, `not`, `true`, and `false`
- `int`, `float`, `bool`, `string`, `seq`, `option`, and `table` lowering

## Important status

This is an early v0.1 prototype, not the compiler for the complete language we subsequently designed. In particular, it does not yet implement `await`/replies, later ordering and failure semantics, blocking FFI rules, arenas, generics/traits, Domain Clustering, or message-elimination optimizations.

Non-primitive message payloads are conservatively cloned. Receiver-type recovery also relies on parameter names in some handler cases. Treat the syntax and generated runtime as experimental.

## Platforms

The source builds on Linux and macOS with a C++17 compiler. The included `bin/moss-linux-x86_64` is a convenience build for 64-bit Linux; macOS and Windows users should build from source.
