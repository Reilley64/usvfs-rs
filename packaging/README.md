# Native release staging: publication blocked

This fork starts from upstream `57f1ea5e6ad13f7435a7af184748e6c1312c5637`.
The upstream C++ implementation, presets, manifests and licenses are unchanged.
The inherited automatic build/test/publish workflow is disabled. Tags cannot
publish binaries. The replacement workflow supports manual dispatch and the initial packaging branch.
It uploads only JSON
inventory reports. It does not run injection tests or execute built programs.
Its final publication gate deliberately fails, even after successful builds.

## Run on Windows

Commit the packaging changes before running. Use a fresh checkout with Visual
Studio 2022 C++ x86/x64 tools, Windows SDK, CMake, Git and PowerShell 7 installed.
Clone Microsoft/vcpkg at `74e6536215718009aae747d86d84b78376bf9e09` into a fresh
directory. Do not reuse downloaded packages or build trees from other builds.

```powershell
./packaging/stage-native.ps1 -VcpkgRoot C:/work/vcpkg -OutputDirectory C:/work/new-usvfs-stage
```

Alternatively dispatch **Native staging and source inventory (not a release)**.
It pins the vcpkg tool checkout separately from the two registry baselines.
The script builds Release x86 and x64 sequentially, using unchanged presets with
`BUILD_TESTING=OFF`, separate installation/dependency directories, and binary
caching disabled. The preset still enables the `testing` manifest feature:
**gtest may resolve despite tests being disabled**. Inventory actual resolution.

Local `install-x86` and `install-x64` are staging trees, not release bundles.
Reports record fork/upstream/tool revisions, manifests/registry baselines,
runner and VS/CMake/vcpkg versions, exact command arguments, compiler detection
and CMake caches, actual installed status/list/SBOM/copyright text, and file
SHA-256 inventories for installed dependencies, native outputs, fork files,
observed downloads, registry cache, ports/triplets, packages and build trees.
The workflow retains reports only. Source trees/caches and binaries remain on
the runner and are ephemeral. No source bundle is created or claimed complete.
A failed architecture may have only partial inventory; check `stage-status.json`.
Do not interpret an empty inventory as proof of no dependencies.

## Release blocker

**Fresh Windows execution has not yet been performed.** macOS static checks are
not evidence of successful Windows compilation or complete corresponding source.
Historical builds do not meet this gate. The first manual run provides evidence
for the next source-closure audit, not authorization to release.

Before implementing publication, establish and review complete corresponding
source for both actual triplets, including:

- Fork source, licenses, local changes and matching Rust bindings revision.
- asmjit, spdlog, libudis86 and all static/header/transitive Boost dependencies.
- gtest and any other packages actually resolved, including host/build tools.
- Pinned vcpkg tool source and bootstrap tool identity; both registry baselines,
  selected versioned ports, patches, triplets and build scripts.
- Actual source archives and extracted/patched trees, with content hashes and
  mappings to installed package versions, features, triplets and SPDX records.
- Required notices/licenses and usable offline rebuild recipes/tool prerequisites.

Manifests, copyright files, SPDX records and download caches alone do not prove
source closure. Cache file hashes are observations, not verified source mappings.
Identify omissions and inspect every selected port/patch and source acquisition
path. Preserve the verified sources themselves in a reviewed source bundle;
this first stage intentionally does not invent a broad vendoring mechanism.
Review compiler/SDK/bootstrap provenance and rebuild requirements too.
`assert-release-ready.ps1` has no bypass switch. Replace it only as part of a
reviewed source-complete publishing change with fresh Windows evidence. Until
then, do not upload native binaries or publish GitHub releases.
