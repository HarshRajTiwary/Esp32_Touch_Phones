# Builds the PC UI simulator and renders every screen to docs\screens\*.png
# (plus 3x3 contact sheets), using the real UI code with fake phone data.
# Needs g++/gcc on PATH (e.g. Strawberry Perl or MSYS2) and tools\setup.ps1 run once.
$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot
$src   = Join-Path $root "firmware\TouchPhone\src"
$sim   = Join-Path $root "tests\uisim"
$lgfx  = Join-Path $env:LOCALAPPDATA "TouchPhone\libraries\LovyanGFX\src"
$obj   = Join-Path $env:LOCALAPPDATA "TouchPhone\uisim"
$out   = Join-Path $root "docs\screens"
New-Item -ItemType Directory -Force $obj, $out | Out-Null
$inc = @("-I", (Join-Path $sim "stubs"), "-I", $lgfx, "-DARDUINO=10819", "-DLGFX_USE_V1")

# LovyanGFX (drawing core + fonts + PNG encoder) - compiled once and cached
$lgfxCore = Join-Path $obj "lgfx_v1.o"
if (-not (Test-Path $lgfxCore)) {
  Write-Host "Compiling LovyanGFX core (one time, ~2 min)..."
  & g++ -std=gnu++17 -O1 -c @inc (Join-Path $lgfx "lgfx\v1\lgfx_v1.cpp") -o $lgfxCore
  if ($LASTEXITCODE -ne 0) { throw "LovyanGFX compile failed" }
}
$cFiles = @("utility\lgfx_miniz", "utility\lgfx_pngle", "utility\lgfx_qrcode", "utility\lgfx_tjpgd", "utility\lgfx_qoi",
            "Fonts\efont\lgfx_efont_cn", "Fonts\efont\lgfx_efont_ja", "Fonts\efont\lgfx_efont_kr",
            "Fonts\efont\lgfx_efont_tw", "Fonts\IPA\lgfx_font_japan")
foreach ($c in $cFiles) {
  $o = Join-Path $obj ((Split-Path $c -Leaf) + ".o")
  if (-not (Test-Path $o)) {
    & gcc -O0 -c @inc (Join-Path $lgfx ("lgfx\" + $c + ".c")) -o $o
    if ($LASTEXITCODE -ne 0) { throw "compile of $c failed" }
  }
}

$sources = @(
  (Join-Path $sim "sim_main.cpp"), (Join-Path $sim "sim_stubs.cpp"),
  "ui\gui.cpp", "ui\icons.cpp", "ui\keyboard.cpp", "ui\nav.cpp", "ui\screens_main.cpp", "ui\screens_phone.cpp",
  "ui\screens_sms.cpp", "ui\screens_contacts.cpp", "ui\screens_settings.cpp", "core\text_util.cpp",
  "modem\at_parse.cpp", "modem\sms_pdu.cpp", "store\contacts.cpp", "store\call_log.cpp", "store\messages.cpp"
) | ForEach-Object { if ([IO.Path]::IsPathRooted($_)) { $_ } else { Join-Path $src $_ } }
$objs = Get-ChildItem $obj -Filter *.o | ForEach-Object { $_.FullName }
$exe = Join-Path $obj "uisim.exe"
& g++ -std=gnu++17 -O1 @inc -o $exe @sources @objs
if ($LASTEXITCODE -ne 0) { throw "simulator build failed" }
& $exe $out
