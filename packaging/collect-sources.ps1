# Source candidate only. Missing or mismatched evidence fails closed before archive creation.
param(
    [Parameter(Mandatory)][string]$VcpkgRoot,
    [Parameter(Mandatory)][string]$StageDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$source = Split-Path $PSScriptRoot -Parent
$VcpkgRoot = (Resolve-Path $VcpkgRoot).Path
$StageDirectory = (Resolve-Path $StageDirectory).Path
$reports = "$StageDirectory/reports"
$candidate = "$StageDirectory/source-candidate"
$archive = "$StageDirectory/source-candidate-UNVERIFIED.tar.gz"
if ((Test-Path $candidate) -or (Test-Path $archive)) { throw 'Source candidate already exists; use a fresh stage' }
function Read-Json([string]$Path) { Get-Content $Path -Raw | ConvertFrom-Json }
function Run([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed ($LASTEXITCODE)" }
}
function Copy-Source([string]$From, [string]$To) {
    $item = Get-Item -LiteralPath $From -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Source link requires review: $From" }
    if ($item.Extension -match '^\.(exe|dll|lib|obj|pdb|msi|nupkg|pyc|pyo)$') { throw "Unexpected binary in source selection: $From" }
    New-Item -ItemType Directory -Force (Split-Path $To -Parent) | Out-Null
    Copy-Item -LiteralPath $From -Destination $To
}
function Copy-Tree([string]$From, [string]$To) {
    if ((Get-Item -LiteralPath $From -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Source link requires review: $From" }
    foreach ($item in Get-ChildItem -LiteralPath $From -Recurse -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Source link requires review: $($item.FullName)" }
        if (-not $item.PSIsContainer) { Copy-Source $item.FullName (Join-Path $To ([IO.Path]::GetRelativePath($From, $item.FullName))) }
    }
}
function Verify([string]$Path, [string]$Algorithm, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing source input: $Path" }
    if ((Get-FileHash -LiteralPath $Path -Algorithm $Algorithm).Hash -ne $Expected) { throw "$Algorithm mismatch: $Path" }
}
function Recipe-Matches([string]$Root, $Files) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return $false }
    if (@($Files.fileName | Sort-Object -Unique).Count -ne $Files.Count) { throw "Duplicate recipe file declarations: $Root" }
    if (@(Get-ChildItem $Root -File -Recurse -Force).Count -ne $Files.Count) { return $false }
    foreach ($file in $Files) {
        $relative = $file.fileName -replace '^\./', ''
        if ($relative -match '(^|[/\\])\.\.([/\\]|$)' -or [IO.Path]::IsPathRooted($relative)) { throw "Unsafe recipe path: $relative" }
        $path = Join-Path $Root $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
        $hash = @($file.checksums | Where-Object algorithm -eq 'SHA256')
        if ($hash.Count -ne 1) { throw "Recipe lacks unique SHA256: $path" }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $hash[0].checksumValue) { return $false }
    }
    return $true
}
$state = [ordered]@{ publication = 'blocked'; completeness = 'UNVERIFIED'; collection = 'started'; error = $null }
try {
    $build = Read-Json "$reports/stage-status.json"
    if ($build.windowsBuild -ne 'succeeded' -or @($build.architectures).Count -ne 2) { throw 'Both clean native builds must succeed first' }
    $provenance = Read-Json "$reports/provenance.json"
    foreach ($repo in @(@{ path = $source; revision = $provenance.forkRevision }, @{ path = $VcpkgRoot; revision = $provenance.vcpkgToolRevision })) {
        $head = Run git @('-C', $repo.path, 'rev-parse', 'HEAD')
        if ($head -ne $repo.revision) { throw "Checkout changed: $($repo.path)" }
        Run git @('-C', $repo.path, 'diff', '--exit-code', 'HEAD')
    }
    New-Item -ItemType Directory "$candidate/checkouts" -Force | Out-Null
    Run git @('-C', $source, 'archive', '--format=tar.gz', "--output=$candidate/checkouts/fork.tar.gz", $provenance.forkRevision)
    Run git @('-C', $VcpkgRoot, 'archive', '--format=tar.gz', "--output=$candidate/checkouts/vcpkg.tar.gz", $provenance.vcpkgToolRevision)
    # vcpkg's SPDX recipe origin suffix is the resolved Git TREE, not a commit.
    # X_VCPKG_REGISTRIES_CACHE is deliberately isolated by stage-native.ps1.
    $recipeRoot = "$StageDirectory/registries/git-trees"
    $buildInventory = @(Read-Json "$reports/buildtrees.json")
    $downloads = @(Read-Json "$reports/downloads.json")
    # Only resource tarballs are eligible. Never traverse downloads/tools or collect helper archives.
    $assets = @(Get-ChildItem "$StageDirectory/downloads" -File | Where-Object { $_.Name -like '*.tar.gz' -or $_.Name -eq 'boost-1.89.0-LICENSE_1_0.txt' } | ForEach-Object {
        [pscustomobject]@{ path = $_.FullName; name = $_.Name; sha512 = (Get-FileHash $_.FullName -Algorithm SHA512).Hash }
    })
    $mapping = @()
    $seen = @{}
    foreach ($arch in @('x86', 'x64')) {
        $evidence = @(Read-Json "$reports/resolved-evidence-$arch.json")
        $documents = @($evidence | Where-Object { $_.path -like '*.spdx.json' })
        if ($documents.Count -ne 69) { throw "Expected reviewed 69 packages for $arch, got $($documents.Count); review changed resolution" }
        foreach ($document in $documents) {
            $spdx = $document.content | ConvertFrom-Json
            $port = @($spdx.packages | Where-Object SPDXID -eq 'SPDXRef-port')[0]
            $files = @($spdx.files | Where-Object { $_.SPDXID -like 'SPDXRef-port-file-*' })
            if ($files.Count -eq 0) { throw "No declared recipe files: $($port.name)" }
            if ($port.name -notmatch '^[a-z0-9-]+$') { throw 'Unexpected port name' }
            $key = $port.name
            $target = "$candidate/overlay-ports/$key"
            if ($seen.ContainsKey($key)) {
                if (-not (Recipe-Matches $target $files)) { throw "Recipe differs across architectures: $key" }
            } else {
                if ($port.downloadLocation -notmatch '^git\+https://github.com/[^@]+@([0-9a-f]{40})$') { throw "Unknown recipe origin: $($port.downloadLocation)" }
                $resolved = "$recipeRoot/$($Matches[1])"
                if (-not (Recipe-Matches $resolved $files)) { throw "Resolved recipe missing or SHA256 mismatch: $key ($($port.downloadLocation)), expected $resolved" }
                $actualFiles = @(Get-ChildItem $resolved -Recurse -File -Force)
                if ($actualFiles.Count -ne $files.Count) { throw "Undeclared recipe files: $resolved" }
                $matches = @($resolved)
                # Copy declared recipe files only; no unverified tool-port fallback or hidden cache output.
                foreach ($file in $files) {
                    $relative = $file.fileName -replace '^\./', ''
                    Copy-Source (Join-Path $matches[0] $relative) (Join-Path $target $relative)
                }
                $seen[$key] = $matches[0]
            }
            $resources = @()
            foreach ($resource in @($spdx.packages | Where-Object { $_.SPDXID -like 'SPDXRef-resource-*' })) {
                if ($resource.downloadLocation -eq 'https://raw.githubusercontent.com/boostorg/boost/refs/tags/boost-1.89.0/LICENSE_1_0.txt') {
                    $expectedName = 'boost-1.89.0-LICENSE_1_0.txt'
                } elseif ($resource.downloadLocation -match '^git\+https://github.com/([^@]+)@(.+)$') {
                    $resourceRepo = $Matches[1]; $resourceRef = $Matches[2]
                    $expectedName = ($resourceRepo -replace '/', '-') + '-' + ($resourceRef -replace '/', '-') + '.tar.gz'
                } else { throw "Unreviewed resource type: $($resource.downloadLocation)" }
                $hash = @($resource.checksums | Where-Object algorithm -eq 'SHA512')
                if ($hash.Count -ne 1) { throw "Resource lacks SHA512: $($resource.name)" }
                $asset = @($assets | Where-Object { $_.name -eq $expectedName -and $_.sha512 -eq $hash[0].checksumValue })
                if ($asset.Count -ne 1) { throw "Missing verified exact SPDX resource: $expectedName ($key)" }
                $observed = @($downloads | Where-Object path -eq $expectedName)
                if ($observed.Count -ne 1) { throw "Resource missing original inventory: $expectedName" }
                Verify $asset[0].path SHA256 $observed[0].sha256
                Copy-Source $asset[0].path "$candidate/assets/$expectedName"
                $resources += @{ spdxId = $resource.SPDXID; origin = $resource.downloadLocation; archive = "assets/$expectedName"; sha512 = $hash[0].checksumValue; sha256 = $observed[0].sha256 }
            }
            $preparedRoots = @()
            if ($resources.Count -gt 0) {
                # Exact port/src boundary excludes builds, installed packages, and downloaded tools.
                $roots = @(Get-ChildItem "$VcpkgRoot/buildtrees/$key/src" -Directory)
                if ($roots.Count -ne 1) { throw "Expected one reviewed prepared source root for $key, got $($roots.Count)" }
                $relative = "prepared/$key/src/$($roots[0].Name)"
                if (-not (Test-Path "$candidate/$relative")) {
                    $prefix = "$key/src/$($roots[0].Name)/"
                    $recorded = @($buildInventory | Where-Object { $_.path.StartsWith($prefix) })
                    $actual = @(Get-ChildItem $roots[0].FullName -File -Recurse -Force)
                    if ($recorded.Count -eq 0 -or $recorded.Count -ne $actual.Count) { throw "Prepared source inventory differs: $key" }
                    foreach ($entry in $recorded) { Verify "$VcpkgRoot/buildtrees/$($entry.path)" SHA256 $entry.sha256 }
                    Copy-Tree $roots[0].FullName "$candidate/$relative"
                }
                $preparedRoots += $relative
            }
            $mapping += @{ architecture = $arch; document = $document.path; package = $spdx.name; port = $key; recipeOrigin = $port.downloadLocation; resolvedRecipeDirectory = $seen[$key]; recipe = "overlay-ports/$key"; resources = $resources; prepared = $preparedRoots }
        }
        foreach ($notice in @($evidence | Where-Object { (Split-Path $_.path -Leaf) -eq 'copyright' })) {
            $relative = $notice.path.Replace('\', '/')
            Copy-Source (Join-Path $StageDirectory $relative) "$candidate/notices/$relative"
        }
    }
    if ($seen.Count -ne 69) { throw "Expected 69 distinct verified recipes, got $($seen.Count)" }
    if (@(Get-ChildItem "$candidate/assets" -File -Filter '*.tar.gz').Count -ne 65) { throw 'Expected 65 distinct verified resource archives' }
    if (-not (Test-Path "$candidate/assets/boost-1.89.0-LICENSE_1_0.txt")) { throw 'Missing SPDX-verified Boost license' }
    $fmt = @(Get-ChildItem "$candidate/prepared/spdlog" -Recurse -File -Filter fmt.license.rst)
    if ($fmt.Count -ne 1) { throw 'Missing bundled fmt license' }
    Copy-Source $fmt[0].FullName "$candidate/notices/fmt.license.rst"
    Copy-Tree $reports "$candidate/evidence"
    Copy-Source "$PSScriptRoot/SOURCE-CANDIDATE.md" "$candidate/README.md"
    $mapping | ConvertTo-Json -Depth 20 | Set-Content -Encoding utf8 "$candidate/source-map.json"
    $manifest = @(Get-ChildItem $candidate -File -Recurse | Sort-Object FullName | ForEach-Object {
        @{ path = [IO.Path]::GetRelativePath($candidate, $_.FullName).Replace('\', '/'); bytes = $_.Length; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 "$candidate/manifest.json"
    # This is the only uploadable archive. No partial candidate is uploaded on a failure.
    Run tar @('-czf', $archive, '-C', $StageDirectory, 'source-candidate')
    $state.collection = 'verified-inputs-collected'
    $state['archiveSha256'] = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
} catch {
    $state.collection = 'failed'; $state.error = $_.Exception.Message
    if (Test-Path $archive) { Remove-Item $archive }
    throw
} finally {
    $state | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 "$reports/source-collection-status.json"
}
