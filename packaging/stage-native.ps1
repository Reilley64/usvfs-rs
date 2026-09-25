# Staging only. This script never publishes or claims source completeness.
param(
    [Parameter(Mandatory)][string]$VcpkgRoot,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw "Requires Windows and Visual Studio 2022 C++ x86/x64 tools" }
$source = Split-Path $PSScriptRoot -Parent
$upstream = "57f1ea5e6ad13f7435a7af184748e6c1312c5637"
$toolRevision = "74e6536215718009aae747d86d84b78376bf9e09"
$VcpkgRoot = (Resolve-Path $VcpkgRoot).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path $OutputDirectory) { throw "Use a new output directory; stale evidence is not accepted" }
$reports = New-Item -ItemType Directory -Path "$OutputDirectory/reports" -Force
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}
function Write-Json($Value, [string]$Name) {
    $Value | ConvertTo-Json -Depth 20 | Set-Content -Encoding utf8 "$reports/$Name.json"
}
function Get-Inventory([string]$Root) {
    if (Test-Path $Root) {
        Get-ChildItem $Root -File -Recurse -Force | Sort-Object FullName | ForEach-Object {
            [ordered]@{
                path = [IO.Path]::GetRelativePath($Root, $_.FullName).Replace('\', '/')
                bytes = $_.Length
                sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
    }
}
$state = [ordered]@{ publication = "blocked"; windowsBuild = "started"; architectures = @(); error = $null }
try {
    $fork = Invoke-Checked git @('-C', $source, 'rev-parse', 'HEAD')
    Invoke-Checked git @('-C', $source, 'merge-base', '--is-ancestor', $upstream, 'HEAD')
    # Packaging and bindings may change; upstream native inputs must not.
    Invoke-Checked git @('-C', $source, 'diff', '--exit-code', $upstream, '--', 'src', 'include', 'cmake', 'CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json', 'vcpkg-configuration.json', 'LICENSE', 'licenses')
    $untrackedNative = Invoke-Checked git @('-C', $source, 'ls-files', '--others', '--exclude-standard', '--', 'src', 'include', 'cmake')
    if ($untrackedNative) { throw "Untracked native inputs are not accepted" }
    $dirty = Invoke-Checked git @('-C', $source, 'status', '--porcelain', '--untracked-files=no')
    if ($dirty) { throw "Tracked fork files must be committed before staging" }
    $actualTool = Invoke-Checked git @('-C', $VcpkgRoot, 'rev-parse', 'HEAD')
    if ($actualTool -ne $toolRevision) { throw "Wrong vcpkg tool revision" }
    Invoke-Checked git @('-C', $VcpkgRoot, 'diff', '--exit-code', 'HEAD')
    $env:VCPKG_ROOT = $VcpkgRoot
    $env:VCPKG_BINARY_SOURCES = 'clear'
    $env:VCPKG_DOWNLOADS = "$OutputDirectory/downloads"
    $env:VCPKG_DEFAULT_BINARY_CACHE = "$OutputDirectory/binary-cache"
    $env:VCPKG_REGISTRIES_CACHE = "$OutputDirectory/registries"
    $env:VCPKG_DISABLE_METRICS = '1'
    New-Item -ItemType Directory -Force $env:VCPKG_DOWNLOADS, $env:VCPKG_DEFAULT_BINARY_CACHE, $env:VCPKG_REGISTRIES_CACHE | Out-Null
    Invoke-Checked "$VcpkgRoot/bootstrap-vcpkg.bat" @('-disableMetrics')
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $provenance = [ordered]@{
        forkRevision = "$fork"; upstreamRevision = $upstream; vcpkgToolRevision = $actualTool
        capturedUtc = [DateTime]::UtcNow.ToString('o'); runnerImage = $env:ImageVersion
        runId = $env:GITHUB_RUN_ID; runAttempt = $env:GITHUB_RUN_ATTEMPT
        cmake = @(Invoke-Checked cmake @('--version'))
        vcpkg = @(Invoke-Checked "$VcpkgRoot/vcpkg.exe" @('version'))
        visualStudio = @(Invoke-Checked $vswhere @('-all', '-format', 'json'))
        registries = Get-Content "$source/vcpkg-configuration.json" -Raw | ConvertFrom-Json
        manifest = Get-Content "$source/vcpkg.json" -Raw | ConvertFrom-Json
        buildTesting = 'OFF'; manifestFeatures = 'testing (inherited preset; gtest may resolve)'
        sourceCompleteness = 'UNPROVEN'; binaryCaching = 'clear'
    }
    Write-Json $provenance 'provenance'
    $trackedFiles = Invoke-Checked git @('-C', $source, 'ls-files')
    Write-Json @($trackedFiles | ForEach-Object { @{ path = $_; sha256 = (Get-FileHash (Join-Path $source $_) -Algorithm SHA256).Hash.ToLowerInvariant() } }) 'fork-source-files'
    Push-Location $source
    try {
        foreach ($arch in @('x86', 'x64')) {
            $build = "$OutputDirectory/build-$arch"
            $install = "$OutputDirectory/install-$arch"
            $installed = "$OutputDirectory/dependencies-$arch"
            $configure = @('--preset', "vs2022-windows-$arch", '-B', $build, '-DBUILD_TESTING=OFF', "-DCMAKE_INSTALL_PREFIX=$install", "-DVCPKG_INSTALLED_DIR=$installed")
            Write-Json @{ configure = $configure; build = @('--build', $build, '--config', 'Release', '--target', 'INSTALL') } "commands-$arch"
            try {
                Invoke-Checked cmake $configure
                Invoke-Checked cmake @('--build', $build, '--config', 'Release', '--target', 'INSTALL')
                foreach ($name in @("usvfs_$arch.dll", "usvfs_proxy_$arch.exe")) {
                    if (-not (Test-Path "$install/bin/$name")) { throw "Missing $arch output: $name" }
                }
                $state.architectures += $arch
            } finally {
                Write-Json @(Get-Inventory $install) "native-files-$arch"
                Write-Json @(Get-Inventory $installed) "installed-files-$arch"
                # Keep actual status/SBOM/compiler/cache text in the evidence, not just names.
                $textFiles = @()
                if (Test-Path $installed) {
                    $textFiles += Get-ChildItem $installed -File -Recurse | Where-Object { $_.Name -eq 'status' -or $_.Name -like '*.spdx.json' -or $_.Name -eq 'copyright' -or $_.Name -like '*.list' }
                }
                if (Test-Path $build) {
                    $textFiles += Get-ChildItem $build -File -Recurse | Where-Object { $_.Name -eq 'CMakeCache.txt' -or $_.Name -eq 'CMakeCXXCompiler.cmake' -or $_.Name -eq 'CMakeCCompiler.cmake' }
                }
                Write-Json @($textFiles | ForEach-Object { @{ path = [IO.Path]::GetRelativePath($OutputDirectory, $_.FullName); content = Get-Content $_.FullName -Raw } }) "resolved-evidence-$arch"
            }
        }
    } finally { Pop-Location }
    $state.windowsBuild = 'succeeded'
} catch {
    $state.windowsBuild = 'failed'
    $state.error = $_.Exception.Message
    throw
} finally {
    Write-Json $state 'stage-status'
    # These hashes describe observed caches, NOT verified corresponding source.
    foreach ($entry in @(
        @{ name = 'downloads'; root = "$OutputDirectory/downloads" },
        @{ name = 'registry-cache'; root = "$OutputDirectory/registries" },
        @{ name = 'buildtrees'; root = "$VcpkgRoot/buildtrees" },
        @{ name = 'packages'; root = "$VcpkgRoot/packages" },
        @{ name = 'tool-ports'; root = "$VcpkgRoot/ports" },
        @{ name = 'tool-triplets'; root = "$VcpkgRoot/triplets" }
    )) {
        Write-Json @(Get-Inventory $entry.root) $entry.name
    }
}
Write-Output "Local staging completed. PUBLICATION REMAINS BLOCKED. Reports: $reports"
