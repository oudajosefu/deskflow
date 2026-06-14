# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
#
# Static bundle-completeness check for the Windows package.
#
# Walks every bundled GStreamer plugin and GStreamer/GLib DLL and asserts that each of their
# non-system DLL dependencies is present inside the package. Catches the class of bug where a
# plugin's transitive dependency (orc-0.4-0.dll, gio-2.0-0.dll, z-1.dll, ...) is not bundled,
# which makes the plugin fail to load at runtime with "the specified module could not be found".
#
# This deliberately does NOT add the GStreamer SDK to PATH, so a dependency that happens to be
# installed on the build machine cannot mask one that is missing from the package.
#
# Usage: check-bundled-deps.ps1 -PackageDir <extracted package root>

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)] [string] $PackageDir
)
$ErrorActionPreference = "Stop"

# Locate the package root (the directory that contains deskflow-core.exe).
$exe = Get-ChildItem -Path $PackageDir -Recurse -Filter deskflow-core.exe -File -ErrorAction SilentlyContinue |
  Select-Object -First 1
if (-not $exe) { Write-Error "deskflow-core.exe not found under $PackageDir"; exit 2 }
$root = $exe.Directory.FullName
Write-Host "Package root: $root"

# Locate dumpbin (ships with the MSVC toolchain).
$dumpbin = (Get-Command dumpbin.exe -ErrorAction SilentlyContinue).Source
if (-not $dumpbin) {
  $dumpbin = Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio", "C:\Program Files (x86)\Microsoft Visual Studio" `
      -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
}
if (-not $dumpbin) { Write-Error "dumpbin.exe not found (need the MSVC dev environment)"; exit 2 }

$sys32 = Join-Path $env:WINDIR "System32"

# Every DLL we ship (root + plugin dir), as a case-insensitive lookup set.
$present = @{}
Get-ChildItem -Path $root -Recurse -Filter *.dll -File | ForEach-Object { $present[$_.Name.ToLower()] = $true }

function Get-Deps([string] $file) {
  & $dumpbin /dependents $file 2>$null |
    Select-String -Pattern '^\s+\S+\.dll' | ForEach-Object { $_.Line.Trim() }
}

# Scan the bundled plugins plus the GStreamer/GLib DLLs that sit next to the executables.
$toScan = @()
$pluginDir = Join-Path $root "gstreamer-1.0"
if (Test-Path $pluginDir) { $toScan += (Get-ChildItem "$pluginDir\*.dll" -File).FullName }
$toScan += (Get-ChildItem "$root\*.dll" -File |
  Where-Object { $_.Name -match '^(gst|glib|gobject|gio|gmodule|orc|opus|ffi|intl|pcre)' }).FullName

$missing = @{}
foreach ($f in $toScan) {
  foreach ($dep in (Get-Deps $f)) {
    $dl = $dep.ToLower()
    if ($present.ContainsKey($dl)) { continue }
    if ($dl -like 'api-ms-*' -or $dl -like 'ext-ms-*') { continue }
    if (Test-Path (Join-Path $sys32 $dep)) { continue }   # genuine OS DLL
    if (-not $missing.ContainsKey($dl)) { $missing[$dl] = @() }
    $missing[$dl] += [System.IO.Path]::GetFileName($f)
  }
}

if ($missing.Count -gt 0) {
  Write-Host "::error::Bundled GStreamer components are missing dependencies from the package:"
  foreach ($k in ($missing.Keys | Sort-Object)) {
    Write-Host ("  {0}  <- needed by: {1}" -f $k, (($missing[$k] | Sort-Object -Unique) -join ', '))
  }
  exit 1
}
Write-Host "OK: every bundled GStreamer plugin/DLL dependency is present in the package ($($present.Count) DLLs)."
