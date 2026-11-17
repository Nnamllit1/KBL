[CmdletBinding()]
param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskCmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($taskCmakeCommand) {
    $taskCmake = $taskCmakeCommand.Source
} else {
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $taskCmake = Join-Path $taskVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
& $taskCmake -S $taskRoot -B (Join-Path $taskRoot 'build') -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }
& $taskCmake --build (Join-Path $taskRoot 'build') --config $Configuration --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$taskCtest = Join-Path (Split-Path -Parent $taskCmake) 'ctest.exe'
& $taskCtest --test-dir (Join-Path $taskRoot 'build') -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
