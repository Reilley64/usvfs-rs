# External prerequisite copying only; no native build or network access.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile("$PSScriptRoot/rebuild-sources.ps1", [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors -join "`n") }
foreach ($name in @('Verify', 'Copy-ExternalToolArchives')) {
    $definition = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $false)
    if (-not $definition) { throw "Missing helper: $name" }
    Invoke-Expression $definition.Extent.Text
}
$fixture = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
New-Item -ItemType Directory "$fixture/original", "$fixture/downloads", "$fixture/mirror" -Force | Out-Null
try {
    $names = @('msys2-mingw-w64-x86_64-pkgconf-1~2.4.3-1-any.pkg.tar.zst', 'msys2-msys2-runtime-3.6.2-2-x86_64.pkg.tar.zst', 'ninja-win-1.13.1.zip', 'python-3.12.7-embed-amd64.zip')
    $records = @(foreach ($name in $names) {
        Set-Content "$fixture/original/$name" "fixture for $name"
        [pscustomobject]@{ path = $name; sha256 = (Get-FileHash "$fixture/original/$name" -Algorithm SHA256).Hash }
    })
    Set-Content "$fixture/original/library-source.tar.gz" 'must come from candidate instead'
    $copied = @(Copy-ExternalToolArchives "$fixture/original" "$fixture/downloads" "$fixture/mirror" $records)
    if ($copied.Count -ne 4 -or @(Get-ChildItem "$fixture/downloads" -File).Count -ne 4 -or @(Get-ChildItem "$fixture/mirror" -File).Count -ne 4) { throw 'Wrong external tool set' }
    foreach ($entry in $copied) {
        Verify "$fixture/downloads/$($entry.path)" SHA256 $entry.sha256
        Verify "$fixture/mirror/$($entry.sha512)" SHA512 $entry.sha512
        if ($entry.redistribution -ne 'excluded') { throw 'External archive redistribution not excluded' }
    }
    if (Test-Path "$fixture/downloads/library-source.tar.gz") { throw 'Library source reused from original stage' }
    Set-Content "$fixture/original/$($names[0])" 'corrupt tool archive'
    $rejected = $false
    try { Copy-ExternalToolArchives "$fixture/original" "$fixture/downloads" "$fixture/mirror" $records } catch { $rejected = $true }
    if (-not $rejected) { throw 'Corrupt external tool accepted' }
    $rejected = $false
    try { Copy-ExternalToolArchives "$fixture/original" "$fixture/downloads" "$fixture/mirror" @() } catch { $rejected = $true }
    if (-not $rejected) { throw 'Missing archive provenance accepted' }
    Write-Host 'External tool archive checks passed; no Windows rebuild performed.'
} finally { Remove-Item $fixture -Recurse -Force }
