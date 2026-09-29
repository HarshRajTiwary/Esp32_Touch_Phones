# TouchPhone build / flash helper (needs arduino-cli with the esp32 core 3.3.x).
#
#   powershell -File tools\build.ps1                 # compile only
#   powershell -File tools\build.ps1 -Upload -Port COM5
#   powershell -File tools\build.ps1 -Upload -Port COM5 -Monitor
#
# Libraries are fetched once by tools\setup.ps1 into %LOCALAPPDATA%\TouchPhone\libraries
# (kept outside OneDrive so the 120 MB of fonts are not synced).
param(
  [switch]$Upload,
  [string]$Port = "",
  [switch]$Monitor,
  [switch]$Verbose
)
$ErrorActionPreference = "Stop"
$root   = Split-Path -Parent $PSScriptRoot
$sketch = Join-Path $root "firmware\TouchPhone"
$cache  = Join-Path $env:LOCALAPPDATA "TouchPhone"
$libs   = Join-Path $cache "libraries"
$build  = Join-Path $cache "build"

# ESP32 (WROVER, 4 MB flash + PSRAM) - "Huge APP" gives 3 MB for the firmware
# and ~900 KB LittleFS for contacts / messages / call history.
$fqbn = "esp32:esp32:esp32:PSRAM=enabled,PartitionScheme=huge_app,CPUFreq=240,FlashMode=dio,FlashFreq=80,FlashSize=4M,UploadSpeed=921600,DebugLevel=none,LoopCore=1,EventsCore=1"

if (-not (Test-Path (Join-Path $libs "LovyanGFX"))) {
  Write-Host "LovyanGFX missing - running tools\setup.ps1 first"
  & (Join-Path $PSScriptRoot "setup.ps1")
}

# --jobs 2: this PC has no page file; unlimited parallel compilers can run it out of memory
$cliArgs = @("compile", "--fqbn", $fqbn, "--libraries", $libs, "--build-path", $build, "--warnings", "default", "--jobs", "2")
if ($Verbose) { $cliArgs += "--verbose" }
if ($Upload) {
  if (-not $Port) { throw "Give the serial port, e.g. -Port COM5 (see Device Manager)" }
  $cliArgs += @("--upload", "--port", $Port)
}
$cliArgs += $sketch

& arduino-cli @cliArgs
if ($LASTEXITCODE -ne 0) { throw "arduino-cli failed ($LASTEXITCODE)" }

if ($Monitor) {
  if (-not $Port) { throw "-Monitor needs -Port" }
  & arduino-cli monitor --port $Port --config baudrate=115200
}
