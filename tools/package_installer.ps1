<#
.SYNOPSIS
  Packages a portable, installable bundle of the Zhuyin IME so it can be
  copied to another Windows PC and installed without Visual Studio, CMake,
  or any other dev tooling present.

.DESCRIPTION
  Collects the already-built zhuyin_ime.dll (statically linked against the
  MSVC runtime, so no separate Visual C++ Redistributable install is
  needed), the dictionary data file, and copies of register.bat /
  unregister.bat (pointed at the DLL sitting next to them) into a flat
  "dist\ZhuyinIME" folder, then zips that folder up as ZhuyinIME.zip in the
  repository root.

  Run this AFTER building the Release configuration, e.g.:
    cmake --build build --config Release

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\package_installer.ps1
#>

param(
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$DllPath = Join-Path $RepoRoot "build\windows-ime\$Configuration\zhuyin_ime.dll"
$DictPath = Join-Path $RepoRoot "data\dictionary.tsv"
$RegisterScript = Join-Path $RepoRoot "installer\register.bat"
$UnregisterScript = Join-Path $RepoRoot "installer\unregister.bat"

if (-not (Test-Path $DllPath)) {
  throw "Built DLL not found at '$DllPath'. Run 'cmake --build build --config $Configuration' first."
}
if (-not (Test-Path $DictPath)) {
  throw "Dictionary file not found at '$DictPath'."
}

$DistRoot = Join-Path $RepoRoot "dist"
$PackageDir = Join-Path $DistRoot "ZhuyinIME"
if (Test-Path $PackageDir) {
  Remove-Item $PackageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $PackageDir -Force | Out-Null

Copy-Item $DllPath (Join-Path $PackageDir "zhuyin_ime.dll") -Force
Copy-Item $DictPath (Join-Path $PackageDir "dictionary.tsv") -Force
Copy-Item $RegisterScript (Join-Path $PackageDir "register.bat") -Force
Copy-Item $UnregisterScript (Join-Path $PackageDir "unregister.bat") -Force

$ReadmeContent = @"
Zhuyin IME - portable install package
======================================

1. Copy this whole "ZhuyinIME" folder anywhere on the target PC (e.g.
   C:\Program Files\ZhuyinIME, or even your Desktop).
2. Double-click register.bat and approve the Administrator prompt.
   (Windows may show a SmartScreen warning because the DLL isn't code
   signed -- choose "More info" -> "Run anyway" if so.)
3. Open Windows Settings -> Time & language -> Language & region, add or
   select a Chinese (Traditional, Taiwan) keyboard, then pick "Zhuyin IME"
   (or whatever name was registered) as the input method.
4. To remove it later, run unregister.bat (also as Administrator) before
   deleting the folder.

Do not move zhuyin_ime.dll and dictionary.tsv to different folders after
installing -- they must stay next to each other, and the registered path
must still exist, or typing will silently fall back to raw Bopomofo
symbols instead of decoding Chinese characters.
"@
Set-Content -Path (Join-Path $PackageDir "README.txt") -Value $ReadmeContent -Encoding UTF8

$ZipPath = Join-Path $RepoRoot "ZhuyinIME.zip"
if (Test-Path $ZipPath) {
  Remove-Item $ZipPath -Force
}
Compress-Archive -Path "$PackageDir\*" -DestinationPath $ZipPath -Force

Write-Host "Packaged installer created at: $ZipPath"
Write-Host "Copy this zip to the other PC, extract it, then run register.bat as Administrator."
