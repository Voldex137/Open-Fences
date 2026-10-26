# Builds Open Fences in Release and produces both distributions in dist\:
#   OpenFences-<ver>-portable.zip   exe + portable.flag (settings stay next to the exe, nothing is installed)
#   OpenFences-<ver>-setup.exe      Inno Setup installer (needs Inno Setup 6: winget install JRSoftware.InnoSetup)
# Usage:  powershell -File tools\package.ps1 [-SkipBuild]
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'

# Version comes from project(... VERSION x.y.z) in CMakeLists.txt.
$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(openFences VERSION ([\d.]+)').Matches[0].Groups[1].Value

$cmake = 'cmake'
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { $cmake = 'C:\Program Files\CMake\bin\cmake.exe' }

if (-not $SkipBuild) {
    & $cmake -S $root -B $build
    if ($LASTEXITCODE) { throw 'configure failed' }
    & $cmake --build $build --config Release --target openfences -- /m
    if ($LASTEXITCODE) { throw 'build failed' }
}
$exe = Join-Path $build 'Release\openfences.exe'
if (-not (Test-Path $exe)) { throw "missing $exe" }

New-Item -ItemType Directory -Force $dist | Out-Null

# --- portable zip ---
$stage = Join-Path $dist 'portable'
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe (Join-Path $stage 'openfences.exe')
Copy-Item (Join-Path $root 'LICENSE'), (Join-Path $root 'README.md'), (Join-Path $root 'README.ru.md'), (Join-Path $root 'README.zh-CN.md') $stage
Set-Content (Join-Path $stage 'portable.flag') 'Delete this file to store settings in %APPDATA%\Open Fences instead.' -Encoding ASCII
$zip = Join-Path $dist "OpenFences-$version-portable.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Remove-Item $stage -Recurse -Force
Write-Host "portable: $zip"

# --- installer ---
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe', 'C:\Program Files\Inno Setup 6\ISCC.exe') |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { Write-Warning 'Inno Setup 6 not found - installer skipped (winget install JRSoftware.InnoSetup)'; return }
& $iscc "/DAppVersion=$version" "/DExePath=$exe" "/DOutDir=$dist" (Join-Path $root 'installer\OpenFences.iss')
if ($LASTEXITCODE) { throw 'installer build failed' }
Write-Host "installer: $(Join-Path $dist "OpenFences-$version-setup.exe")"
