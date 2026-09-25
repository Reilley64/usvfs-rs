# Native source validation and release packaging

The fork preserves upstream native revision
`57f1ea5e6ad13f7435a7af184748e6c1312c5637`. Native code, presets, manifests,
and licenses are unchanged. No injection tests are run.

Windows run [36084606525](https://github.com/Reilley64/usvfs-rs/actions/runs/36084606525)
passed both original Release builds, source collection, and both archive-only
library-source rebuilds. All 69 package identities matched for each architecture.
Only the then-disabled publication gate failed. Independent review checked all
170 recipe files, 65 source archives plus the Boost license, prepared source roots,
and dependency notices. The approved hashes are in `source-approval.json`.

## Source validation

The manual **Native staging and source inventory** workflow builds both original
architectures, collects sources, and rebuilds from the resulting archive. It
uploads source candidates and reports only, never native or helper binaries.

With Windows, PowerShell 7, VS2022 C++ x86/x64 and SDK, use a fresh vcpkg checkout
at `74e6536215718009aae747d86d84b78376bf9e09`:

```powershell
./packaging/stage-native.ps1 -VcpkgRoot C:/work/vcpkg -OutputDirectory C:/work/stage
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
