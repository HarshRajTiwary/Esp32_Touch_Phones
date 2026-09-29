# One-time setup for Phone2.
#
#   powershell -File tools\setup.ps1
#
# * checks ESP-IDF v5.5.5 (installs it into %USERPROFILE%\esp if missing)
# * fetches the pinned third-party components into %LOCALAPPDATA%\Phone2\deps
#   (kept outside OneDrive: LovyanGFX alone is ~120 MB of fonts)
$ErrorActionPreference = "Stop"

$idfVersion = "v5.5.5"
$idfPath    = Join-Path $env:USERPROFILE "esp\esp-idf-$idfVersion"
$deps       = Join-Path $env:LOCALAPPDATA "Phone2\deps"
New-Item -ItemType Directory -Force $deps | Out-Null

# ESP-IDF needs Python 3.9 - 3.13. Python 3.14 is too new for some of IDF 5.5's pinned packages.
$py = Join-Path $env:LOCALAPPDATA "Programs\Python\Python311\python.exe"
if (-not (Test-Path $py)) { throw "Python 3.11 not found at $py - install Python 3.11 (python.org) first" }

if (-not (Test-Path (Join-Path $idfPath "tools\idf.py"))) {
  Write-Host "Cloning ESP-IDF $idfVersion (about 1.5 GB) ..."
  git -c advice.detachedHead=false clone -q -b $idfVersion --depth 1 --recursive --shallow-submodules -j 4 `
      https://github.com/espressif/esp-idf.git $idfPath
  if ($LASTEXITCODE -ne 0) { throw "ESP-IDF clone failed" }
}
Push-Location $idfPath
try {
  & $py tools\idf_tools.py install --targets "esp32,esp32s3"
  if ($LASTEXITCODE -ne 0) { throw "idf_tools install failed" }
  & $py tools\idf_tools.py install-python-env
  if ($LASTEXITCODE -ne 0) { throw "idf python env install failed" }
} finally { Pop-Location }

function Get-Dep([string]$name, [string]$url, [string]$tag, [switch]$Recursive) {
  $dir = Join-Path $deps $name
  if (Test-Path $dir) { Write-Host "$name already present"; return }
  Write-Host "Fetching $name $tag ..."
  $gitArgs = @("-c", "advice.detachedHead=false", "clone", "-q", "--depth", "1", "--branch", $tag)
  if ($Recursive) { $gitArgs += @("--recursive", "--shallow-submodules") }
  & git @gitArgs $url $dir
  if ($LASTEXITCODE -ne 0) { throw "clone of $name failed" }
}

Get-Dep "LovyanGFX" "https://github.com/lovyan03/LovyanGFX.git" "1.2.30"
# LovyanGFX's examples are not needed and slow down IDE indexing
foreach ($d in @("examples", "examples_for_PC", "examples_for_picosdk", "doc")) {
  $p = Join-Path $deps "LovyanGFX\$d"
  if (Test-Path $p) { Remove-Item -Recurse -Force $p }
}
Get-Dep "esp_littlefs" "https://github.com/joltwallet/esp_littlefs.git" "v1.22.3" -Recursive

Write-Host "Setup done. Build with: powershell -File tools\build.ps1"
