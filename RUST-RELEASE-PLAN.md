# Rust bindings and release packaging

## Scope

This fork adds Rust bindings and release packaging to a pinned upstream USVFS
baseline plus one exact approved issue #33 delta. The initial upstream baseline is
`57f1ea5e6ad13f7435a7af184748e6c1312c5637`.
Rust bindings and the existing exception shim are extracted from Reilley64/mods
without behavioral changes. Issue #33 adds one approved upstream exception: the
headless cross-bitness proxy treats unavailable shared-memory logging as optional
instead of opening a modal dialog before injection.

The consumer pins the Rust crate to an exact Git revision and the native bundle
to a matching release and SHA-256 checksum. Releases must contain both x86 and
x64 Release DLLs and proxy executables, provenance, notices, and complete
corresponding source, including required statically linked dependency source.

## Acceptance criteria

- Both MSVC architectures compile and pass the owned binding checks.
- The native source baseline, release inputs, and four artifact hashes are recorded.
- Release assets and corresponding source are validated before publication.
- All artifact hashes remain verified; runtime behavior outside the approved issue #33 proxy fallback remains unchanged.
- mods switches only after a matching release is published and validated.

## Publication gate

No new binary release is authorized until the corresponding-source inventory and
packaging are complete and reviewed. A GitHub source archive, vcpkg manifest, or
registry reference alone does not satisfy this gate. Do not push release tags
through the inherited upstream publication workflow to bypass this requirement.

## Non-goals

Other upstream implementation changes, runtime API redesign, and re-enabling
mods preview publication are outside this migration. Process-injection testing is
limited to the approved issue #33 cross-bitness regression.

## Validation environment

The implementation host is macOS. Windows-specific compilation and owned binding
checks require Windows CI; local static checks are not substitutes for that evidence.
