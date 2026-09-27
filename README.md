# Link to Bangumi（Anime Vault，Windows 本地版）

目前交付范围以阶段 0–5 为基础：扫描 qB 下载目录及独立的外来导入目录、修正解析、浏览本地番剧、查询与绑定 Bangumi、预览并确认整理，以及设置与审计。现已支持读取 qB Web API 状态与 Mikan RSS 文章、用双语标题建立本地别名，以及通过设置页添加 Mikan 订阅并自动建立按集去重的 qB 下载规则。番剧详情页可用本机 mpv 播放；自动扫描调度与清理仍未实现。qB 来源的“下载完成”目前仍由用户在执行整理时单独确认；文件稳定性扫描不等于 qB 完成证明。

## 目录与安全边界

默认目录（qB 来源由用户在设置页选择；未配置也可启动）：

| 用途 | 默认路径 | 环境变量 |
| --- | --- | --- |
| qB 下载来源 | 未配置，须在设置页选择已有目录 | `ANIME_VAULT_SOURCE_DIR` |
| 外来导入暂存（需自行创建，初始应为空） | `%USERPROFILE%\Videos\AnimeVault\Import` | `ANIME_VAULT_IMPORT_DIR` |
| 整理目标媒体库 | `%USERPROFILE%\Videos\AnimeVault\Library` | `ANIME_VAULT_LIBRARY_DIR` |
| SQLite 数据 | `%LOCALAPPDATA%\AnimeVault` | `ANIME_VAULT_DATA_DIR` |

首次使用时，在设置页填写已有的 qB 下载目录并保存，然后重启后端；更改目录不会移动或删除原文件。未配置或所选目录不可用时，qB 扫描与 Mikan 自动下载创建不可用，外来导入和本地浏览仍可使用。

从旧版 `D:\追番` 布局升级时，如需继续使用原 SQLite 数据，可在启动前显式设置 `ANIME_VAULT_DATA_DIR` 指向旧数据目录；开发者设置的 `ANIME_VAULT_SOURCE_DIR` 会覆盖设置页保存的 qB 下载目录，此时设置页禁止改写该目录。要切换到页面配置，先移除该环境变量并重启。

来源、导入和媒体库目录必须相互独立且不嵌套。扫描只读取来源；外来导入仅由单独按钮触发，不能把任意路径当作导入来源。每次成功扫描会把来源中已消失且尚未整理的媒体标记为 `missing`，从待整理和番剧媒体列表排除；记录保留，文件重新出现后可恢复。未过稳定期但仍存在的文件不会被误判为消失；已整理的媒体库文件不会因来源移除而被隐藏。此同步在手动点击扫描后生效，自动扫描调度尚未实现。整理会先给出目标与冲突预览，再要求明确确认；不会删除 qB 来源。不要把 `D:\追番` 用于测试。

## 标题识别与整理命名

Anime Vault 从实际下载到本机的媒体文件名解析番剧标题、集数和发布组等信息；RSS 文章标题不作为本地文件名的替代。待整理页面会把文件名解析出的“识别标题”与可编辑的“规范标题”分开显示。用户保存确认的规范标题会成为番剧显示名称，并保留原识别标题作为别名，供后续同标题文件匹配。Mikan RSS 中明确出现中外文并列标题时，程序可以给尚未手动修改、未锁定且未绑定的本地词条添加对应别名和中文规范标题；不会仅凭罗马音自动推断中文名。确认整理后，普通单集文件采用 `规范标题 [01].mkv` 这样的名称（扩展名沿用来源文件），并放入规范标题目录。更改规范标题不会自动移动或重命名已经整理的文件；本版本需手动管理已有文件，或使用将来另行支持的流程调整。

## 编译与启动

需要 Windows 10/11、Visual Studio 2022 C++ 工具集、CMake 3.25+、Git、Node.js/npm 和 vcpkg。`vcpkg.json` 固定 baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d`。在仓库根目录的 PowerShell 中：

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
& "$env:VCPKG_ROOT\bootstrap-vcpkg.bat"
cmake --preset test
cmake --build --preset test
ctest --preset test --output-on-failure
npm.cmd --prefix frontend ci
npm.cmd --prefix frontend test -- --run --configLoader runner
npm.cmd --prefix frontend run lint
npm.cmd --prefix frontend run build -- --configLoader runner
```

日常构建与启动后端无需事先安装 qB 或建立 qB 下载目录。在仓库根目录运行（路径改为自己的 vcpkg 安装目录）：

```powershell
.\build-backend.bat 'C:\path\to\vcpkg'
.\start-backend.bat
```

