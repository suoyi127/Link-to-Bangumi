param(
    [Parameter(Mandatory=$true)][string]$PackageDir,
    [string]$IsccPath,
    [string]$AppVersion = '1.0.0'
)

$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$package = (Resolve-Path -LiteralPath $PackageDir -ErrorAction Stop).Path
$packageRoot = [IO.Path]::GetFullPath((Join-Path $repo 'build/package')).TrimEnd('\') + '\'
if (-not $package.StartsWith($packageRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw '安装包输入必须是本仓库 build/package 下的发布目录。'
}
# Do not let a junction inside the build tree pull arbitrary external files into a release.
if (((Get-Item -LiteralPath $package).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw '发布目录不能是符号链接或目录联接。'
}
$linkedEntry = Get-ChildItem -LiteralPath $package -Recurse -Force |
    Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 } |
    Select-Object -First 1
if ($linkedEntry) { throw "发布目录包含符号链接或目录联接：$($linkedEntry.FullName)" }
foreach ($relative in @('AnimeVault.exe', 'backend/anime_vault_server.exe', 'web/index.html', 'WebView2Loader.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $relative) -PathType Leaf)) {
        throw "发布目录缺少 $relative"
    }
}
if ($AppVersion -notmatch '^\d+\.\d+\.\d+(?:\.\d+)?$') { throw 'AppVersion 必须是数字版本号。' }

if (-not $IsccPath) {
    $candidate = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($candidate) { $IsccPath = $candidate.Source }
}
if (-not $IsccPath) {
    foreach ($candidate in @('C:\Program Files\Inno Setup 7\ISCC.exe', 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe')) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $IsccPath = $candidate; break }
    }
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
    throw '找不到 ISCC.exe；请安装 Inno Setup 7，或传入 -IsccPath。'
}

$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$output = Join-Path $repo "build/installer/AnimeVault-$runId"
$prereq = Join-Path $repo "build/installer/prerequisites-$runId"
New-Item -ItemType Directory -Path $output, $prereq | Out-Null

function Get-MicrosoftInstaller([string]$url, [string]$destination) {
    Invoke-WebRequest -Uri $url -OutFile $destination -MaximumRedirection 10
    $signature = Get-AuthenticodeSignature -LiteralPath $destination
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Subject -notmatch '(^|,)\s*O=Microsoft Corporation(,|$)') {
        throw "下载文件不是有效的 Microsoft 签名：$destination"
    }
}

Get-MicrosoftInstaller 'https://aka.ms/vc14/vc_redist.x64.exe' (Join-Path $prereq 'vc_redist.x64.exe')
Get-MicrosoftInstaller 'https://go.microsoft.com/fwlink/p/?LinkId=2124703' (Join-Path $prereq 'MicrosoftEdgeWebview2Setup.exe')

& $IsccPath "--define=PackageDir=$package" "--define=PrereqDir=$prereq" "--define=InstallerOutputDir=$output" "--define=AppVersion=$AppVersion" (Join-Path $repo 'installer/AnimeVault.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup 编译失败。' }
$setup = Join-Path $output 'Setup.exe'
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw '编译成功但找不到 Setup.exe。' }
Write-Host "安装程序：$setup"
