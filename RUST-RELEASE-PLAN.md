# Rust bindings and release packaging

## Scope

This fork adds Rust bindings and release packaging to unmodified upstream USVFS.
The initial upstream baseline is `57f1ea5e6ad13f7435a7af184748e6c1312c5637`.
Upstream C++ implementation files remain unchanged. Rust bindings and the existing
exception shim are extracted from Reilley64/mods without behavioral changes.

The consumer pins the Rust crate to an exact Git revision and the native bundle
to a matching release and SHA-256 checksum. Releases must contain both x86 and
x64 Release DLLs and proxy executables, provenance, notices, and complete
corresponding source, including required statically linked dependency source.

## Acceptance criteria

- Both MSVC architectures compile and pass the owned binding checks.
- The native source baseline, release inputs, and four artifact hashes are recorded.
- Release assets and corresponding source are validated before publication.
- Artifact hash verification and existing runtime behavior remain unchanged.
- mods switches only after a matching release is published and validated.

## Publication gate

No new binary release is authorized until the corresponding-source inventory and
packaging are complete and reviewed. A GitHub source archive, vcpkg manifest, or
registry reference alone does not satisfy this gate. Do not push release tags
through the inherited upstream publication workflow to bypass this requirement.

## Non-goals

Upstream implementation changes, process-injection testing, runtime API redesign,
and re-enabling mods preview publication are outside this migration.

## Validation environment

The implementation host is macOS. Windows-specific compilation and owned binding
checks require Windows CI; local static checks are not substitutes for that evidence.
