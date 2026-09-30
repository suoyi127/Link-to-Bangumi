# Link to Bangumi（Anime Vault，Windows 本地版）

目前交付范围以阶段 0–5 为基础：扫描 qB 下载目录及独立的外来导入目录、修正解析、浏览本地番剧、查询与绑定 Bangumi、预览并确认整理，以及设置与审计。现已支持读取 qB Web API 状态与 Mikan RSS 文章、用双语标题建立本地别名，以及通过设置页添加 Mikan 订阅并自动建立按集去重的 qB 下载规则。番剧详情页可选择本机播放器播放；自动扫描调度与清理仍未实现。qB 来源的“下载完成”目前仍由用户在执行整理时单独确认；文件稳定性扫描不等于 qB 完成证明。

## 目录与安全边界

默认目录（qB 来源由用户在设置页选择；未配置也可启动）：

| 用途 | 默认路径 | 环境变量 |
| --- | --- | --- |
| qB 下载来源 | 未配置，须在设置页选择已有目录 | `ANIME_VAULT_SOURCE_DIR` |
| 外来导入暂存（程序自动创建，初始为空） | `<项目或安装目录>\Media\Import` | `ANIME_VAULT_HOME`、`ANIME_VAULT_IMPORT_DIR` |
| 整理目标媒体库 | `<项目或安装目录>\Media\Library` | `ANIME_VAULT_HOME`、`ANIME_VAULT_LIBRARY_DIR` |
| SQLite 数据 | `%LOCALAPPDATA%\AnimeVault` | `ANIME_VAULT_DATA_DIR` |

开发启动脚本将“项目目录”设为仓库根目录；安装版使用安装时选择的应用目录，因此媒体文件夹会跟随安装位置，而不再创建到用户的 `Videos` 目录。安装器仅为 `Media\Import` 和 `Media\Library` 授予普通用户写权限。数据库、封面缓存和 WebView2 用户配置仍放在 `%LOCALAPPDATA%`；qB 下载来源始终由用户在设置页单独选择。

首次使用时，程序会在项目/安装目录下创建 `Media\Import` 与 `Media\Library`。在设置页填写已有的 qB 下载目录并保存，然后重启后端；更改目录不会移动或删除原文件。未配置或所选目录不可用时，qB 扫描与 Mikan 自动下载创建不可用，外来导入和本地浏览仍可使用。

qB Web UI 连接也可直接在设置页配置：填写 `http://[::1]:8080`（或 qB 实际监听的 `127.0.0.1` 本机端口）、用户名和密码，先点“测试 qB 连接”，再保存即可立即生效，不需要重启。密码保存在当前 Windows 用户的凭据管理器中，页面不会回显；修改地址或用户名时留空密码会沿用已保存的密码。“清除 qB 配置”只移除 Anime Vault 保存的连接凭据，不修改 qB 设置或下载任务。若未保存页面配置，旧的 `ANIME_VAULT_QB_USERNAME`、`ANIME_VAULT_QB_PASSWORD` 启动环境变量仍可作为回退；页面保存的配置优先。

从旧版 `D:\追番` 布局升级时，如需继续使用原 SQLite 数据，可在启动前显式设置 `ANIME_VAULT_DATA_DIR` 指向旧数据目录；开发者设置的 `ANIME_VAULT_SOURCE_DIR` 会覆盖设置页保存的 qB 下载目录，此时设置页禁止改写该目录。要切换到页面配置，先移除该环境变量并重启。

来源、导入和媒体库目录必须相互独立且不嵌套。扫描只读取来源；固定外来导入目录与用户登记的文件夹分别由各自的按钮扫描，任意路径须先经过登记与校验。每次成功扫描会把来源中已消失且尚未整理的媒体标记为 `missing`，从资源库和番剧媒体列表排除；记录保留，文件重新出现后可恢复。未过稳定期但仍存在的文件不会被误判为消失；已整理的媒体库文件不会因来源移除而被隐藏。此同步在手动点击扫描后生效，自动扫描调度尚未实现。整理会先给出目标与冲突预览，再要求明确确认；不会删除 qB 来源。不要把 `D:\追番` 用于测试。

