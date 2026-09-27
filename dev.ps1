param(
    [string]$VcpkgRoot,
    [ValidateRange(1,65535)][int]$BackendPort = 8848,
    [ValidateRange(1,65535)][int]$FrontendPort = 5173
)

$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
if (-not $VcpkgRoot) { $VcpkgRoot = $env:VCPKG_ROOT }
if (-not $VcpkgRoot) { $VcpkgRoot = Read-Host '请输入 vcpkg 根目录' }
$toolchain = Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake'
if (-not (Test-Path -LiteralPath $toolchain -PathType Leaf)) { throw "无效的 vcpkg 根目录：$VcpkgRoot" }
$env:VCPKG_ROOT = (Resolve-Path -LiteralPath $VcpkgRoot).Path

function Assert-FreePort([int]$port) {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $port)
    try { $listener.Start() } catch { throw "本机端口 $port 已被占用；不会停止现有进程。" } finally { $listener.Stop() }
}
Assert-FreePort $BackendPort
Assert-FreePort $FrontendPort

Push-Location $repo
try {
    # Keep the managed Debug binary separate from older start-backend.bat instances.
    $managedBuild = Join-Path $repo 'build/dev-managed'
    & cmake --preset dev -B $managedBuild
    if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败' }
    & cmake --build $managedBuild --config Debug
    if ($LASTEXITCODE -ne 0) { throw '后端构建失败' }
    if (-not (Test-Path -LiteralPath (Join-Path $repo 'frontend/node_modules/vite/bin/vite.js'))) {
        & npm.cmd --prefix frontend ci
        if ($LASTEXITCODE -ne 0) { throw '前端依赖安装失败' }
    }

    $backendExe = Join-Path $managedBuild 'backend/Debug/anime_vault_server.exe'
    $viteScript = Join-Path $repo 'frontend/node_modules/vite/bin/vite.js'
    $nodeExe = (Get-Command node.exe -ErrorAction Stop).Source
    $env:ANIME_VAULT_PORT = "$BackendPort"
    $env:ANIME_VAULT_DEV_BACKEND_PORT = "$BackendPort"
    if (-not $env:ANIME_VAULT_BANGUMI_USER_AGENT) {
        $env:ANIME_VAULT_BANGUMI_USER_AGENT = 'suoyi127/Link-to-Bangumi/0.1 (Windows)'
    }
    Assert-FreePort $BackendPort
    Assert-FreePort $FrontendPort
    $backend = $null
    $frontend = $null
    try {
        $backend = Start-Process -FilePath $backendExe -WorkingDirectory (Split-Path $backendExe) -WindowStyle Hidden -PassThru
        $frontend = Start-Process -FilePath $nodeExe -ArgumentList @($viteScript, '--host', '127.0.0.1', '--port', "$FrontendPort", '--strictPort') -WorkingDirectory (Join-Path $repo 'frontend') -WindowStyle Hidden -PassThru
        Write-Host "前端：http://127.0.0.1:$FrontendPort/"
        Write-Host "后端：http://127.0.0.1:$BackendPort/health"
        Write-Host '按 Ctrl+C 停止本次启动的两个进程。'
        while (-not $backend.HasExited -and -not $frontend.HasExited) { Start-Sleep -Milliseconds 500 }
        throw '后端或前端进程已退出。'
    } finally {
        foreach ($child in @($frontend, $backend)) {
            if ($child -and -not $child.HasExited) { Stop-Process -Id $child.Id -ErrorAction SilentlyContinue }
            if ($child) { $child.Dispose() }
        }
    }
} finally {
    Pop-Location
}
