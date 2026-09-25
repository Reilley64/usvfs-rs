# Archive-only library-source rebuild validator (Windows validation pending)

Run immediately after `collect-sources.ps1`, on the same Windows runner:

```powershell
./packaging/rebuild-sources.ps1 -StageDirectory "$env:RUNNER_TEMP/usvfs-stage" -VcpkgRoot "$env:RUNNER_TEMP/vcpkg" -OutputDirectory "$env:RUNNER_TEMP/usvfs-source-rebuild"
```

Use the actual collector stage/tool paths if different. The output must not exist.
Upload only `usvfs-source-rebuild/reports`, never its fork, tools, downloads,
installed dependencies, build trees, or four native outputs.

The validator verifies the outer archive against the separate collection status,
then every candidate manifest entry before extracting either checkout. It uses
fresh fork/tool/build/installed paths. Prepared source trees are evidence only;
the rebuild starts from the original archived assets and verified recipes.

The original fork manifest, registry configuration, presets and native files stay
unchanged. A separate `VCPKG_MANIFEST_DIR` contains an exact manifest copy and a
`default-registry: null` configuration. All 69 collected recipes are overlays.
This prevents registry baseline downloads rather than hoping overlays suppress
them. Both architecture resolutions must match the original installed status by
package, architecture (including host packages), version, port-version and feature.
Any mismatch fails before that architecture's native build. The isolated registry
cache must remain empty. This is equivalent resolved input verification, not a
claim that the original registry configuration can resolve offline unchanged.

The source archive supplies all library assets. Original filenames and a SHA512
file mirror are staged. `VCPKG_BINARY_SOURCES=clear` and
`X_VCPKG_ASSET_SOURCES=clear;x-azurl,file://.../,,read;x-block-origin` disable binary
reuse and vcpkg asset-origin fallback. This is not a general network sandbox:
review of arbitrary acquisition commands in the pinned recipes remains a release
gate. Do not interpret the success marker as proof that arbitrary third-party
scripts cannot access the network. Missing provisioned helpers fail rather than
relaxing the origin block.

The original stage's `downloads/tools` and bootstrapped `vcpkg.exe` are external
prerequisites. They are copied into fresh validation locations and hash-recorded
independently. They are NOT added to the source archive or redistributed. The
same runner supplies Visual Studio/MSVC, Windows SDK, CMake, Git, and PowerShell.
MSYS2 and other helper executables intentionally remain external. No offline
compiler/tool closure or corresponding source for redistributed tools is claimed.

Both original presets build Release INSTALL with BUILD_TESTING=OFF and inherited
`testing` manifest feature. Reports contain package comparisons and SHA256 of all
four outputs. No runtime or injection tests run. No PE byte-reproducibility claim
is made. Publication and source-completeness gates remain blocked.

Local check: PowerShell parser on macOS only. Windows build validation is not
available locally; this script is not yet demonstrated on Windows.

## API evidence consulted

Context7 was attempted but unavailable (quota). Official live documentation:

- https://learn.microsoft.com/en-us/vcpkg/users/assetcaching — SHA512 mirror,
  `x-azurl`, `clear`, `x-block-origin`.
- https://learn.microsoft.com/en-us/vcpkg/users/registries — null default registry.
- https://learn.microsoft.com/en-us/vcpkg/users/config-environment — overlay ports,
  isolated downloads and registry cache.
- https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration —
  `VCPKG_MANIFEST_DIR`, `VCPKG_OVERLAY_PORTS`, feature inheritance.
- https://github.com/microsoft/vcpkg/blob/74e6536215718009aae747d86d84b78376bf9e09/scripts/cmake/vcpkg_download_distfile.cmake
  — pinned helper delegates asset acquisition to `vcpkg x-download`.
