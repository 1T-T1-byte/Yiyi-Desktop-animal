$ErrorActionPreference = "Stop"

$MINGW_BIN = "E:\Desktop\一一\软件\RedPanda-CPP\mingw64\bin"
$env:Path = "$MINGW_BIN;$env:Path"

$PROJECT  = "e:\Yiyi Desktop animal"
$DIST     = "$PROJECT\dist"
$EXE      = "$DIST\DesktopPet.exe"

Write-Host "=== Desktop Pet Build ===" -ForegroundColor Cyan

# cleanup
if (Test-Path $DIST) {
    Remove-Item -Recurse -Force $DIST
}
New-Item -ItemType Directory -Force $DIST | Out-Null

# compile
Set-Location $PROJECT
Write-Host "[BUILD] Compiling..." -ForegroundColor Cyan
g++ -o $EXE main.cpp `
    -mwindows -static-libgcc -static-libstdc++ -O2 `
    -lgdiplus -lcomctl32 -municode 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] Build failed" -ForegroundColor Red
    exit 1
}

# copy assets
if (Test-Path "$PROJECT\animal") {
    Copy-Item -Recurse "$PROJECT\animal" "$DIST\animal"
    Write-Host "[OK] Assets copied" -ForegroundColor Green
}

# copy MinGW DLLs
$MINGW_DLLS = @("libwinpthread-1.dll")
foreach ($dll in $MINGW_DLLS) {
    $src = "$MINGW_BIN\$dll"
    if (Test-Path $src) {
        Copy-Item $src "$DIST\$dll" -Force
        Write-Host "[OK] $dll copied" -ForegroundColor Green
    }
}

# show result
$info = Get-Item $EXE
Write-Host "[OK] $EXE ($([math]::Round($info.Length/1KB,1)) KB)" -ForegroundColor Green
Write-Host "Run: .\dist\DesktopPet.exe" -ForegroundColor White
