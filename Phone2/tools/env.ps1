# Loads the ESP-IDF environment into the current PowerShell session.
#
#   . tools\env.ps1        (note the leading dot)
#
# export.ps1 runs whatever "python" is first on PATH; this PC's default is 3.14,
# but the IDF virtual environment was created with Python 3.11, so put 3.11 first.
$py311 = Join-Path $env:LOCALAPPDATA "Programs\Python\Python311"
if (Test-Path $py311) { $env:PATH = "$py311;$py311\Scripts;$env:PATH" }
$env:IDF_PATH = Join-Path $env:USERPROFILE "esp\esp-idf-v5.5.5"
if (-not (Test-Path (Join-Path $env:IDF_PATH "export.ps1"))) {
  throw "ESP-IDF not found at $env:IDF_PATH - run tools\setup.ps1 first"
}
# export.ps1 prints progress on stderr; with $ErrorActionPreference = "Stop" in the
# calling script, Windows PowerShell 5.1 would turn that into a terminating error.
$savedEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
. (Join-Path $env:IDF_PATH "export.ps1") *> $null
$ErrorActionPreference = $savedEap
$env:PHONE2_DEPS = Join-Path $env:LOCALAPPDATA "Phone2\deps"
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) { throw "ESP-IDF export failed" }
