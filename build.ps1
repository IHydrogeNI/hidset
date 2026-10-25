# Builds hidset.exe with llvm-mingw from %LOCALAPPDATA% (or gcc on PATH).
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$gcc = Get-ChildItem "$env:LOCALAPPDATA\llvm-mingw-*\bin\x86_64-w64-mingw32-gcc.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $gcc) { $gcc = 'x86_64-w64-mingw32-gcc' }
& $gcc -O2 -s -Wall -Wextra -o "$here\hidset.exe" "$here\hidset.c" -lhid -lsetupapi
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Host "built $here\hidset.exe"
