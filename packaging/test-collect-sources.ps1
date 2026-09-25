# Cross-platform collector unit checks; no builds, downloads, or injection tests.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile("$PSScriptRoot/collect-sources.ps1", [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors -join "`n") }
# Load only pure helper definitions, never execute the collection entry point.
foreach ($name in @('Recipe-Matches', 'Copy-Source', 'Verify')) {
    $definition = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $false)
    Invoke-Expression $definition.Extent.Text
}
$fixture = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
New-Item -ItemType Directory "$fixture/patches" -Force | Out-Null
try {
    Set-Content "$fixture/portfile.cmake" 'recipe'
    Set-Content "$fixture/patches/fix.patch" 'patch'
    $files = @('portfile.cmake', 'patches/fix.patch') | ForEach-Object {
        [pscustomobject]@{ fileName = "./$_"; checksums = @([pscustomobject]@{ algorithm = 'SHA256'; checksumValue = (Get-FileHash "$fixture/$_").Hash }) }
    }
    if (-not (Recipe-Matches $fixture $files)) { throw 'Valid nested recipe rejected' }
    Set-Content "$fixture/patches/fix.patch" 'wrong patch'
    if (Recipe-Matches $fixture $files) { throw 'Modified patch accepted' }
    Remove-Item "$fixture/patches/fix.patch"
    if (Recipe-Matches $fixture $files) { throw 'Missing patch accepted' }
    Set-Content "$fixture/patches/fix.patch" 'patch'
    $files[0].fileName = '../escape'
    $rejected = $false
    try { Recipe-Matches $fixture $files } catch { $rejected = $true }
    if (-not $rejected) { throw 'Unsafe path accepted' }
    Set-Content "$fixture/tool.exe" 'not an actual executable'
    $rejected = $false
    try { Copy-Source "$fixture/tool.exe" "$fixture/output/tool.exe" } catch { $rejected = $true }
    if (-not $rejected) { throw 'Executable copy accepted' }
    $rejected = $false
    try { Verify "$fixture/portfile.cmake" SHA256 ('0' * 64) } catch { $rejected = $true }
    if (-not $rejected) { throw 'Bad source hash accepted' }
    Write-Host 'Collector helper checks passed (not a Windows build or completeness test).'
} finally { Remove-Item $fixture -Recurse -Force }
