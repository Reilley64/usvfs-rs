# Native source validation and release packaging

The fork preserves upstream native baseline
`57f1ea5e6ad13f7435a7af184748e6c1312c5637` plus the exact approved issue #33
proxy delta recorded in `source-approval.json`. Other native code, presets,
manifests, and licenses remain unchanged.

Windows run [36245638937](https://github.com/Reilley64/usvfs-rs/actions/runs/36245638937)
built both Release architectures, collected source, and rebuilt both architectures
from the collected archive with origin fallback blocked. Review verified the
16,347-entry manifest, all 170 recipe files, 65 source archives plus the Boost
license, unchanged dependency notices, and the exact proxy source blob. The
approved hashes and run identity are in `source-approval.json`.

## Source validation

The manual **Native staging and source inventory** workflow builds both original
architectures, collects sources, and rebuilds from the resulting archive. It
uploads source candidates and reports only, never native or helper binaries.

With Windows, PowerShell 7, VS2022 C++ x86/x64 and SDK, use a fresh vcpkg checkout
at `74e6536215718009aae747d86d84b78376bf9e09`:

```powershell
./packaging/stage-native.ps1 -VcpkgRoot C:/work/vcpkg -OutputDirectory C:/work/stage
./packaging/test-proxy-missing-logger.ps1 -StageDirectory C:/work/stage
./packaging/collect-sources.ps1 -VcpkgRoot C:/work/vcpkg -StageDirectory C:/work/stage
./packaging/rebuild-sources.ps1 -VcpkgRoot C:/work/vcpkg -StageDirectory C:/work/stage -OutputDirectory C:/work/rebuild
```

The collector validates exact SPDX recipes/assets and records the one excluded
generated Python bytecode cache while preserving its preferred source. The
rebuild uses verified overlay ports, a null default registry, local source assets,
and blocked vcpkg origin fallback. External build tools and their four helper
archives are supplied separately and are not redistributed. This is not a
whole-machine network sandbox or an offline toolchain claim.

See [SOURCE-CANDIDATE.md](SOURCE-CANDIDATE.md) and
[SOURCE-REBUILD-INTEGRATION.md](SOURCE-REBUILD-INTEGRATION.md) for the precise
source layout, acquisition controls, prerequisites and limitations.

## Release

The **Native release preparation** workflow repeats all checks and packages
matching native/source assets only when their fixed input hashes match the
reviewed approval. It does not automatically publish a GitHub release.
See [FINAL-RELEASE.md](FINAL-RELEASE.md). Final Windows ZIP validation and explicit
reviewed publication still follow the successful source validation.
