# cmake isn't on PATH on this machine; use the copy bundled with VS2022.
$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -latest -format value -property installationPath
if (-not $vs) { throw "VS2022 with the C++ desktop workload was not found." }

$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

& $cmake -S $PSScriptRoot -B "$PSScriptRoot\build" -G "Visual Studio 17 2022" -A x64
& $cmake --build "$PSScriptRoot\build" --config Release

Write-Host "`nBuilt: $PSScriptRoot\build\Release\WhatsAppPrivacy.exe"
