# =============================================================================
#  DesktopPet 自动发布脚本
#  构建 → 打标签 → 推送 → 创建Release → 上传附件
#  用法: .\release.ps1 -Version v1.0.0 -Notes "发布说明..."
# =============================================================================

param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$Notes = ""
)

$ErrorActionPreference = "Stop"
$PROJECT = "e:\Yiyi Desktop animal"

# 从 git 凭证管理器获取 GitHub token
function Get-GitHubToken {
    $tempFile = "$env:TEMP\git-cred.txt"
    [System.IO.File]::WriteAllText($tempFile, "protocol=https`nhost=github.com`n`n", [System.Text.Encoding]::ASCII)
    $result = & cmd.exe /c "type `"$tempFile`" | git credential fill" 2>&1
    Remove-Item $tempFile -ErrorAction SilentlyContinue
    foreach ($line in $result) {
        if ($line -match "^password=(.+)") { return $matches[1] }
    }
    throw "无法获取 GitHub Token"
}

Write-Host "=== DesktopPet 自动发布 [${Version}] ===" -ForegroundColor Cyan

# 1. 构建
Write-Host "[1/5] 构建..." -ForegroundColor Yellow
$env:Path = "E:\Desktop\一一\软件\RedPanda-CPP\mingw64\bin;$env:Path"
Set-Location $PROJECT
if (Test-Path dist) { Remove-Item -Recurse -Force dist }
New-Item -ItemType Directory -Force dist | Out-Null
g++ -o dist/DesktopPet.exe main.cpp -mwindows -static-libgcc -static-libstdc++ -O2 -lgdiplus -lcomctl32 -municode 2>&1
if ($LASTEXITCODE -ne 0) { throw "构建失败" }
Copy-Item -Recurse animal dist/animal
Write-Host "  [OK] $( (Get-Item dist/DesktopPet.exe).Length / 1KB ) KB"

# 复制 MinGW DLLs
$MINGW_DIR = "E:\Desktop\一一\软件\RedPanda-CPP\mingw64"
$MINGW_DLLS = @("libwinpthread-1.dll")
foreach ($dll in $MINGW_DLLS) {
    $src = "$MINGW_DIR\bin\$dll"
    if (Test-Path $src) {
        Copy-Item $src "dist\$dll" -Force
        Write-Host "  [OK] $dll copied" -ForegroundColor Green
    }
}

# 2. 打包
Write-Host "[2/5] 打包..." -ForegroundColor Yellow
$zipPath = "$PROJECT\dist\DesktopPet-${Version}.zip"
Compress-Archive -Path "dist\DesktopPet.exe","dist\animal","dist\libwinpthread-1.dll" -DestinationPath $zipPath -Force
Write-Host "  [OK] $( (Get-Item $zipPath).Length / 1MB ) MB"

# 3. 提交并推送标签
Write-Host "[3/5] 推送标签..." -ForegroundColor Yellow
git add -A
git commit --allow-empty -m "release: ${Version}" 2>&1 | Out-Null
git push origin main 2>&1 | Out-Null
git tag -f $Version 2>&1
git push origin $Version 2>&1
Write-Host "  [OK] tag ${Version} 已推送"

# 4. 创建 Release
Write-Host "[4/5] 创建 Release..." -ForegroundColor Yellow
$token = Get-GitHubToken
$body = @{
    tag_name   = $Version
    name       = $Version
    body       = $Notes
    draft      = $false
    prerelease = $false
} | ConvertTo-Json

$release = Invoke-RestMethod -Method Post `
    -Uri "https://api.github.com/repos/1T-T1-byte/Yiyi-Desktop-animal/releases" `
    -Headers @{ Authorization = "token $token"; Accept = "application/vnd.github+json" } `
    -Body $body -ContentType "application/json" -UseBasicParsing

$releaseId = $release.id
Write-Host "  [OK] $($release.html_url)"

# 5. 上传附件
Write-Host "[5/5] 上传附件..." -ForegroundColor Yellow
$uploadUrl = "https://uploads.github.com/repos/1T-T1-byte/Yiyi-Desktop-animal/releases/${releaseId}/assets?name=DesktopPet-${Version}.zip"
$asset = Invoke-RestMethod -Uri $uploadUrl -Method Post `
    -Headers @{ Authorization = "token $token"; Accept = "application/vnd.github+json"; "Content-Type" = "application/zip" } `
    -InFile $zipPath -UseBasicParsing
$assetSize = $asset.size
Write-Host "  [OK] $($asset.name) ($assetSize bytes)"

Write-Host ""
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "  ${Version} 发布完成!" -ForegroundColor Green
Write-Host "  $($release.html_url)" -ForegroundColor White
Write-Host "=========================================" -ForegroundColor Cyan
