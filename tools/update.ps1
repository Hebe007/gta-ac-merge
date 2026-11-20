# Updates everything after a change: pulls, builds the GTA script, installs it and the AC app, and checks each step.
# Run in "Developer PowerShell for VS" from the repo folder:   .\tools\update.ps1
# Set your folders once below (or pass -GtaDir / -AcDir).
param(
    [string]$GtaDir = "C:\Program Files\Epic Games\GTAVEnhanced",
    [string]$AcDir = "C:\Program Files (x86)\Steam\steamapps\common\assettocorsa"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

function Fail($msg) { Write-Host "FAILED: $msg" -ForegroundColor Red; exit 1 }
function Ok($msg) { Write-Host "OK: $msg" -ForegroundColor Green }

if (-not (Test-Path (Join-Path $GtaDir "GTA5_Enhanced.exe"))) { Fail "GTA5_Enhanced.exe not found in $GtaDir - edit GtaDir at the top of tools\update.ps1" }
if (-not (Test-Path (Join-Path $AcDir "acs.exe"))) { Fail "acs.exe not found in $AcDir - edit AcDir at the top of tools\update.ps1" }
if (Get-Process -Name "GTA5_Enhanced" -ErrorAction SilentlyContinue) { Fail "GTA is running - close it first (Windows locks the script file while GTA has it loaded)" }

git pull
if ($LASTEXITCODE -ne 0) { Fail "git pull" }
Ok "pulled"

cmake -S . -B build -A x64 | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "cmake configure (is the ScriptHookV SDK in Scripthook\?)" }
cmake --build build --config Release --target GtaAcLink
if ($LASTEXITCODE -ne 0) { Fail "build - see the errors above" }
$asi = Join-Path $repo "build\gta\Release\GtaAcLink.asi"
Ok "built $asi ($((Get-Item $asi).LastWriteTime))"

Copy-Item $asi $GtaDir -Force
if (-not (Test-Path (Join-Path $GtaDir "gta_link.ini"))) { Copy-Item (Join-Path $repo "gta\gta_link.ini") $GtaDir }
$installed = Get-Item (Join-Path $GtaDir "GtaAcLink.asi")
Ok "GTA script installed ($($installed.LastWriteTime)); your gta_link.ini was left as it is"

$app = Join-Path $AcDir "apps\lua\GtaLink"
New-Item -ItemType Directory -Force $app | Out-Null
Copy-Item (Join-Path $repo "csp\GtaLink\*") $app -Recurse -Force
Ok "AC app installed in $app (restart AC to load it)"

$keys = Get-Content (Join-Path $GtaDir "gta_link.ini") | Where-Object { $_ -match "^\s*[a-z_]+\s*=" } | ForEach-Object { ($_ -split "=")[0].Trim() }
$dups = $keys | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name }
if ($dups) {
    Write-Host ""
    Write-Host "WARNING: gta_link.ini has these settings more than once; GTA uses the FIRST one: $($dups -join ', ')" -ForegroundColor Red
    Write-Host "         Open $GtaDir\gta_link.ini and keep one line each." -ForegroundColor Red
}

Write-Host ""
Write-Host "New gta_link.ini settings you may not have yet (add them by hand if missing):" -ForegroundColor Yellow
$mine = Get-Content (Join-Path $GtaDir "gta_link.ini") | ForEach-Object { ($_ -split "=")[0].Trim() }
Get-Content (Join-Path $repo "gta\gta_link.ini") | Where-Object { $_ -match "^[a-z_]+=" } | ForEach-Object {
    if ($mine -notcontains ($_ -split "=")[0].Trim()) { Write-Host "  $_" }
}
