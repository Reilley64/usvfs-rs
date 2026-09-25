# Corresponding-source candidate — COMPLETENESS UNVERIFIED

This is review evidence, not a release or a claim of complete corresponding source.
No native library, proxy executable, vcpkg.exe, or downloaded build-tool archive is
included by the collector. Publication remains blocked.

## Contents and verification

- `checkouts/fork.tar.gz`: exact staging fork commit, including native source,
  Rust bindings/shim, manifests, licenses, presets and packaging instructions.
- `checkouts/vcpkg.tar.gz`: exact pinned vcpkg Git source, including bootstrap
  scripts, build scripts and triplets. This is NOT the bootstrapped executable.
- `overlay-ports/`: all 69 resolved recipes, including patches and helper scripts.
  Every declared recipe file must match its SPDX SHA256. The exact resolved Git
  tree comes from SPDX and the isolated registry cache, not current tool ports.
- `assets/`: 65 original source tarballs mapped by the SPDX Git resource URL/ref
  and checked against SPDX SHA512 plus the original download inventory SHA256.
  The Boost license is checked against its SPDX SHA512 as well
  as its inventoried SHA256. No other downloaded archives are collected.
- `prepared/<port>/src/<root>/`: complete prepared source roots, checked against
  staging inventory. Only the exact `buildtrees/<port>/src/` boundary is copied.
  Source-distributed data/fixtures remain intact. Staging disables Python
  bytecode generation; the collector rejects `.pyc`/`.pyo` along with native
  executable/library outputs rather than silently omitting source-root files.
- `notices/`: installed dependency copyright notices and bundled fmt license.
  Original notices remain in the full source trees and archives as well.
- `evidence/`: status, SPDX, versions, features, triplets and provenance reports.
  These include observations of external tools, NOT those tools themselves.
- `source-map.json`: per-architecture package → recipe/resource/prepared mappings.
- `manifest.json`: size/SHA256 of every other candidate file. The outer archive's
  SHA256 is recorded separately in the workflow inventory artifact's
  `source-collection-status.json` (avoids a self-referential hash).

The package counts are frozen to the reviewed Windows run 36079703237. Changed
resolution, missing recipes/resources, mismatches and unexpected native/tool
binaries fail collection before an uploadable archive exists. This verifies
input identity, not semantic source completeness or reproducibility.

## Archive-only library-source rebuild: Windows validation pending

A separate clean Windows validation must extract the two checkouts, use all
verified `overlay-ports` as `VCPKG_OVERLAY_PORTS`, and stage the original source
archives/Boost license in a local vcpkg asset mirror keyed by SHA512. The local
mirror uses the documented `X_VCPKG_ASSET_SOURCES` source
`x-azurl,file://<absolute-mirror>/,,read`. Preserve the original filenames too.
Use the original presets, Release configuration, `BUILD_TESTING=OFF`, the
inherited testing manifest feature, and `VCPKG_BINARY_SOURCES=clear`.

External prerequisites must be supplied and recorded independently: Windows,
Visual Studio/MSVC x86+x64, Windows SDK, Git, PowerShell, CMake, bootstrap vcpkg,
and the build helpers vcpkg actually requests (including Ninja, Python and MSYS2).
Downloaded executable archives are intentionally NOT redistributed. This is not
an offline toolchain closure claim. `x-block-origin` affects tool acquisition as
well as library assets: only enable it after external tools are provisioned and
validated. Merely using a mirror while allowing origin fallback does not prove
an archive-only rebuild. `rebuild-sources.ps1` implements this check using a separate null-registry manifest
and verified overlays; see `SOURCE-REBUILD-INTEGRATION.md`. It has not yet passed
Windows validation. This is not a general network sandbox; recipe acquisition
commands must also be reviewed.

Remaining release gates: inspect every source acquisition/patch and notice,
validate library-source-only acquisition with external tools independently
provisioned, compare both rebuilt architectures, review licensing/source offer
and Rust release alignment. Do not remove the publication gate based only on a
successful collector run. Do not run injection tests in this staging workflow.

## API evidence

- https://learn.microsoft.com/en-us/vcpkg/users/assetcaching
- https://learn.microsoft.com/en-us/vcpkg/users/config-environment
- https://github.com/microsoft/vcpkg-tool/blob/71538f2694db93da4668782d094768ba74c45991/src/vcpkg/vcpkgpaths.cpp
- https://github.com/microsoft/vcpkg-tool/blob/71538f2694db93da4668782d094768ba74c45991/src/vcpkg/registries.cpp
- https://github.com/microsoft/vcpkg-tool/blob/71538f2694db93da4668782d094768ba74c45991/src/vcpkg/spdx.cpp

The pinned tool reads `X_VCPKG_REGISTRIES_CACHE`, not
`VCPKG_REGISTRIES_CACHE`; resolved Git recipes live in `git-trees/<tree-id>`.
The collector requires this controlled cache and has no guessed recipe fallback.
