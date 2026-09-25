# usvfs-sys

Raw, unsafe Rust bindings to the existing USVFS C++ exception barrier. This crate
compiles the unchanged shim and generates bindings from `../../include` in this
Git checkout. It does not build, package, download, or verify the native DLLs and
proxy executables. Use it from the fork checkout or an exact Git revision, not as
a standalone copy of this directory.

Only `x86_64-pc-windows-msvc` and `i686-pc-windows-msvc` are supported on Windows.
The crate checks use the pinned Rust toolchain; Git consumers select their own
compatible toolchain. Building requires MSVC C++ tools, Windows SDK, and
libclang with Windows headers available. Non-Windows builds expose no API and
skip native compilation; they do not validate Windows bindings or linking.

The generated declarations intentionally expose an unsafe boundary. Callers must
provide valid, correctly terminated strings and pointers, serialize session use,
and enforce session lifetime and Windows handle ownership. C++ exception handling
does not establish Rust memory safety. Keep safe runtime, domain, and process
facades in the consumer. `MODS_CLEANUP_TIMEOUT_MS` and the two allowed `LINKFLAG_*`
constants come from the unchanged shim and upstream headers.

The consumer remains responsible for `MODS_USVFS_ARTIFACTS`, source-revision
validation, and all four native artifact hashes. This extraction does not change
source-revision semantics or authorize binary publication.

From this directory:

```text
cargo fmt --check
cargo check
cargo clippy --all-targets -- -D warnings
```

Windows validation must also check both supported MSVC targets. A successful
non-Windows check is not evidence that either Windows target works.