也可直接运行 `.\build-backend.bat`：若当前环境已有 `VCPKG_ROOT` 就使用它，否则在窗口中提示输入 vcpkg 路径；命令参数优先于已有环境变量。输入仅对本次构建生效，不会永久修改 Windows 环境变量。脚本从自身所在目录运行 `cmake --preset dev` 和 `cmake --build --preset dev`，只构建后端，不自动启动；路径无效或缺少 CMake 时会报错。之后可双击根目录的 `start-backend.bat`，或在 PowerShell 中运行 `.\start-backend.bat`。启动脚本依次查找 `build\dev\backend\Debug`、`build\test\backend\Debug`、`build\debug\backend\Debug`、`build\release\backend\Release` 中的 `anime_vault_server.exe`，在当前窗口前台运行；关闭窗口或按 Ctrl+C 会停止服务。找不到程序时会提示编译命令，不会自动编译。启动脚本继承启动它的进程环境变量，不内置路径覆盖、Bangumi 标识或 qB 凭据；如需配置，先在同一个 PowerShell 窗口设置环境变量再运行脚本。双击启动时，只能继承 Windows 用户/系统环境变量，不能继承另一个 PowerShell 窗口中临时设置的值。

服务仅绑定 `127.0.0.1`，默认端口 `8848`；`http://127.0.0.1:8848/health` 应返回 `status: ok`。可在运行脚本前设置 `ANIME_VAULT_PORT` 改端口，但必须同步修改 `frontend/vite.config.ts` 中 `/api`、`/health` 的代理目标。

另开 PowerShell 启动界面：

```powershell
npm.cmd --prefix frontend run dev
```

打开 Vite 打印的本地地址。浏览器只通过同源 `/api` 和 `/health` 访问本机后端。默认 Bangumi 不启用；若要查询及刮削封面，在启动后端前设置包含开发者 ID 和应用名的 `ANIME_VAULT_BANGUMI_USER_AGENT`，例如 `your-id/AnimeVault/0.1 (Windows)`。Windows 版 Bangumi API 与图片请求遵循当前用户的系统代理设置并验证 HTTPS 证书；它们不经过 qB 的 RSS 代理桥。不要把凭据写进 `.env.example`；程序也不会自动加载 `.env`。

设置页显示实际生效路径和 Bangumi/qB 状态。保存的首选整理方式可用于界面偏好；`scanIntervalSeconds`、`qbWebUiUrl` 仍是预留偏好，不会启动调度或控制 qB 连接。`mpvExecutable` 是本机播放使用的可执行文件绝对路径：在番剧详情页点击某集的“用 mpv 播放”，后端按媒体 ID 校验路径并启动 mpv，不经网页串流；浏览器和后端须位于同一台电脑。当前机器的 `8080` 端口同时有 IPv4 和 IPv6 监听者，只有 IPv6 回环 `http://[::1]:8080` 指向 qB；程序的 qB 客户端固定使用此地址，避免把凭据发给另一个服务。设置 `ANIME_VAULT_QB_USERNAME` 与 `ANIME_VAULT_QB_PASSWORD` 后重启后端，设置页会显示只读连接诊断。不要把凭据写入仓库文件。

在设置页新增 Mikan HTTPS RSS 订阅时，程序等待 qB 首次获取文章，将已有文章标记已读，然后自动建立只作用于该订阅的下载规则。规则不限定画质或字幕，仅下载每集首个可识别资源；方括号集数和 `- 08 -` 形式均可识别，无法识别集数的文章跳过。qB 智能剧集过滤负责同集去重，并禁止 REPACK/PROPER 再下载。目标目录固定为生效的 qB 下载来源目录。若首次获取或规则配置失败，订阅可能已加入 qB 但自动规则未启用，设置页会显示错误；不应直接重复添加同一订阅。原有手动创建关键词规则仍可用，但不具备此自动去重保证；程序不会自动覆盖同名既有规则。

扫描到的实际文件名会自动创建本地番剧词条。若 Bangumi 已配置，完成 qB 来源扫描后会尝试为最多五个尚未绑定且未锁定的词条补充 Bangumi ID、别名和封面；只有唯一且高置信的候选才自动绑定，其余留待人工确认。RSS 文章标题始终不替代实际文件名解析。

封面刮削使用 Bangumi 官方条目 API 提供的图片地址，不抓取 HTML。配置带有开发者 ID 的 `ANIME_VAULT_BANGUMI_USER_AGENT` 后，自动或手动绑定的番剧会尝试把封面缓存到数据目录的 `covers` 子目录；已绑定词条可在详情页点“刮削封面”重试。只接受 Bangumi 图片域名上的 HTTPS JPEG/PNG/WebP，图片上限为 5 MiB。页面通过同源 `/api/covers/...` 读取已缓存封面；抓取失败或 Bangumi 不可用不会改变原封面，也不影响本地媒体文件。未配置 User-Agent 时，已有缓存仍可读取，但不能进行在线刷新。

交付核验见 [临时目录冒烟步骤](docs/disposable-smoke.md)。`compose.yaml` 仍为占位文件，不提供当前 Windows 本地启动方式。
