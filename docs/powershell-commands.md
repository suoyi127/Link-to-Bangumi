# PowerShell 命令

本文件对应 [README 的启动流程](../README.md#启动流程)。按所需流程选择一节执行，不必从头执行所有命令。

示例项目目录为 `D:\代码库\Link-to-Bangumi`，vcpkg 目录为 `D:\av-build-cache\vcpkg`。请替换成自己的实际路径；带空格或中文的路径保留引号。每行单独执行，出现错误时先解决该错误，再执行下一步。

<a id="source"></a>
## 获取源码与进入项目目录

首次获取源码，在存放项目的目录运行：

```powershell
Set-Location 'D:\代码库'
git clone https://github.com/suoyi127/Link-to-Bangumi.git
Set-Location '.\Link-to-Bangumi'
```

已有源码时，只需进入项目根目录：

```powershell
Set-Location 'D:\代码库\Link-to-Bangumi'
```

确认该目录包含 `README.md`、`dev.ps1` 和 `frontend` 文件夹。

<a id="vcpkg"></a>
## 准备 vcpkg

`VCPKG_ROOT` 是 vcpkg 的安装目录，不是项目目录；其中应包含 `scripts\buildsystems\vcpkg.cmake`。本项目通过 `vcpkg.json` 固定依赖版本，构建时会自动安装所需 C++ 库。

若尚未下载 vcpkg，可在已存在的依赖存放目录执行：

```powershell
git clone https://github.com/microsoft/vcpkg.git 'D:\av-build-cache\vcpkg'
```

首次使用时初始化 vcpkg：

```powershell
$env:VCPKG_ROOT = 'D:\av-build-cache\vcpkg'
& "$env:VCPKG_ROOT\bootstrap-vcpkg.bat"
```

这里设置的环境变量只在当前 PowerShell 窗口及它启动的进程中生效。已有初始化完成的 vcpkg 时，不需要重复下载或初始化。

<a id="development"></a>
## 同时启动前后端

在项目根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\dev.ps1 -VcpkgRoot 'D:\av-build-cache\vcpkg'
```

也可以让脚本提示输入 vcpkg 路径；若当前窗口已设置 `VCPKG_ROOT`，脚本会直接使用它：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\dev.ps1
```

选择其中一种即可。脚本完成构建后会打印前端和后端地址。默认访问 `http://127.0.0.1:5173/`，按 Ctrl+C 结束两个服务。执行策略参数仅对这次新开的 PowerShell 进程生效，不修改系统设置。

<a id="ports"></a>
## 更换开发端口

如果默认端口已有服务，选择其他空闲端口，例如：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\dev.ps1 -VcpkgRoot 'D:\av-build-cache\vcpkg' -BackendPort 8849 -FrontendPort 5174
```

此时打开 `http://127.0.0.1:5174/`，后端健康检查为 `http://127.0.0.1:8849/health`。脚本会自动调整前端请求后端的地址。

<a id="backend-build"></a>
## 构建后端

在项目根目录执行，等待命令结束后再启动后端：

```powershell
.\build-backend.bat 'D:\av-build-cache\vcpkg'
```

如果当前窗口已设置 `VCPKG_ROOT`，或希望在提示中输入路径，也可以执行：

```powershell
.\build-backend.bat
```

命令参数优先于当前环境变量。脚本会配置并构建 `build\dev` 下的 Debug 后端；它不会启动服务，也不会永久修改 Windows 环境变量。

<a id="backend-start"></a>
## 启动后端

构建成功后，在项目根目录执行：

```powershell
.\start-backend.bat
```

保留这个窗口，后端会持续运行；按 Ctrl+C 或关闭窗口会停止服务。默认健康检查地址为 `http://127.0.0.1:8848/health`，应返回包含 `"status":"ok"` 的内容。

<a id="frontend-start"></a>
## 启动前端

后端保持运行。另开一个 PowerShell 窗口，执行：

```powershell
Set-Location 'D:\代码库\Link-to-Bangumi'
npm.cmd --prefix frontend ci
npm.cmd --prefix frontend run dev
```

首次使用或依赖发生变化时执行依赖安装行；已安装依赖的日常启动可跳过该行。打开 Vite 打印的本地地址，默认是 `http://127.0.0.1:5173/`。在该窗口按 Ctrl+C 停止前端。

<a id="separate-ports"></a>
## 单独启动时更换端口

使用两个窗口时，需要分别设置后端监听端口和前端代理目标。

第一个窗口启动后端：

```powershell
Set-Location 'D:\代码库\Link-to-Bangumi'
$env:ANIME_VAULT_PORT = '8849'
.\start-backend.bat
```

第二个窗口启动前端：

```powershell
Set-Location 'D:\代码库\Link-to-Bangumi'
$env:ANIME_VAULT_DEV_BACKEND_PORT = '8849'
npm.cmd --prefix frontend run dev -- --host 127.0.0.1 --port 5174 --strictPort
```

此时打开 `http://127.0.0.1:5174/`。后端和前端代理目标的端口必须相同。

<a id="desktop-package"></a>
## 生成桌面发布目录

准备好开发环境和 .NET 10 SDK 后，在项目根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\package-desktop.ps1 -VcpkgRoot 'D:\av-build-cache\vcpkg'
```

脚本会构建 Release 后端、前端页面和桌面宿主。请记下最后打印的完整输出目录，下一步生成安装包时需要它。打开该目录中的 `AnimeVault.exe` 可运行桌面程序。

<a id="installer"></a>
## 生成安装包

安装 Inno Setup 7，并把下方 `PackageDir` 替换为上一节打印的实际目录，`AppVersion` 替换为本次发布版本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-installer.ps1 -PackageDir 'D:\代码库\Link-to-Bangumi\build\package\AnimeVault-<实际时间戳和后缀>' -IsccPath 'C:\Program Files\Inno Setup 7\ISCC.exe' -AppVersion '0.0.2'
```

示例路径中的 `<实际时间戳和后缀>` 需要替换，不能原样运行。脚本会打印生成的 `Setup.exe` 路径，位于项目的 `build\installer` 下。

<a id="release"></a>
## 发布版本

仅在维护者已确认要发布版本、代码已提交并推送后执行。将示例版本号替换为一个尚未使用的新版本；标签应指向要发布的提交。

```powershell
git tag v0.0.2
git push origin v0.0.2
```

推送标签后，在 GitHub 仓库的 Actions 页查看构建结果，再到 Releases 页确认 `Setup.exe` 已上传。

<a id="verification"></a>
## 构建与测试

以下指令用于开发者交付前检查，在项目根目录逐行执行：

```powershell
$env:VCPKG_ROOT = 'D:\av-build-cache\vcpkg'
cmake --preset test
cmake --build --preset test
ctest --preset test --output-on-failure
npm.cmd --prefix frontend ci
npm.cmd --prefix frontend test -- --run
npm.cmd --prefix frontend run lint
npm.cmd --prefix frontend run build
```

如需指定 Visual Studio 安装，可在对应的 Developer PowerShell 中执行。普通使用和日常启动不需要运行这一节。
