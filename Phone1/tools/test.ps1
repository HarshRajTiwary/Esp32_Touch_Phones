# Builds and runs the host (PC) unit tests for the hardware-independent code.
# Needs any g++ (MinGW / Strawberry Perl / MSYS2) on PATH.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root "firmware\TouchPhone\src"
$outDir = Join-Path $env:LOCALAPPDATA "TouchPhone"
New-Item -ItemType Directory -Force $outDir | Out-Null
$out  = Join-Path $outDir "touchphone_tests.exe"

$files = @(
  (Join-Path $root "tests\host\test_main.cpp"),
  (Join-Path $src "core\text_util.cpp"),
  (Join-Path $src "modem\at_parse.cpp"),
  (Join-Path $src "modem\sms_pdu.cpp"),
  (Join-Path $src "modem\sms_assembler.cpp"),
  (Join-Path $src "audio\dsp.cpp")
)

& g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -o $out @files
if ($LASTEXITCODE -ne 0) { throw "compile failed" }
& $out
exit $LASTEXITCODE
