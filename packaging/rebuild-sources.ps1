# Bounded library-source rebuild. External build tools are NOT source-candidate contents.
param(
    [Parameter(Mandatory)][string]$StageDirectory,
    [Parameter(Mandatory)][string]$VcpkgRoot,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'Requires the original Windows runner and provisioned tools' }
$StageDirectory = (Resolve-Path $StageDirectory).Path
$VcpkgRoot = (Resolve-Path $VcpkgRoot).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path $OutputDirectory) { throw 'Use a fresh rebuild directory' }
$reports = "$OutputDirectory/reports"
New-Item -ItemType Directory $reports -Force | Out-Null
function Read-Json([string]$Path) { Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
function Write-Json($Value, [string]$Name) {
    $Value | ConvertTo-Json -Depth 30 | Set-Content -Encoding utf8 "$reports/$Name.json"
}
function Run([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed ($LASTEXITCODE)" }
}
function Verify([string]$Path, [string]$Algorithm, [string]$Expected) {
    if ((Get-FileHash -LiteralPath $Path -Algorithm $Algorithm).Hash -ne $Expected) { throw "Hash mismatch: $Path" }
}
function Inventory([string]$Root) {
    @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
        @{ path = [IO.Path]::GetRelativePath($Root, $_.FullName).Replace('\', '/'); bytes = $_.Length; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
    })
}
function Extract([string]$Archive, [string]$Destination) {
    # Archives must first pass their independent hash; also reject escaping member paths.
    $members = @(Run tar @('-tf', $Archive))
    foreach ($member in $members) {
        if ($member -match '(^|[/\\])\.\.([/\\]|$)|^[/\\]|^[A-Za-z]:') { throw "Unsafe archive member: $member" }
    }
    New-Item -ItemType Directory $Destination -Force | Out-Null
    Run tar @('-xf', $Archive, '-C', $Destination)
    if (@(Get-ChildItem $Destination -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Archive links require review' }
}
function Package-Identity([string]$Text) {
    $identities = @()
    foreach ($paragraph in ($Text.Trim() -split '\r?\n\s*\r?\n')) {
        $fields = @{}
        foreach ($line in ($paragraph -split '\r?\n')) {
            if ($line -match '^([^ :]+):\s*(.*)$') { $fields[$Matches[1]] = $Matches[2] }
        }
        if (-not $fields.ContainsKey('Package')) { continue }
        if ($fields['Status'] -ne 'install ok installed') { throw 'Non-installed package in status evidence' }
        $identities += (@('Package', 'Architecture', 'Version', 'Port-Version', 'Feature') | ForEach-Object { "$_=$($fields[$_])" }) -join '|'
    }
    if ($identities.Count -eq 0) { throw 'Empty package identity evidence' }
    @($identities | Sort-Object)
}
$state = [ordered]@{ publication = 'blocked'; librarySourceRebuild = 'started'; architectures = @(); error = $null; byteReproduciblePE = 'NOT-CLAIMED' }
try {
    $collection = Read-Json "$StageDirectory/reports/source-collection-status.json"
    if ($collection.collection -ne 'verified-inputs-collected') { throw 'Collection did not succeed' }
    $archive = "$StageDirectory/source-candidate-UNVERIFIED.tar.gz"
    Verify $archive SHA256 $collection.archiveSha256
    Extract $archive "$OutputDirectory/input"
    $candidate = "$OutputDirectory/input/source-candidate"
    $manifest = @(Read-Json "$candidate/manifest.json")
    if (@($manifest.path | Sort-Object -Unique).Count -ne $manifest.Count) { throw 'Duplicate manifest paths' }
    if (@(Get-ChildItem $candidate -Recurse -File -Force).Count -ne ($manifest.Count + 1)) { throw 'Undeclared candidate files' }
    foreach ($entry in $manifest) {
        if ($entry.path -match '(^|[/\\])\.\.([/\\]|$)|^[/\\]|^[A-Za-z]:' -or $entry.path -eq 'manifest.json') { throw 'Unsafe manifest path' }
        $path = Join-Path $candidate $entry.path
        if ((Get-Item -LiteralPath $path).Length -ne $entry.bytes) { throw "Size mismatch: $path" }
        Verify $path SHA256 $entry.sha256
    }
    $original = Read-Json "$candidate/evidence/stage-status.json"
    if ($original.windowsBuild -ne 'succeeded') { throw 'Original build failed' }
    $mapping = @(Read-Json "$candidate/source-map.json")
    if (@(Get-ChildItem "$candidate/overlay-ports" -Directory).Count -ne 69 -or @($mapping.port | Sort-Object -Unique).Count -ne 69) { throw 'Expected 69 overlay recipes' }
    $assets = @(Get-ChildItem "$candidate/assets" -File)
    if ($assets.Count -ne 66) { throw 'Expected 65 source archives and Boost license' }
    $fork = "$OutputDirectory/fork"
    $tool = "$OutputDirectory/vcpkg"
    Extract "$candidate/checkouts/fork.tar.gz" $fork
    Extract "$candidate/checkouts/vcpkg.tar.gz" $tool
    # No original registry cache, buildtrees, packages, installed tree, or source downloads are reused.
    $downloads = "$OutputDirectory/downloads"
    $mirror = "$OutputDirectory/asset-mirror"
    $registry = "$OutputDirectory/registries"
    $manifestRoot = "$OutputDirectory/manifest"
    New-Item -ItemType Directory $downloads, $mirror, $registry, $manifestRoot | Out-Null
    Copy-Item "$VcpkgRoot/vcpkg.exe" "$tool/vcpkg.exe"
    Copy-Item "$StageDirectory/downloads/tools" "$downloads/tools" -Recurse
    Verify "$tool/vcpkg.exe" SHA256 (Get-FileHash "$VcpkgRoot/vcpkg.exe" -Algorithm SHA256).Hash
    $external = @(Inventory "$StageDirectory/downloads/tools")
    foreach ($entry in $external) { Verify "$downloads/tools/$($entry.path)" SHA256 $entry.sha256 }
    Write-Json @{ vcpkgExecutable = (Get-FileHash "$tool/vcpkg.exe" -Algorithm SHA256).Hash; suppliedTools = $external; source = "$StageDirectory/downloads/tools"; redistribution = 'excluded' } 'external-tools'
    foreach ($asset in $assets) {
        Copy-Item -LiteralPath $asset.FullName -Destination "$downloads/$($asset.Name)"
        $sha512 = (Get-FileHash $asset.FullName -Algorithm SHA512).Hash.ToLowerInvariant()
        Copy-Item -LiteralPath $asset.FullName -Destination "$mirror/$sha512"
    }
    foreach ($resource in @($mapping.resources)) {
        Verify "$candidate/$($resource.archive)" SHA512 $resource.sha512
    }
    # Keep source manifest and original registry baseline untouched. Only this separate
    # resolution adapter disables registries; every recipe is the verified resolved overlay.
    Copy-Item "$fork/vcpkg.json" "$manifestRoot/vcpkg.json"
    '{"default-registry":null}' | Set-Content -Encoding utf8 "$manifestRoot/vcpkg-configuration.json"
    $env:VCPKG_ROOT = $tool
    $env:VCPKG_DOWNLOADS = $downloads
    $env:VCPKG_BINARY_SOURCES = 'clear'
    $env:VCPKG_DEFAULT_BINARY_CACHE = "$OutputDirectory/binary-cache"
    $env:X_VCPKG_REGISTRIES_CACHE = $registry
    $env:VCPKG_DISABLE_METRICS = '1'
    $env:PYTHONDONTWRITEBYTECODE = '1'
    $env:VCPKG_OVERLAY_PORTS = "$candidate/overlay-ports"
    $env:VCPKG_OVERLAY_TRIPLETS = ''
    $mirrorUri = ([Uri]([IO.Path]::GetFullPath($mirror) + [IO.Path]::DirectorySeparatorChar)).AbsoluteUri
    $env:X_VCPKG_ASSET_SOURCES = "clear;x-azurl,$mirrorUri,,read;x-block-origin"
    New-Item -ItemType Directory $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
    Write-Json @{ archiveSha256 = $collection.archiveSha256; assetSources = $env:X_VCPKG_ASSET_SOURCES; registryConfiguration = @{ 'default-registry' = $null }; originalProvenance = (Read-Json "$candidate/evidence/provenance.json"); cmake = @(Run cmake @('--version')); vcpkg = @(Run "$tool/vcpkg.exe" @('version')) } 'rebuild-inputs'
    Push-Location $fork
    try {
        foreach ($arch in @('x86', 'x64')) {
            $build = "$OutputDirectory/build-$arch"
            $installed = "$OutputDirectory/dependencies-$arch"
            $install = "$OutputDirectory/install-$arch"
            $configure = @('--preset', "vs2022-windows-$arch", '-B', $build, '-DBUILD_TESTING=OFF', "-DCMAKE_INSTALL_PREFIX=$install", "-DVCPKG_INSTALLED_DIR=$installed", "-DVCPKG_MANIFEST_DIR=$manifestRoot", "-DVCPKG_OVERLAY_PORTS=$candidate/overlay-ports", '-DVCPKG_OVERLAY_TRIPLETS=')
            Write-Json @{ configure = $configure; build = @('--build', $build, '--config', 'Release', '--target', 'INSTALL') } "commands-$arch"
            Run cmake $configure
            $evidence = @(Read-Json "$candidate/evidence/resolved-evidence-$arch.json")
            $status = @($evidence | Where-Object { $_.path.Replace('\', '/') -eq "dependencies-$arch/vcpkg/status" })
            if ($status.Count -ne 1) { throw "Missing unique original status for $arch" }
            $expected = @(Package-Identity $status[0].content)
            $actual = @(Package-Identity (Get-Content "$installed/vcpkg/status" -Raw))
            Write-Json @{ original = $expected; rebuilt = $actual } "package-identity-$arch"
            if (Compare-Object $expected $actual) { throw "Package/version/feature/host-target resolution changed for $arch" }
            if (@(Get-ChildItem $registry -Recurse -File -Force).Count) { throw 'Registry cache unexpectedly populated' }
            Run cmake @('--build', $build, '--config', 'Release', '--target', 'INSTALL')
            $outputs = foreach ($name in @("usvfs_$arch.dll", "usvfs_proxy_$arch.exe")) {
                $path = "$install/bin/$name"
                if (-not (Test-Path $path -PathType Leaf)) { throw "Missing output: $name" }
                @{ name = $name; bytes = (Get-Item $path).Length; sha256 = (Get-FileHash $path -Algorithm SHA256).Hash }
            }
            Write-Json @($outputs) "outputs-$arch"
            Copy-Item "$installed/vcpkg/status" "$reports/status-$arch.txt"
            $state.architectures += $arch
        }
    } finally { Pop-Location }
    $state.librarySourceRebuild = 'succeeded-with-origin-blocking'
} catch {
    $state.librarySourceRebuild = 'failed'
    $state.error = $_.Exception.Message
    throw
} finally {
    Write-Json $state 'source-rebuild-status'
}
Write-Output "Library-source rebuild completed. PUBLICATION REMAINS BLOCKED. Reports: $reports"
