# One-time setup: installs the ESP32 core (if missing) and fetches the pinned
# LovyanGFX release into %LOCALAPPDATA%\TouchPhone\libraries.
$ErrorActionPreference = "Stop"
$libs = Join-Path $env:LOCALAPPDATA "TouchPhone\libraries"
New-Item -ItemType Directory -Force $libs | Out-Null

$lovyanTag = "1.2.30"
$lovyan = Join-Path $libs "LovyanGFX"
if (-not (Test-Path $lovyan)) {
  Write-Host "Fetching LovyanGFX $lovyanTag ..."
  git clone --quiet --depth 1 --branch $lovyanTag https://github.com/lovyan03/LovyanGFX.git $lovyan
  Remove-Item -Recurse -Force (Join-Path $lovyan ".git"), (Join-Path $lovyan "examples"), (Join-Path $lovyan "examples_for_PC"), (Join-Path $lovyan "examples_for_picosdk"), (Join-Path $lovyan "doc") -ErrorAction SilentlyContinue
} else {
  Write-Host "LovyanGFX already present"
}

$cores = & arduino-cli core list
if (-not ($cores -match "esp32:esp32\s+3\.")) {
  Write-Host "Installing esp32 core 3.3.x ..."
  & arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
  & arduino-cli core update-index
  & arduino-cli core install esp32:esp32@3.3.8
}
Write-Host "Setup done."
