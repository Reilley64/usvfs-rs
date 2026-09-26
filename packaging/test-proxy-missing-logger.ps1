param([Parameter(Mandatory)][string]$StageDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$StageDirectory = [IO.Path]::GetFullPath($StageDirectory)
foreach ($arch in @('x86', 'x64')) {
    $directory = "$StageDirectory/install-$arch/bin"
    $executable = "$directory/usvfs_proxy_$arch.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing staged proxy: $executable" }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $executable
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $directory
    [void]$start.ArgumentList.Add('--instance')
    [void]$start.ArgumentList.Add("release-missing-logger-$arch")
    [void]$start.ArgumentList.Add('--pid')
    [void]$start.ArgumentList.Add('0')
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "Cannot start staged proxy: $arch" }
    if (-not $process.WaitForExit(5000)) {
        try { $process.Kill($true) } catch {}
        throw "Staged proxy blocked without shared logging: $arch"
    }
    if ($process.ExitCode -ne 1) { throw "Unexpected missing-logger exit for $arch`: $($process.ExitCode)" }
}
Write-Output 'Both staged proxies fail non-modally without shared logging.'
