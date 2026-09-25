# Local packaging only. No release or tag creation.
param(
    [Parameter(Mandatory)][string]$StageDirectory,
    [Parameter(Mandatory)][string]$RebuildDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Read-Json([string]$Path) { Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Verify([string]$Path, [string]$Expected) {
    if ($Expected -cnotmatch '^[0-9a-fA-F]{64}$' -or (Hash $Path) -ne $Expected) { throw "SHA256 mismatch: $Path" }
}
function Both($Architectures) {
    if (@($Architectures).Count -ne 2 -or (Compare-Object @('x86', 'x64') @($Architectures))) { throw 'Both architectures required' }
}
$approval = Read-Json "$PSScriptRoot/source-approval.json"
$tag = 'usvfs-0.5.7.2-rs.1'
$upstream = '57f1ea5e6ad13f7435a7af184748e6c1312c5637'
if ($approval.reviewStatus -cne 'approved' -or $approval.noticesReviewStatus -cne 'approved') { throw 'Root source and notices approval pending; publication blocked' }
if ($approval.tag -cne $tag -or $approval.upstreamRevision -cne $upstream -or $approval.reviewedRebuildRunUrl -notmatch '^https://github.com/Reilley64/usvfs-rs/actions/runs/[0-9]+$') { throw 'Invalid frozen release approval' }
if (Test-Path $OutputDirectory) { throw 'Use a new output directory' }
$reports = "$StageDirectory/reports"
$build = Read-Json "$reports/stage-status.json"
$collection = Read-Json "$reports/source-collection-status.json"
$rebuild = Read-Json "$RebuildDirectory/reports/source-rebuild-status.json"
$inputs = Read-Json "$RebuildDirectory/reports/rebuild-inputs.json"
$provenance = Read-Json "$reports/provenance.json"
if ($build.windowsBuild -ne 'succeeded' -or $collection.collection -ne 'verified-inputs-collected' -or $rebuild.librarySourceRebuild -ne 'succeeded-with-origin-blocking') { throw 'Build, collection and rebuild must succeed' }
Both $build.architectures
Both $rebuild.architectures
if ($inputs.archiveSha256 -ne $collection.archiveSha256) { throw 'Candidate SHA differs from the archive rebuilt in this run' }
$head = & git -C "$PSScriptRoot/.." rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify packaging revision' }
if ($provenance.upstreamRevision -cne $upstream -or $provenance.forkRevision -cne $head -or $inputs.originalProvenance.forkRevision -cne $provenance.forkRevision) { throw 'Source provenance mismatch' }
$archive = "$StageDirectory/source-candidate-UNVERIFIED.tar.gz"
Verify $archive $collection.archiveSha256
# Inspect the archive itself, not a potentially changed sibling candidate directory.
$temporary = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString())
New-Item -ItemType Directory $temporary | Out-Null
try {
    & tar -xzf $archive -C $temporary
    if ($LASTEXITCODE -ne 0) { throw 'Source extraction failed' }
    $candidate = "$temporary/source-candidate"
    $manifest = @(Read-Json "$candidate/manifest.json")
    if (@($manifest.path | Sort-Object -Unique).Count -ne $manifest.Count -or @(Get-ChildItem $candidate -Recurse -File -Force).Count -ne $manifest.Count + 1) { throw 'Invalid candidate inventory' }
    foreach ($entry in $manifest) {
        if ($entry.path -match '(^|[/\\])\.\.([/\\]|$)|^[/\\]|^[A-Za-z]:' -or $entry.path -eq 'manifest.json') { throw 'Unsafe manifest path' }
        Verify "$candidate/$($entry.path)" $entry.sha256
    }
    foreach ($selection in @(@{ root = 'overlay-ports'; files = @($approval.recipes); count = 170 }, @{ root = 'assets'; files = @($approval.assets); count = 66 })) {
        $actual = @(Get-ChildItem "$candidate/$($selection.root)" -Recurse -File -Force | ForEach-Object { [IO.Path]::GetRelativePath($candidate, $_.FullName).Replace('\', '/') })
        if ($selection.files.Count -ne $selection.count -or @($selection.files.path | Sort-Object -Unique).Count -ne $selection.count -or (Compare-Object @($selection.files.path) $actual)) { throw 'Frozen source selection changed' }
        foreach ($file in $selection.files) { Verify "$candidate/$($file.path)" $file.sha256 }
    }
    # Evidence in source and stage must be byte-identical, including native hashes.
    foreach ($name in @('provenance', 'stage-status', 'native-files-x86', 'native-files-x64')) {
        Verify "$reports/$name.json" (Hash "$candidate/evidence/$name.json")
    }
    $bundle = "$OutputDirectory/native"
    New-Item -ItemType Directory "$bundle/bin" -Force | Out-Null
    $hashes = [ordered]@{}
    foreach ($arch in @('x86', 'x64')) {
        $inventory = @(Read-Json "$reports/native-files-$arch.json")
        foreach ($name in @("usvfs_$arch.dll", "usvfs_proxy_$arch.exe")) {
            $entry = @($inventory | Where-Object path -eq "bin/$name")
            if ($entry.Count -ne 1) { throw "Missing unique native evidence: $name" }
            $native = "$StageDirectory/install-$arch/bin/$name"
            Verify $native $entry[0].sha256
            Copy-Item -LiteralPath $native -Destination "$bundle/bin/$name"
            $hashes[$name] = Hash "$bundle/bin/$name"
        }
    }
    [IO.File]::WriteAllText([IO.Path]::GetFullPath("$bundle/bin/source-revision.txt"), $upstream)
    @{ source = $upstream; configuration = 'Release'; artifacts = $hashes } | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 "$bundle/bin/artifacts.json"
    # Only reviewed notice text is copied, never installed dependency binaries/tools.
    New-Item -ItemType Directory "$bundle/notices" | Out-Null
    foreach ($file in Get-ChildItem "$candidate/notices" -Recurse -File) {
        if ($file.Name -notin @('copyright', 'fmt.license.rst')) { throw 'Unreviewed notice file' }
        $relative = [IO.Path]::GetRelativePath("$candidate/notices", $file.FullName)
        $destination = Join-Path "$bundle/notices" $relative
        New-Item -ItemType Directory (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
    $forkFiles = @(Read-Json "$candidate/evidence/fork-source-files.json")
    foreach ($relative in @('LICENSE') + @(Get-ChildItem "$PSScriptRoot/../licenses" -File | ForEach-Object { "licenses/$($_.Name)" })) {
        $record = @($forkFiles | Where-Object path -eq $relative)
        if ($record.Count -ne 1) { throw "Missing source license evidence: $relative" }
        Verify "$PSScriptRoot/../$relative" $record[0].sha256
        $destination = Join-Path $bundle $relative
        New-Item -ItemType Directory (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath "$PSScriptRoot/../$relative" -Destination $destination
    }
    $sourceName = "$tag-corresponding-source.tar.gz"
    $nativeName = "$tag-native.zip"
    $sourceUrl = "https://github.com/Reilley64/usvfs-rs/releases/download/$tag/$sourceName"
    @"
Native Release $tag
Fork revision: $($provenance.forkRevision)
Upstream/sourceRevision: $upstream
Corresponding source: $sourceUrl
Source SHA256: $($collection.archiveSha256)
Requires Windows 11 and Microsoft Visual C++ 2015-2022 Redistributable BOTH x86 and x64, installed separately.
Redistributables and build tools are not included. See notices/ and LICENSE.
Source archive: source-candidate/manifest.json inventories every source input.
See source-candidate/README.md and checkouts/fork.tar.gz packaging/rebuild-sources.ps1
for source reconstruction and external Windows/VS2022 tool requirements.
The historical candidate README retains its pre-approval warning; this release's
root approval and rebuild evidence are in release-evidence/.
Rust bindings remain the usvfs-sys 0.0.0 Cargo git source package; no Rust libraries included.
"@ | Set-Content -Encoding utf8 "$bundle/README.txt"
    New-Item -ItemType Directory "$bundle/release-evidence" | Out-Null
    Copy-Item "$PSScriptRoot/source-approval.json" "$bundle/release-evidence/"
    Copy-Item "$RebuildDirectory/reports/*.json" "$bundle/release-evidence/"
    Copy-Item $archive "$OutputDirectory/$sourceName"
    Compress-Archive -Path "$bundle/*" -DestinationPath "$OutputDirectory/$nativeName"
    @{ tag = $tag; forkRevision = $provenance.forkRevision; upstreamRevision = $upstream; sourceUrl = $sourceUrl; sourceArchive = $sourceName; sourceSha256 = (Hash "$OutputDirectory/$sourceName"); nativeArchive = $nativeName; nativeSha256 = (Hash "$OutputDirectory/$nativeName"); artifacts = $hashes; publication = 'disabled' } | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 "$OutputDirectory/release.json"
} finally { Remove-Item $temporary -Recurse -Force }
