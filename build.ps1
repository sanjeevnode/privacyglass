# cmake isn't on PATH on this machine; use the copy bundled with VS2022.
param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -latest -format value -property installationPath
if (-not $vs) { throw "VS2022 with the C++ desktop workload was not found." }

$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

& $cmake -S $PSScriptRoot -B "$PSScriptRoot\build" -G "Visual Studio 17 2022" -A x64
& $cmake --build "$PSScriptRoot\build" --config Release

$exe = "$PSScriptRoot\build\Release\PrivacyGlass.exe"
Write-Host "`nBuilt: $exe"

if (-not $SkipTests) {
    $report = "$PSScriptRoot\build\Release\selfcheck.txt"
    Remove-Item $report -ErrorAction SilentlyContinue
    $p = Start-Process $exe -ArgumentList '--selfcheck' -PassThru -Wait
    if (Test-Path $report) { Get-Content $report | Select-Object -First 40 }
    if ($p.ExitCode -ne 0) { throw "Privacy engine self-check FAILED (exit $($p.ExitCode))." }
    Write-Host "Self-check passed." -ForegroundColor Green
}
exit 0
