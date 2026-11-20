# Builds GtaExtract (reads GTA V's collision with CodeWalker.Core). Run once, and again after a git pull that
# changes tools\gtaextract. Needs the .NET 8 SDK (Visual Studio Installer: ".NET desktop development").
#   powershell -ExecutionPolicy Bypass -File .\tools\build_extractor.ps1
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo
function Fail($msg) { Write-Host "FAILED: $msg" -ForegroundColor Red; exit 1 }
function Ok($msg) { Write-Host "OK: $msg" -ForegroundColor Green }

if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) { Fail "dotnet not found - install the .NET 8 SDK (Visual Studio Installer: .NET desktop development)" }
$cw = Join-Path $repo "third_party\CodeWalker"
if (Test-Path (Join-Path $cw ".git")) {
    git -C $cw pull --ff-only
    if ($LASTEXITCODE -ne 0) { Fail "updating CodeWalker" }
} else {
    git clone --depth 1 https://github.com/dexyfex/CodeWalker $cw
    if ($LASTEXITCODE -ne 0) { Fail "downloading CodeWalker" }
}
Ok "CodeWalker source: $(git -C $cw log -1 --format='%h %cd' --date=short)"

dotnet build tools\gtaextract\GtaExtract.csproj -c Release
if ($LASTEXITCODE -ne 0) { Fail "build - see the errors above" }
$exe = Join-Path $repo "tools\gtaextract\bin\Release\net8.0\GtaExtract.exe"
& $exe --self-test --out (Join-Path $env:TEMP "gtaextract_selftest.gtac")
if ($LASTEXITCODE -ne 0) { Fail "self-test" }
Ok "built $exe"
Write-Host ""
Write-Host "Next: extract the collision (takes a while; GTA can stay closed):" -ForegroundColor Yellow
Write-Host "  tools\gtaextract\bin\Release\net8.0\GtaExtract.exe --gta ""C:\Program Files\Epic Games\GTAVEnhanced"" --out work\gta_collision.gtac"
