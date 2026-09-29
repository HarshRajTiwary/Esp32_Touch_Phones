# Builds the PC UI simulator and renders every screen to docs\screens\*.png
# (plus 3x3 contact sheets), using the real UI code with fake phone data.
# Needs g++/gcc on PATH (e.g. Strawberry Perl or MSYS2) and tools\setup.ps1 run once.
#
#   powershell -File tools\uisim.ps1
$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot
$fw    = Join-Path $root "firmware"
$sim   = Join-Path $root "tests\uisim"
$lgfx  = Join-Path $env:LOCALAPPDATA "Phone2\deps\LovyanGFX\src"
$obj   = Join-Path $env:LOCALAPPDATA "Phone2\uisim"
$out   = Join-Path $root "docs\screens"
if (-not (Test-Path $lgfx)) { throw "LovyanGFX missing - run tools\setup.ps1 first" }
New-Item -ItemType Directory -Force $obj, $out | Out-Null

$incDirs = @(
  "main_s3\components\ui\include", "main_s3\components\display\include", "main_s3\components\touch\include",
  "main_s3\components\phonecore\include", "main_s3\components\storage\include", "main_s3\components\modem\include",
  "main_s3\components\btclient\include", "main_s3\components\net\include", "main_s3\components\phone\include",
  "common\core\include"
)
$inc = @("-I", (Join-Path $sim "stubs"), "-I", $lgfx, "-I", (Join-Path $root "boards\s3_main"))
foreach ($d in $incDirs) { $inc += @("-I", (Join-Path $fw $d)) }
$defs = @("-DARDUINO=10819", "-DLGFX_USE_V1", "-DTFT_WIDTH=240", "-DTFT_HEIGHT=320")

# LovyanGFX (drawing core + fonts + PNG encoder) - compiled once and cached
$lgfxCore = Join-Path $obj "lgfx_v1.o"
if (-not (Test-Path $lgfxCore)) {
  Write-Host "Compiling LovyanGFX core (one time, ~2 min)..."
  & g++ -std=gnu++17 -O1 -c @inc @defs (Join-Path $lgfx "lgfx\v1\lgfx_v1.cpp") -o $lgfxCore
  if ($LASTEXITCODE -ne 0) { throw "LovyanGFX compile failed" }
}
$cFiles = @("utility\lgfx_miniz", "utility\lgfx_pngle", "utility\lgfx_qrcode", "utility\lgfx_tjpgd", "utility\lgfx_qoi",
            "Fonts\efont\lgfx_efont_cn", "Fonts\efont\lgfx_efont_ja", "Fonts\efont\lgfx_efont_kr",
            "Fonts\efont\lgfx_efont_tw", "Fonts\IPA\lgfx_font_japan")
foreach ($c in $cFiles) {
  $o = Join-Path $obj ((Split-Path $c -Leaf) + ".o")
  if (-not (Test-Path $o)) {
    & gcc -O0 -c @inc @defs (Join-Path $lgfx ("lgfx\" + $c + ".c")) -o $o
    if ($LASTEXITCODE -ne 0) { throw "compile of $c failed" }
  }
}

$ui = "main_s3\components\ui"
$sources = @(
  (Join-Path $sim "sim_main.cpp"), (Join-Path $sim "sim_stubs.cpp")
) + (@(
  "$ui\gui.cpp", "$ui\icons.cpp", "$ui\keyboard.cpp", "$ui\nav.cpp", "$ui\screens_main.cpp", "$ui\screens_phone.cpp",
  "$ui\screens_sms.cpp", "$ui\screens_contacts.cpp", "$ui\screens_settings.cpp", "$ui\screens_net.cpp",
  "$ui\screens_diag.cpp",
  "common\core\text_util.cpp", "main_s3\components\modem\at_parse.cpp", "main_s3\components\modem\sms_pdu.cpp",
  "main_s3\components\phonecore\net_rules.cpp", "main_s3\components\phonecore\events.cpp",
  "main_s3\components\touch\touch_cal.cpp",
  "main_s3\components\storage\contacts.cpp", "main_s3\components\storage\call_log.cpp",
  "main_s3\components\storage\messages.cpp"
) | ForEach-Object { Join-Path $fw $_ })
$objs = Get-ChildItem $obj -Filter *.o | ForEach-Object { $_.FullName }
$exe = Join-Path $obj "uisim.exe"
& g++ -std=gnu++17 -O1 @inc @defs -o $exe @sources @objs
if ($LASTEXITCODE -ne 0) { throw "simulator build failed" }
& $exe $out
