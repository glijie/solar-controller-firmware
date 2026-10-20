@echo off
REM Create a clean release zip containing only the sketch folder.
REM Usage:
REM   tools\make_release_zip.bat
REM or
REM   tools\make_release_zip.bat "C:\path\to\SketchFolder"

setlocal enabledelayedexpansion

set SKETCH_DIR=%~1
if "%SKETCH_DIR%"=="" (
  REM assume this file is in <sketch>\tools
  set SKETCH_DIR=%~dp0..
)

for %%I in ("%SKETCH_DIR%") do set NAME=%%~nxI
set OUT=%NAME%.zip

if not exist "%SKETCH_DIR%\%NAME%.ino" (
  echo ERROR: expected "%SKETCH_DIR%\%NAME%.ino"
  exit /b 1
)
if not exist "%SKETCH_DIR%\src" (
  echo ERROR: expected "%SKETCH_DIR%\src" folder
  exit /b 1
)

REM Use PowerShell Compress-Archive (available on modern Windows)
powershell -NoProfile -Command "^$
  $src = Resolve-Path '%SKETCH_DIR%';
  $name = '%NAME%';
  $out = Join-Path (Get-Location) '%OUT%';
  if (Test-Path $out) { Remove-Item -Force $out };
  $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString());
  New-Item -ItemType Directory -Path $tmp | Out-Null;
  $dst = Join-Path $tmp $name;
  Copy-Item -Recurse -Force -Path $src -Destination $dst;
  Get-ChildItem -Recurse -Force $dst | Where-Object {
    $_.Name -match '\\.(bin|elf|map|log)$'
  } | Remove-Item -Force -ErrorAction SilentlyContinue;
  Compress-Archive -Path $dst -DestinationPath $out -Force;
  Remove-Item -Recurse -Force $tmp;"

echo OK: created %OUT%
