param([string]$VcpkgRoot)

$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (-not $VcpkgRoot) { $VcpkgRoot = $env:VCPKG_ROOT }
if (-not $VcpkgRoot) { $VcpkgRoot = Read-Host '请输入 vcpkg 根目录' }
if (-not (Test-Path -LiteralPath (Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake') -PathType Leaf)) {
    throw "无效的 vcpkg 根目录：$VcpkgRoot"
}
$env:VCPKG_ROOT = (Resolve-Path -LiteralPath $VcpkgRoot).Path
$packageParent = Join-Path $repo 'build/package'
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$package = Join-Path $packageParent "AnimeVault-$runId"
$publish = Join-Path $packageParent "host-publish-$runId"

Push-Location $repo
try {
    & cmake --preset release
    if ($LASTEXITCODE -ne 0) { throw 'Release 配置失败' }
    & cmake --build --preset release
    if ($LASTEXITCODE -ne 0) { throw 'Release 构建失败' }
    if (-not (Test-Path -LiteralPath 'frontend/node_modules/vite/bin/vite.js')) {
        & npm.cmd --prefix frontend ci
        if ($LASTEXITCODE -ne 0) { throw '前端依赖安装失败' }
    }
    & npm.cmd --prefix frontend run build
    if ($LASTEXITCODE -ne 0) { throw '前端构建失败' }
    & dotnet publish desktop/AnimeVault.Desktop/AnimeVault.Desktop.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o $publish
    if ($LASTEXITCODE -ne 0) { throw '桌面宿主发布失败' }

    # Every run uses a new directory, preserving older packages and user-added files.
    if (Test-Path -LiteralPath $package) { throw '本次打包目录意外存在；不会覆盖。' }
    New-Item -ItemType Directory -Path (Join-Path $package 'backend') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $package 'web') -Force | Out-Null
    $backendOutput = Join-Path $repo 'build/release/backend/Release'
    Copy-Item -LiteralPath (Join-Path $backendOutput 'anime_vault_server.exe') -Destination (Join-Path $package 'backend')
    Get-ChildItem -LiteralPath $backendOutput -Filter '*.dll' -File |
        Copy-Item -Destination (Join-Path $package 'backend')
    Copy-Item -Path (Join-Path $repo 'frontend/dist/*') -Destination (Join-Path $package 'web') -Recurse
    Get-ChildItem -LiteralPath $publish -File |
        Where-Object { $_.Extension -ne '.pdb' } |
        Copy-Item -Destination $package
    if (-not (Test-Path -LiteralPath (Join-Path $package 'AnimeVault.exe')) -or
        -not (Test-Path -LiteralPath (Join-Path $package 'web/index.html'))) {
        throw '打包产物缺少启动程序或页面'
    }
    Write-Host "可运行目录：$package"
} finally {
    Pop-Location
}
