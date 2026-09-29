# Phone2 build / flash helper.
#
#   powershell -File tools\build.ps1                          # build both firmwares
#   powershell -File tools\build.ps1 -Target main             # ESP32-S3 phone only
#   powershell -File tools\build.ps1 -Target bt               # ESP32 Bluetooth helper only
#   powershell -File tools\build.ps1 -Target main -Flash -Port COM7 -Monitor
#   powershell -File tools\build.ps1 -Target bt -Clean        # fresh configure (after editing sdkconfig.defaults)
#
# Build output and the generated sdkconfig live in %LOCALAPPDATA%\Phone2\build\<target>
# (outside OneDrive). -Jobs limits parallel compilers: this PC has no page file and
# runs out of memory with the default (one job per CPU thread).
param(
  [ValidateSet("all", "main", "bt")] [string]$Target = "all",
  [switch]$Flash,
  [string]$Port = "",
  [switch]$Monitor,
  [switch]$Clean,
  [int]$Jobs = 2
)
# Native tools (idf.py, cmake, the compiler) print warnings on stderr; failures are
# detected through $LASTEXITCODE instead of PowerShell error records.
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "env.ps1")

$projects = @{
  "main" = @{ Dir = "firmware\main_s3";  Chip = "esp32s3" };
  "bt"   = @{ Dir = "firmware\bt_esp32"; Chip = "esp32" }
}
$targets = if ($Target -eq "all") { @("bt", "main") } else { @($Target) }
if (($Flash -or $Monitor) -and $targets.Count -ne 1) { throw "-Flash/-Monitor need -Target main or -Target bt" }
if (($Flash -or $Monitor) -and -not $Port) { throw "give the serial port, e.g. -Port COM7 (see Device Manager)" }

foreach ($t in $targets) {
  $p = $projects[$t]
  $src = Join-Path $root $p.Dir
  $build = Join-Path $env:LOCALAPPDATA "Phone2\build\$t"
  $sdkconfig = Join-Path $build "sdkconfig"
  if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }
  New-Item -ItemType Directory -Force $build | Out-Null

  Write-Host "=== $t ($($p.Chip)) ==="
  if (-not (Test-Path (Join-Path $build "build.ninja"))) {
    & idf.py -C $src -B $build -D "SDKCONFIG=$sdkconfig" -D "IDF_TARGET=$($p.Chip)" reconfigure
    if ($LASTEXITCODE -ne 0) { throw "configure of $t failed" }
  }
  # -k 0: keep compiling after an error so one build shows every problem
  & cmake --build $build -j $Jobs -- -k 0
  if ($LASTEXITCODE -ne 0) { throw "build of $t failed" }
  & idf.py -C $src -B $build -D "SDKCONFIG=$sdkconfig" size
  if ($Flash) {
    & idf.py -C $src -B $build -D "SDKCONFIG=$sdkconfig" -p $Port -b 921600 flash
    if ($LASTEXITCODE -ne 0) { throw "flash of $t failed" }
  }
  if ($Monitor) {
    & idf.py -C $src -B $build -D "SDKCONFIG=$sdkconfig" -p $Port monitor
  }
}
