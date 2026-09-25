# Native staging: publication blocked

This fork starts from upstream `57f1ea5e6ad13f7435a7af184748e6c1312c5637`.
The upstream native code, presets, manifests and licenses are unchanged.
The inherited publishing workflow is disabled. The replacement workflow never
runs injection tests or executes built programs. Its final publication gate
intentionally fails, including after successful source collection.

Windows run **36079703237** successfully built Release x86 and x64 with
`BUILD_TESTING=OFF`, binary caching disabled, and separate build/install trees.
It inventoried 69 packages per architecture (66 target and three host helpers),
65 resource archives and prepared source roots. It uploaded reports only.
That proves the staging build, not corresponding-source completeness.

## Next source-only stage

Commit these packaging changes before execution. Dispatch **Native staging and
source inventory (not a release)** on `feat/rust-release-packaging`, or let its
packaging-path push trigger run. The workflow repeats both clean Windows builds,
then collects the actual verified sources on the same runner before it expires.
It uploads only JSON reports and `source-candidate-UNVERIFIED.tar.gz`. It never
uploads native/tool binaries, entire caches, or a partial candidate.

Local equivalent (PowerShell 7, Windows, VS 2022 C++ x86/x64 and SDK required):

```powershell
./packaging/stage-native.ps1 -VcpkgRoot C:/work/vcpkg -OutputDirectory C:/work/new-usvfs-stage
./packaging/collect-sources.ps1 -VcpkgRoot C:/work/vcpkg -StageDirectory C:/work/new-usvfs-stage
```

Use a fresh Microsoft/vcpkg checkout at
`74e6536215718009aae747d86d84b78376bf9e09`. Do not reuse prior downloads/buildtrees.
Staging disables generated Python bytecode so prepared generator roots remain
source-only, and now uses the correct `X_VCPKG_REGISTRIES_CACHE` variable. The original
inventory used an ineffective variable, so its empty custom registry directory
was not evidence that resolved recipes were absent. The collector selects exact
SPDX Git-tree recipe directories and verifies every declared file, including
custom asmjit patches and spdlog recipe files. It fails with a precise missing
path/hash/resource gap rather than substituting current tool ports.

Read [SOURCE-CANDIDATE.md](SOURCE-CANDIDATE.md) for contents, identity checks,
external build-tool exclusions, and the unvalidated archive-only rebuild plan.
The new collector has not yet passed Windows CI. Source completeness and rebuild
closure remain UNVERIFIED even when all identity checks pass.

## Release blocker

Review the source candidate and notices; validate an archive-only library-source
rebuild using verified overlay ports/local assets and separately provisioned
external tools. Source manifests and successful builds alone are insufficient.
`assert-release-ready.ps1` retains its unconditional block and has no bypass.
Replace it only in a reviewed source-complete publishing change with fresh
Windows evidence. No GitHub release or native-binary upload is authorized by
this collection stage.
