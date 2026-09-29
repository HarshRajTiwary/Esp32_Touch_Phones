# Builds and runs the host (PC) unit tests for Phone2's hardware-independent code.
# Needs any g++ (MinGW / Strawberry Perl / MSYS2) on PATH.
#
#   powershell -File tools\test.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$fw = Join-Path $root "firmware"
$outDir = Join-Path $env:LOCALAPPDATA "Phone2"
New-Item -ItemType Directory -Force $outDir | Out-Null
$out = Join-Path $outDir "phone2_tests.exe"

$incDirs = @(
  "common\core\include", "common\btlink\include",
  "main_s3\components\modem\include", "main_s3\components\phonecore\include",
  "main_s3\components\storage\include", "main_s3\components\touch\include",
  "main_s3\components\phone\include", "bt_esp32\components\audio\include"
)
$inc = @()
foreach ($d in $incDirs) { $inc += @("-I", (Join-Path $fw $d)) }

$fwSources = @(
  "common\core\text_util.cpp", "common\core\cobs.cpp", "common\core\plog.cpp",
  "common\btlink\btlink_codec.cpp", "common\btlink\sco_policy.cpp",
  "main_s3\components\modem\at_parse.cpp", "main_s3\components\modem\sms_pdu.cpp",
  "main_s3\components\modem\sms_assembler.cpp", "main_s3\components\modem\cmux.cpp",
  "main_s3\components\modem\call_tracker.cpp",
  "main_s3\components\phonecore\net_rules.cpp", "main_s3\components\phonecore\events.cpp",
  "main_s3\components\touch\touch_cal.cpp", "main_s3\components\phone\battery.cpp",
  "main_s3\components\storage\contacts.cpp", "main_s3\components\storage\call_log.cpp",
  "main_s3\components\storage\messages.cpp",
  "bt_esp32\components\audio\dsp.cpp"
) | ForEach-Object { Join-Path $fw $_ }
$testSources = Get-ChildItem (Join-Path $root "tests\host") -Filter *.cpp | ForEach-Object { $_.FullName }

& g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -DTFT_WIDTH=240 -DTFT_HEIGHT=320 @inc -o $out @testSources @fwSources
if ($LASTEXITCODE -ne 0) { throw "compile failed" }
& $out
exit $LASTEXITCODE
