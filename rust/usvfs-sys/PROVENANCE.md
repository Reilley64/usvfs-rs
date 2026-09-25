# Rust integration provenance

The Rust integration and `rust/usvfs-sys/native/barrier.{h,cpp}` originate from
Reilley64/mods at commit `c9b98e5a8f5fd1775bb7aa99505846705ebb9c9f`.
Original source: https://github.com/Reilley64/mods/tree/c9b98e5a8f5fd1775bb7aa99505846705ebb9c9f/src/infrastructure/execution

These additions are licensed under GPL-3.0-or-later, as in the source project.
The shim files are unchanged; build paths and crate packaging were adapted for
this fork on 2026-09-25. Upstream notices and license terms remain intact.
The upstream native baseline is `57f1ea5e6ad13f7435a7af184748e6c1312c5637`.

The bindings crate does not redistribute native runtime binaries. Native release
publication remains gated on complete corresponding-source packaging and Windows
validation; see RUST-RELEASE-PLAN.md.