## 标题识别与整理命名

番剧库上方可切换“全部番剧”和“每周排期”。全部番剧保留原来的海报/列表浏览；每周排期按星期一至星期日展示 [Bangumi 每日放送表](https://github.com/bangumi/api/blob/master/open-api/api.yml) 中同时存在于本地媒体库的番剧，通过已绑定的 Bangumi ID 匹配，不受当前分页限制。未绑定、没有本地媒体或已标记缺失的词条不进入排期。排期缓存六小时，联网失败时可使用七天内的缓存并显示提示；这是放送日安排，不代表文件下载完成或准确播放时间。

游戏库可在页面顶部直接导入本机游戏 EXE，自动刮削 Bangumi 游戏信息并从详情页启动。标题、简介、启动路径和条目绑定在「资源库 → 游戏资源」整理。详细操作见[游戏库使用方法](docs/game-library.md)。

游戏刮削以 Bangumi 为首选，自动匹配失败时使用 VNDB 作为视觉小说备用来源。游戏资源页可切换来源、搜索候选或手动绑定 VNDB ID。

Anime Vault 从实际下载到本机的媒体文件名解析番剧标题、集数和发布组等信息；RSS 文章标题不作为本地文件名的替代。资源库页面会把文件名解析出的“识别标题”与可编辑的“规范标题”分开显示。用户保存确认的规范标题会成为番剧显示名称，并保留原识别标题作为别名，供后续同标题文件匹配。Mikan RSS 中明确出现中外文并列标题时，程序可以给尚未手动修改、未锁定且未绑定的本地词条添加对应别名和中文规范标题；不会仅凭罗马音自动推断中文名。确认整理后，普通单集文件采用 `规范标题 [01].mkv` 这样的名称（扩展名沿用来源文件），并放入规范标题目录。更改规范标题不会自动移动或重命名已经整理的文件；本版本需手动管理已有文件，或使用将来另行支持的流程调整。

资源库有“qB 下载”“外来导入”“文件夹导入”三个并列来源。文件夹导入用于本机已有目录：粘贴绝对路径并添加，再点击该目录的“扫描目录”；程序只登记和读取原文件，不复制、移动或删除它。扫描完成后会先应用已有 Mikan 双语别名，再针对本次扫描到且未锁定、未绑定的番剧尝试 Bangumi 匹配与封面缓存；不可靠的匹配仍留给用户确认。文件名有明确标题但没有集数时也可建立待刮削词条，集数仍为空，整理前须由用户填写；纯数字文件名不自动建立番剧词条。添加时拒绝磁盘根目录，以及与 qB 下载、固定外来导入、媒体库或其他已登记文件夹重叠的目录。重新扫描只更新该目录对应的文件状态；目录丢失或被替换时会报错，不把其他来源误标为缺失。浏览器出于安全限制无法直接提供所选文件夹的绝对路径，网页版本需从资源管理器复制路径；整理仍需预览并确认。

## 启动流程

如果只是使用软件，选择安装版；如果需要修改源码，选择开发启动。所有可复制的 PowerShell 指令集中在 [PowerShell 命令文件](docs/powershell-commands.md)，下方每一步都链接到对应指令。

### 普通用户：下载安装版

1. 打开 [GitHub Releases](https://github.com/suoyi127/Link-to-Bangumi/releases)，选择所需版本并下载 `Setup.exe`。
2. 运行安装包，选择安装目录，按向导完成安装。缺少 WebView2 时，安装过程需要联网下载运行库。
3. 从开始菜单打开 **Anime Vault**，应用会自动启动后端并显示界面。
4. 在“设置”中配置 Bangumi 和 qB Web UI；需要 qB 下载时再选择自己的下载目录，保存后按页面提示重启应用。

安装版无需 PowerShell 或开发工具。`Media\Import` 和 `Media\Library` 跟随安装目录，数据库和封面缓存位于 `%LOCALAPPDATA%\AnimeVault`。卸载不会删除用户媒体或该数据目录。安装包尚未签名，Windows 可能显示 SmartScreen 提示，请核对下载来源。

### 开发者：一次启动前后端（推荐）

开始前准备 Windows 10/11、Visual Studio 2022 的 C++ 工具集、CMake 3.25+、Git、Node.js/npm 和 vcpkg。`VCPKG_ROOT` 指向 vcpkg 自身的安装文件夹，例如 `D:\av-build-cache\vcpkg`。

1. 按 [获取源码与进入项目目录](docs/powershell-commands.md#source) 操作。项目根目录是包含 `README.md` 和 `dev.ps1` 的文件夹，后续指令在这里执行。
2. 首次使用 vcpkg 时，执行 [准备 vcpkg](docs/powershell-commands.md#vcpkg)。已有可用安装可跳过这一步。
3. 执行 [同时启动前后端](docs/powershell-commands.md#development)。`dev.ps1` 会构建 Debug 后端、安装缺少的前端依赖，并启动两个服务。首次构建需要下载依赖，耗时较长。
4. 等窗口打印访问地址后，打开 [前端页面](http://127.0.0.1:5173/)。保留启动窗口；按 Ctrl+C 结束本次启动的两个进程。

默认网页端口为 **5173**；后端端口为 **8848**，可通过 [健康检查地址](http://127.0.0.1:8848/health) 查看服务状态。开发模式的网页由 Vite 提供，访问后端根地址不会打开前端。端口被占用时，可按 [更换开发端口](docs/powershell-commands.md#ports) 操作。

### 需要分别构建和启动时

1. 在第一个 PowerShell 窗口，执行 [构建后端](docs/powershell-commands.md#backend-build)，等待构建成功。
2. 在同一窗口执行 [启动后端](docs/powershell-commands.md#backend-start)，并保留窗口。已有构建产物时，可直接双击根目录的 `start-backend.bat`。
3. 另开一个 PowerShell 窗口，进入相同项目目录，执行 [启动前端](docs/powershell-commands.md#frontend-start)。
4. 打开前端窗口打印的地址，默认是 [http://127.0.0.1:5173/](http://127.0.0.1:5173/)。结束使用时，在两个窗口分别按 Ctrl+C。

`build-backend.bat` 负责构建，`start-backend.bat` 负责运行已构建的后端。两条指令应分行执行。启动脚本找不到程序时会提示先构建；它优先使用 `build\dev` 中的程序，再查找其他构建目录。单独启动时的端口与环境变量配置见 [单独启动时更换端口](docs/powershell-commands.md#separate-ports)。

### 维护者：生成桌面程序和安装包

打包前需完成开发环境准备，并安装 **.NET 10 SDK**。生成安装包还需安装 **Inno Setup 7**。

1. 执行 [生成桌面发布目录](docs/powershell-commands.md#desktop-package)。脚本会打印本次输出目录，位于项目的 `build\package` 下。
2. 打开输出目录中的 `AnimeVault.exe` 检查桌面程序。分发便携版时保留整个目录，包含其中的 `backend` 和 `web`；用户电脑需要 WebView2 Runtime 和 Visual C++ x64 Redistributable，.NET 运行时已随包携带。
3. 将上一步的实际目录填入 [生成安装包](docs/powershell-commands.md#installer) 指令。产物位于 `build\installer\AnimeVault-<时间戳>\Setup.exe`，安装器包含微软运行库安装程序。

每次打包均生成新目录。安装包脚本只接受本仓库 `build\package` 下的发布目录，并验证下载的微软安装程序签名。推送版本标签会触发 GitHub Actions 构建、测试和上传安装包，操作指令见 [发布版本](docs/powershell-commands.md#release)。

源码交付前的检查指令见 [构建与测试](docs/powershell-commands.md#verification)，文件操作的手动验证步骤见 [临时目录冒烟步骤](docs/disposable-smoke.md)。

## 启动后的配置与使用

浏览器通过同源 `/api` 和 `/health` 访问本机后端。初次可在“设置 → Bangumi 连接”中填写并测试 User-Agent，默认建议值为 `suoyi127/Link-to-Bangumi/0.1 (Windows) (https://github.com/suoyi127/Link-to-Bangumi)`；保存后搜索、绑定与封面请求立即使用它。也可在启动后端前设置 `ANIME_VAULT_BANGUMI_USER_AGENT` 作为回退值；页面保存配置优先，清除后恢复环境值。当前仅使用公开接口，不需要个人 Token。Windows 版 Bangumi API 与图片请求遵循当前用户的系统代理设置并验证 HTTPS 证书；它们不经过 qB 的 RSS 代理桥。不要把凭据写进 `.env.example`；程序也不会自动加载 `.env`。

设置页显示实际生效路径和 Bangumi/qB 状态。保存的首选整理方式可用于界面偏好；`scanIntervalSeconds`、`qbWebUiUrl` 仍是预留偏好，不会启动调度或控制 qB 连接。不要把凭据写入仓库文件。

“设置 → 本机播放器”替代原 mpv 路径输入：可选择 mpv、PotPlayer、VLC、MPC-HC、MPC-BE、系统默认播放器或自定义播放器。程序检测常见安装目录、Windows App Paths 与 PATH；未检测到的安装版或便携版可填写已有 `.exe` 的绝对路径，再点“保存默认播放器”。旧 `mpvExecutable` 自动作为 mpv 的路径回退。切换默认播放器后，其他选项使用自动检测结果；便携版需在成为默认选项时重新指定路径。不自动下载安装软件。

在番剧详情的“本次播放使用”中可临时选择已检测到的播放器，再点击对应媒体的“播放”；临时选择不会修改默认设置，切换番剧后恢复默认。自定义播放器须支持接收单个媒体文件路径；mpv 仍启用退出时保存播放位置。系统默认使用 [Windows 文件关联](https://learn.microsoft.com/zh-cn/windows/win32/shell/launch)，没有视频关联时启动会失败。所有方式都先检查媒体 ID、文件存在性、来源目录边界和视频扩展名；不接受网页传入的任意命令行，不经网页串流，浏览器和后端须位于同一台电脑。

在设置页新增 Mikan HTTPS RSS 订阅时，程序等待 qB 首次获取文章，将已有文章标记已读，然后自动建立只作用于该订阅的下载规则。规则不限定画质或字幕，仅下载每集首个可识别资源；方括号集数和 `- 08 -` 形式均可识别，无法识别集数的文章跳过。qB 智能剧集过滤负责同集去重，并禁止 REPACK/PROPER 再下载。目标目录固定为生效的 qB 下载来源目录。若首次获取或规则配置失败，订阅可能已加入 qB 但自动规则未启用，设置页会显示错误；不应直接重复添加同一订阅。原有手动创建关键词规则仍可用，但不具备此自动去重保证；程序不会自动覆盖同名既有规则。

扫描到的实际文件名会自动创建本地番剧词条。自动刮削保留两条路径：先用 Mikan RSS 的双语标题对照补充中文名，再对未绑定词条尝试 Bangumi 标题搜索；普通搜索没有可靠结果时，可通过 Bangumi 的别名搜索找到唯一候选，并读取条目自身的别名核验罗马音标题。核验通过后会绑定 ID、保存别名和封面，并给尚未整理的词条补中文展示标题；用户设置的标题及 Mikan 已确定的标题不会被覆盖。每次最多处理五个未绑定且未锁定的词条，多候选或别名不符时留待人工确认。RSS 文章标题始终不替代实际文件名解析。

封面刮削使用 Bangumi 官方条目 API 提供的图片地址，不抓取 HTML。在设置页保存带有开发者 ID 的 User-Agent（或设置 `ANIME_VAULT_BANGUMI_USER_AGENT` 环境变量）后，自动或手动绑定的番剧会尝试把封面缓存到数据目录的 `covers` 子目录；已绑定词条可在详情页点“刮削封面”重试。只接受 Bangumi 图片域名上的 HTTPS JPEG/PNG/WebP，图片上限为 5 MiB。页面通过同源 `/api/covers/...` 读取已缓存封面；抓取失败或 Bangumi 不可用不会改变原封面，也不影响本地媒体文件。未配置 User-Agent 时，已有缓存仍可读取，但不能进行在线刷新。

交付核验见 [临时目录冒烟步骤](docs/disposable-smoke.md)。`compose.yaml` 仍为占位文件，不提供当前 Windows 本地启动方式。
