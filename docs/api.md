# HTTP 接口说明

适用版本：0.1.2；接口契约基线：`42d86a2`；核对日期：2026-10-26。完整字段定义见 [OpenAPI 3.0 文件](openapi.json)。本文描述当前实现。

## 连接与通用约定

- 开发环境后端：`http://127.0.0.1:8848`；`ANIME_VAULT_PORT` 可指定 1–65535 的端口。桌面版端口由宿主选择，应使用页面同源地址。
- JSON 请求使用 `Content-Type: application/json`。无请求体的接口不要发送 `{}`；小说／游戏的自动刮削则必须发送 `{}`。
- 当前没有 Bearer token 或用户登录接口。服务只监听回环地址，面向同机界面；不能按远程多用户 API 使用。
- 播放器写入／播放、小说写入／阅读、游戏写入／启动调用 `localRequestAllowed`：拒绝 `Sec-Fetch-Site: cross-site`；允许无 Origin、固定的 `http://localhost` / `http://127.0.0.1` 的 5173、8848 端口，或实际回环监听端口对应 Origin。其余控制器未统一应用此检查，不能假定所有写接口采用相同策略。
- ID 使用 JSON 整数或路径整数；Media 控制器严格要求正 64 位整数。小说／游戏路径由 Drogon 转换为 `int64`，尚未统一负值和格式错误的处理。
- 集数为字符串，例如 `"1"`、`"12.5"`；支持非负 uint32 整数及一位小数，整数部分规范化后移除前导零。不要改为浮点数。
- `episodeType` 可写值：`normal | sp | ova | ncop | nced`；读取时还可能出现 `unknown`。
- 下文字符串长度限制来自 C++ `std::string::size()`，按 UTF-8 字节计算。OpenAPI 中用描述及 `x-max-utf8-bytes` 表达，不将字节限制误写为字符数限制。

### 响应和请求编号

成功响应直接返回对象，不包裹 `data`。一般 JSON 错误格式为：

```json
{
  "error": { "code": "invalid_request", "message": "JSON object required" },
  "requestId": "42"
}
```

| 接口族 | 当前请求编号规则 |
| --- | --- |
| Media 控制器 JSON 接口 | 回显请求的 `X-Request-Id`，未提供时用控制器递增序号；响应头和响应体均携带 |
| qB 配置、Bangumi 配置 | 各自生成递增序号，不回显输入请求 ID |
| 小说、游戏 JSON 接口 | 分别生成 `novel-N`、`game-N` |
| `/health`、qB 状态和 RSS 接口 | 不附请求 ID |
| 成功的二进制封面 | 不附 JSON 请求 ID；番剧封面未命中是空体 404 |

编号在不同控制器、进程之间不保证唯一。Media JSON 响应序列化预检上限 1 MiB，超限返回 500 `response_too_large`；此限制并未统一用于全部控制器。Drogon 自身拒绝的请求可能不是上述 JSON 格式。

HTTP 成功不必然表示业务成功：qB 状态、连接测试、元数据同步、排期过期缓存、小说／游戏刮削都需要检查相应的 `connected`、`success`、`bound`、`stale` 与 `errorCode`。

### 分页与时间

| 接口 | limit 默认 / 范围 | offset 默认 / 范围 | 其他参数 |
| --- | --- | --- | --- |
| `GET /api/scans` | 50 / 1–100 | 0 / 0–1000000 | 无 |
| `GET /api/inbox` | 100 / 1–100 | 0 / 0–1000000 | `origin` 可选，见来源枚举 |
| `GET /api/anime` | 50 / 1–200 | 0 / 0–1000000 | `sort=import_desc`，也可 `import_asc` |
| `GET /api/anime/{id}` | `mediaLimit=50` / 1–200 | `mediaOffset=0` / 0–1000000 | 参数名称与列表不同 |
| `GET /api/audit-logs` | 50 / 1–100 | 0 / 0–1000000 | 可选正整数 `animeId` |

上述分页接口拒绝未知查询参数，`nextOffset: null` 表示结束；仅 inbox 额外返回 `total`。小说、游戏、来源列表不分页。整理预览 `expiresAt` 为 UTC ISO 8601 字符串；合并预览 `expiresAt`、排期 `updatedAt`、搜索失败 `retryAfter` 为 Unix 秒，其中 `retryAfter` 是绝对时刻，不是等待秒数，也未设置为 HTTP `Retry-After` 头。审计 `createdAt` 为数据库原始时间字符串。

## 数据对象

表中 `?` 表示可能不返回该字段；`| null` 表示字段存在但允许为空。精确嵌套结构见 OpenAPI `components.schemas`。

| 对象 | 字段 |
| --- | --- |
| `Scan` | `id, source, status, discoveredCount, processedCount, errorCount, errorSummary`；`source` 为来源枚举，`status` 通常为 `completed / failed`；内部过程可为 `running`。对外 `errorSummary` 为空或 `scan_failed`，不返回原异常路径 |
| `Media` | `id, scanId, sourcePath, filename, parsedTitle, title, season, episodeNumber, rawEpisodeNumber, episodeNumberSource, episodeType, sizeBytes, status, origin, animeId: integer \| null, confidence, bangumiSubjectId?`；`episodeNumberSource=parsed / bangumi / manual` |
| `Anime` | `id, displayTitle, originalTitle, season, year?, bangumiSubjectId?, coverUrl, locked, importedMonth` |
| `AnimeDetail` | `Anime` 加 `aliases: string[], media: Media[], nextMediaOffset: integer \| null` |
| `Page<T>` | `items: T[], nextOffset: integer \| null`；`InboxPage` 加 `total` |
| `FolderImport` | `id, path` |
| `Preview` | `id, targetPath, operation, expiresAt: string, conflicts: string[]` |
| `Execution` | `jobId, targetPath, bytes, status`；成功为 `completed` |
| `AnimeMergePreview` | `previewId, sourceId, targetId, subjectId, sourceTitle, targetTitle, mediaCount, aliasCount, duplicateEpisodeCount, expiresAt: integer` |
| `Settings` | `sourcePath, importPath, libraryPath, dataPath, bangumiConfigured, qbWebUiConfigured, qbDownloadConfigured, qbDownloadDirectory, qbDownloadEnvironmentOverride, preferredOperation, scanIntervalSeconds, mpvExecutable, qbWebUiUrl` |
| `PlayerCatalog` | `selectedId, items: {id, name, executable, available}[]` |
| `QbStatus` | `configured, connected, errorCode, version, torrentCount, completedCount` |
| `QbConfig` | `url, username, source: saved / environment / none, configured`，不返回密码 |
| `BangumiConfig` | `userAgent, source: saved / environment / none, configured` |
| `NovelWork` | `id, title, author, summary, subjectId: integer \| null, manualMetadata, coverUrl, files: NovelFile[]` |
| `NovelFile` | `id, workId, path, label, missing, subjectId: integer \| null, coverUrl` |
| `GameResource` | `id, title, path, developer, summary, platform, subjectId: integer \| null, manualMetadata, missing, coverUrl, vndbId, metadataSource` |
| `NovelScrape` | `bound, coverUpdated, errorCode` |
| `GameScrape` | `bound, coverUpdated, errorCode, source`；来源可为空、`bangumi` 或 `vndb` |

来源枚举为 `qb_download | external_import | folder_import`。`parsedTitle` 保留文件名识别结果，`title` 为规范标题；前端不应通过修改解析接口来写 Bangumi 绑定。视频 `missing` 记录保留但不进入正常媒体列表，小说不可访问卷会被清理，两者生命周期不同。

## 健康与扫描接口

| 方法与路径 | 请求 | 成功响应 | 说明与典型错误 |
| --- | --- | --- | --- |
| `GET /health` | 无 | 200 `{status:"ok", service:"anime-vault", instanceToken?}` | token 仅在启动配置了实例标记时返回 |
| `GET /api/scans` | 分页 | 200 `Page<Scan>` | `invalid_page`、`invalid_request` |
| `POST /api/scans` | 无体、无查询 | 201 `Scan` | 扫描 qB 下载目录；`qb_download_dir_unconfigured`、`source_root_unavailable`、`scan_rate_limited` |
| `POST /api/imports/scan` | 无体、无查询 | 201 `Scan` | 固定外来导入目录；`import_root_unavailable`、`import_root_changed` |
| `GET /api/scans/{id}` | 路径 ID | 200 `Scan` | `invalid_id`、404 `scan_not_found` |
| `GET /api/folder-imports` | 无体、无查询 | 200 `{items: FolderImport[]}` | 已登记的原位置目录 |
| `POST /api/folder-imports` | `{path:string}` | 201 `FolderImport` | 仅登记；已登记同一路径返回原记录，状态仍为 201 |
| `POST /api/folder-imports/{id}/scan` | 无体、无查询 | 201 `Scan` | 404 `folder_import_not_found`、409 `folder_unavailable` |
| `GET /api/inbox` | 分页、可选 `origin` | 200 `InboxPage` | `invalid_origin`、`invalid_page` |
| `POST /api/inbox/{id}/parse` | `{title, season, episodeNumber, episodeType}`，均为字符串 | 200 `Media` | `invalid_correction`、`invalid_title`、`invalid_episode_type`、404 `media_not_found` |
| `PUT /api/media/{id}/episode` | `{episodeNumber, episodeType}` | 200 `Media` | 严格 2 字段，体 ≤1024 字节，无查询；`invalid_episode_edit`、`invalid_episode` |
| `POST /api/enrichment/sync` | 无体、无查询 | 200 `{mikanApplied, bangumiBound, mikanErrorCode}` | 本次普通未绑定补全最多 5 项；Mikan 失败可能仍返回 200 |

文件夹登记的请求体 ≤4096 字节，`path` 1–2048 字节且不含 NUL；必须是存在的绝对非根目录，不得与 qB、固定导入、媒体库或其他登记目录重叠。错误为 400 `invalid_folder_path` 或 409 `overlapping_roots / folder_unavailable`。

标题修正要求 `title` 非空且 ≤200 字节，`season` ≤40 字节，标题可作为安全路径片段，集数必须可解析。明确携带 `bangumiSubjectId` 返回 400 `binding_requires_confirmation`；该旧接口未统一拒绝所有其他额外字段，客户端应只发送上述 4 个字段。

扫描触发接口同步完成本地遍历并返回记录；内部异常会形成 `status:"failed"`，HTTP 仍为 201。qB 与固定导入默认等待文件稳定 60 秒，文件夹扫描立即读取。扫描请求共用 1 秒间隔限制，触发过快返回 **409** `scan_rate_limited`。请求取消不保证后端扫描或刮削停止；扫描返回后可继续刮削，需重新读取列表获取更新。

## 番剧、Bangumi 与封面

| 方法与路径 | 请求 | 成功响应 | 说明与典型错误 |
| --- | --- | --- | --- |
| `GET /api/anime` | 分页、可选 sort | 200 `Page<Anime>` | `invalid_sort`、`invalid_page` |
| `GET /api/anime/{id}` | `mediaLimit, mediaOffset` | 200 `AnimeDetail` | 404 `anime_not_found` |
| `PUT /api/anime/{id}` | `{displayTitle, season, coverUrl}` | 200 `AnimeDetail` | 严格 3 字段，体 ≤8192 字节，无查询；`invalid_anime_edit` |
| `GET /api/anime-calendar` | 无体、无查询 | 200 `{days:[{weekday,items:Anime[]}], fromCache, stale, updatedAt, errorCode}` | weekday 1=周一，7=周日；无可用缓存则 503 |
| `GET /api/bangumi/search` | 唯一参数 `q`，1–500 字节，无体 | 200 `{items:BangumiCandidate[], autoBindEligible, fromCache, localMatch}` | `localMatch` 为空或 `{animeId,displayTitle,bangumiSubjectId?}` |
| `PUT /api/anime/{id}/bangumi` | `{subjectId:positive integer, confirmed:true}` | 200 `AnimeDetail` | 体 ≤4096 字节，无查询；409 `bangumi_subject_in_use` 附 `targetAnimeId,targetTitle` |
| `POST /api/anime/{id}/merge/preview` | `{subjectId:positive integer}` | 200 `AnimeMergePreview` | 目标必须是已占用此 subjectId 的另一词条 |
| `POST /api/anime/{id}/merge` | `{previewId:string, confirmed:true}` | 200 目标 `AnimeDetail` | 凭证为 32 位小写十六进制；过期／变化需重新预览 |
| `POST /api/anime/{id}/cover/refresh` | 无体、无查询 | 200 `{coverUrl}` | 404 `anime_not_found`，409 `cover_subject_unbound`，其他失败通常 503 |
| `GET /api/covers/{animeId}/{subjectId}/{filename}` | 无体、无查询 | 200 图片字节 | 使用返回的 coverUrl；任何未命中／输入错误均空体 404 |

`BangumiCandidate`：`id, name, nameCn, date, coverUrl, episodeCount, type, score`。搜索错误可能附 `retryCount` 与 Unix 秒 `retryAfter`。动画搜索的 429 包括 `bangumi_rate_limited / bangumi_local_rate_limited / bangumi_remote_rate_limited`，队列满为 503 `bangumi_queue_full`，条目类型错误／HTTP 错误通常为 409，其他远端不可用通常为 503。

番剧编辑会保存人工资料并保护人工标题：`displayTitle` 非空、≤200 字节，不能全为可归一化后为空的内容、包含控制字符或 Windows 禁用字符，也不能以空格或句点结尾；`season` ≤40 字节；`coverUrl` ≤2048 字节，允许空字符串、http(s) URL 或 `/api/covers/` 路径，season 和 coverUrl 均不得含控制字符。编辑不自动重命名已整理文件。排期仅展示已绑定且有本地非 missing 媒体的条目，缓存 6 小时；远端失败可返回 7 天内旧缓存，此时 HTTP 200 且 `stale:true`，需显示 `errorCode`。

合并两接口均严格校验字段、体 ≤4096 字节且无查询。预览有效 5 分钟：404 `anime_not_found / merge_target_not_found`，413 `merge_too_large`，其他如 `anime_locked / merge_same_anime / merge_organization_running / merge_preview_expired / merge_target_changed / merge_preview_changed` 为 409。有效期内完成凭证可重试，不会再次合并；合并保留媒体文件，不自动去重删除重复集数文件。

番剧封面成功响应 MIME 为 JPEG / PNG / WebP，带 `Cache-Control: private, max-age=86400` 与 `X-Content-Type-Options: nosniff`。远端抓取有域名、HTTPS、类型和 5 MiB 限制。

## 整理与播放

| 方法与路径 | 请求 | 成功响应 | 说明 |
| --- | --- | --- | --- |
| `POST /api/organize/preview` | `{mediaFileId:integer, operation?:string}` | 201 `Preview` | 默认 hardlink；允许 hardlink / copy / symlink |
| `POST /api/organize/execute` | `{planId:positive integer, idempotencyKey:string, confirmed:true, qbDownloadComplete?:boolean}` | 200 `Execution` | 严格字段白名单，体 ≤4096 字节，无查询 |
| `GET /api/players` | 无体、无查询 | 200 `PlayerCatalog` | 根据本机配置发现可用播放器 |
| `PUT /api/players` | `{playerId:string, executable:string}` | 200 `PlayerCatalog` | 严格 2 字段，体 ≤2048 字节，无查询，本机来源检查 |
| `POST /api/media/{id}/play` | 无体或 `{playerId:string}` | 202 `{mediaId,status:"started",playerId,playerName}` | 体 ≤128 字节，无查询；无体使用默认播放器 |

播放器 ID 为 `mpv | potplayer | vlc | mpchc | mpcbe | system | custom`。`system` 的 executable 必须为空；其他播放器可使用发现路径，自定义路径需为存在的绝对 EXE，长度 ≤1024 字节。不可用播放器返回 400 或 409 `player_not_found`，不应只根据设置保存成功就推断播放已启动。

整理预览有效 15 分钟，`conflicts` 非空时不能执行。预览会核对路径、大小和修改时间。执行必须 `confirmed:true`；qB 来源还须 `qbDownloadComplete:true`，该字段是用户声明，不等于后端核实 qB 完成状态。

`idempotencyKey` 为 1–128 个 ASCII 可见非空白字符（0x21–0x7e）。同一计划重试沿用同一键；新键最多每秒 10 次，超限 429 `execute_rate_limited`。同键跨计划为 409 `idempotency_key_reused`，已完成返回原结果，执行中为 409 `execution_in_progress`。过期、冲突或来源变化时应重新预览。整理保留来源，拒绝覆盖目标。

播放常见错误：400 `invalid_id / invalid_player / invalid_request`；403 `playback_origin_forbidden`；404 `media_not_found / playback_file_missing`；409 `playback_path_outside_root / playback_format_unsupported / player_not_found`；503 `mpv_launch_failed / player_launch_failed`。HTTP 202 仅表示启动请求已交给本机程序。

## 设置与连接配置

| 方法与路径 | 请求 | 成功响应 | 说明 |
| --- | --- | --- | --- |
| `GET /api/settings` | 无体、无查询 | 200 `Settings` | 路径为后端机器路径 |
| `PUT /api/settings` | `{preferredOperation, scanIntervalSeconds, mpvExecutable, qbWebUiUrl}` | 200 `Settings` | 严格 4 字段，体 ≤4096 字节，无查询 |
| `PUT /api/settings/qb-download-directory` | `{path:string}` | 200 `Settings` 加 `restartRequired:true` | 严格 1 字段，体 ≤8192 字节，无查询 |
| `GET /api/audit-logs` | 分页、可选 animeId | 200 `Page<AuditLog>` | `AuditLog={id,action,entityType,entityId,createdAt}` |
| `GET /api/qb/config` | 无体、无查询 | 200 `QbConfig` | 仅返回摘要 |
| `PUT /api/qb/config` | `{url,username,password}` | 200 `QbConfig` | 严格 3 字段，体 ≤2048 字节，无查询；即时生效 |
| `DELETE /api/qb/config` | 无体、无查询 | 200 `QbConfig` | 删除应用保存配置，可能重新使用环境变量 |
| `POST /api/qb/config/test` | 同 qB 配置写入 | 200 `QbStatus` | 测试草稿，不保存；连接失败仍可 200 |
| `GET /api/bangumi/config` | 无体、无查询 | 200 `BangumiConfig` | 已保存值优先于环境变量 |
| `PUT /api/bangumi/config` | `{userAgent:string}` | 200 `BangumiConfig` | 严格 1 字段，体 ≤1024 字节，无查询 |
| `DELETE /api/bangumi/config` | 无体、无查询 | 200 `BangumiConfig` | 清除保存值，可能回退环境变量 |
| `POST /api/bangumi/config/test` | `{userAgent:string}` | 200 `{connected,errorCode}` | 不保存，直接测试远端，不通过搜索缓存 |

通用设置的 `preferredOperation` 为 hardlink / copy / symlink；`scanIntervalSeconds` 为 60–86400 的整数，目前仅保存偏好，未控制 qB 后台轮询；`mpvExecutable` ≤1024 字节，`qbWebUiUrl` ≤2048 字节，不含控制字符。旧 `qbWebUiUrl` 偏好不等同于保存 qB 登录凭据，应使用 `/api/qb/config` 管理连接；播放器选择使用 `/api/players`。

qB 完成任务由应用协调器在后台检查，每轮先等待 10 秒；状态查询最多等待约 25 秒，断线或超时则跳过本轮。它只扫描已完成路径的新增或变更文件，再按 Mikan、Bangumi 顺序补全；没有新增文件时也会在空闲状态重试未绑定条目。RSS 规则计算与网络流程已分离，订阅失败仍沿用原回滚顺序，接口和下载策略不变。

下载目录 `path` ≤4096 字节且不含 NUL，空字符串表示清除；非空必须是存在的绝对非根目录，与 import / library / data 根不重叠。保存不移动文件，需重启使有效来源切换；返回中 `sourcePath`、`qbDownloadConfigured` 仍描述本进程当前生效状态，`qbDownloadDirectory` 是已保存选择。环境变量覆盖时 409 `qb_download_directory_overridden`。

qB URL 当前只接受 `http://127.0.0.1:端口` 或 `http://[::1]:端口`，不接受 localhost、HTTPS、路径或查询，端口 1–65535；用户名非空 ≤128 字节，密码 ≤512 字节，均不得含控制字符。首次保存必须填写密码；已有保存配置时空密码沿用旧密码，即使地址或用户名改变。密码存入当前 Windows 用户凭据管理器，不回传。连接失败查看 `connected:false` 和 `errorCode`；配置错误通常 400，超大请求 413，存储故障 500。

Bangumi User-Agent 要求非空、≤200 字节、不含控制字符，建议按项目现有说明填写开发者身份；HTTP 校验不要求固定格式。无效为 400 `invalid_user_agent`，超大请求 413，存储故障 500。

## qB 与 Mikan RSS

| 方法与路径 | 请求 | 成功响应 | 说明 |
| --- | --- | --- | --- |
| `GET /api/qb/status` | 无必需参数 | 200 `QbStatus` | 未配置／连接失败通过字段报告 |
| `GET /api/qb/rss` | 无必需参数 | 200 `{feedCount,articleCount,pairCount,errorCode,feeds:[{title,articleCount,hasError}]}` | 读取 qB 中 Mikan 标题对照，不直接扫描媒体 |
| `POST /api/qb/rss/feeds` | `{url:string}` | 200 `{success:true,errorCode:""}` | 严格 1 字段，体 ≤2048 字节；添加订阅和自动剧集规则 |
| `POST /api/qb/rss/rules` | `{ruleName,feedUrl,keyword}` | 200 `{success:true,errorCode:""}` | 严格 3 字段，体 ≤4096 字节；旧式关键词规则入口 |

RSS URL 必须为 HTTPS 的 `mikanani.me` 或 `mikanani.kas.pub`（可带 `www.`），路径 `/RSS/Bangumi`、`/RSS/MyBangumi` 或 `/RSS/Search`，URL 总长 ≤1024 字节，查询字符受白名单限制。`ruleName` 1–80 字节、`keyword` 1–120 字节，不含控制字符。

`/feeds` 自动创建每集首个版本的智能过滤规则，开启 qB RSS／自动下载相关偏好，并更新全局智能集数表达式；这些全局偏好也会影响其他智能过滤规则。`/rules` 使用 `mikanRuleJson` 的关键词规则，`useRegex:false`、`smartFilter:false`，**不提供 `/feeds` 的自动集数去重语义**。两入口不能按同一规则模板理解。

未配置有效下载目录或目录需重启返回 409 `qb_download_dir_unconfigured / qb_download_dir_restart_required`；来源不可用也可能 409。无效输入 400 `invalid_mikan_feed_url / invalid_mikan_rule`；已有订阅／规则为 409 `qb_feed_exists_or_invalid / qb_rule_exists`；远端失败一般 503。失败响应可能同时有 `success:false, errorCode` 和 `error`，输入校验失败则未必有 `success` 字段。这组接口没有请求 ID。

## 小说接口

| 方法与路径 | 请求 | 成功响应 | 说明 |
| --- | --- | --- | --- |
| `GET /api/novels` | 无必需参数 | 200 `{items:NovelWork[]}` | 查询会清理不可访问分卷记录 |
| `GET /api/novels/{id}` | 路径 ID | 200 `NovelWork` | 复用全库核对，可能清理其他作品分卷 |
| `PUT /api/novels/{id}` | `{title,author,summary}` | 200 `NovelWork` | 保存人工元数据 |
| `DELETE /api/novels/{id}` | `{confirmed:true}` | 200 `{removedId}` | 仅允许无可读分卷作品，保留磁盘文件及来源登记 |
| `PUT /api/novel-files/{id}` | `{workId:positive integer,label:string}` | 200 目标 `NovelWork` | 修改卷归属及展示名，不移动文件 |
| `GET /api/novel-sources` | 无必需参数 | 200 `{items:[{id,path,directory}]}` | 导入来源登记 |
| `POST /api/novel-imports` | `{path:string}` | 202 `{sourceId,fileCount,scraping,queuedCount}` | 同步导入，后续排队刮削 |
| `GET /api/novel-reader` | 无必需参数 | 200 `{type,executable}` | 默认 system |
| `PUT /api/novel-reader` | `{type:"system"\|"custom",executable:string}` | 200 `{type,executable}` | custom 要求存在的绝对 EXE，system 保存为空路径 |
| `POST /api/novel-files/{id}/read` | 无请求体 | 202 `{started:true}` | 启动本机阅读器 |
| `GET /api/novel-bangumi/search` | `q` 1–200 字节 | 200 `{items:[{id,name,nameCn,platform,series,coverUrl}]}` | 搜索书籍并过滤漫画，失败统一为 503，包括非法查询 |
| `POST /api/novels/{id}/scrape` | `{}` 或 `{subjectId:positive integer}` | 200 `NovelScrape` | 作品自动匹配或手工指定 |
| `POST /api/novel-files/{id}/scrape` | 同上 | 200 `NovelScrape` | 分卷独立绑定 |
| `GET /api/novels/{id}/cover` | 路径 ID | 200 图片字节 | 无封面为 JSON 404 `cover_unavailable` |
| `GET /api/novel-files/{id}/cover` | 路径 ID | 200 图片字节 | `Cache-Control:no-cache`，`nosniff` |

小说写请求使用字段白名单、JSON 对象、24000 字节体上限和本机来源检查；阅读单独要求空体。`path` 1–4096 字节、无 NUL，必须为存在的绝对安全路径，支持 EPUB / TXT / PDF 和递归文件夹，拒绝来源嵌套；超过 20000 文件报错。`title` 与卷 `label` 非空且 ≤600 字节；`author` ≤600 字节、`summary` ≤20000 字节，不含 NUL。

列表与详情逐卷检查文件、读取权限及重解析路径，失败的卷从数据库删除；断盘或临时权限问题也可能触发，作品保留，文件恢复后需重新导入。删除前再次核对，尚有可读卷返回 409 `novel_has_accessible_files`。

一般小说错误：400 `invalid_novel_path / novel_format_unsupported / novel_source_overlap / novel_import_too_large / invalid_novel_metadata / invalid_reader_config / reader_launch_failed`；403 `cross_site_request_forbidden`；404 `novel_not_found / novel_file_not_found`；413 `request_too_large`；500 `novel_storage_error`。刮削异步结果中的 `novel_match_requires_confirmation / novel_scrape_superseded / bangumi_not_novel / cover_*` 等通过 **HTTP 200 的 errorCode** 返回；`bound:true` 不保证 `coverUpdated:true`。

## 游戏接口

| 方法与路径 | 请求 | 成功响应 | 说明 |
| --- | --- | --- | --- |
| `GET /api/games` | 无必需参数 | 200 `{items:GameResource[]}` | 逐项重新核对 EXE 可访问性 |
| `POST /api/game-imports` | `{path:string}` | 201 `GameScrape` 加 `{game:GameResource}` | 先登记，再等待该次刮削回调；刮削失败仍可 201 |
| `PUT /api/games/{id}` | `{title,developer,summary,path}` | 200 `GameResource` | 人工元数据优先；原路径缺失时仍可编辑，换路径须验证 EXE |
| `DELETE /api/games/{id}` | `{confirmed:true}` | 200 `{removedId}` | 仅清理缺失词条，不删除文件 |
| `POST /api/games/{id}/launch` | `{confirmed:true}` | 202 `{started:true}` | 工作目录为 EXE 所在目录 |
| `GET /api/game-bangumi/search` | `q` 1–200 字节 | 200 `{items:[{id,name,nameCn,platform}]}` | 搜索游戏，失败通常 400 |
| `GET /api/game-vndb/search` | `q` 1–200 字节，无 NUL | 200 `{items:[{id:string,name,nameCn,platform}],more:boolean}` | 不暴露翻页参数；more=true 时不自动唯一绑定 |
| `POST /api/games/{id}/scrape` | `{}` 或 `{subjectId:positive integer}` | 200 `GameScrape` | 默认 Bangumi 路径，必要时回退 VNDB；已选 VNDB 来源可能沿用该来源 |
| `POST /api/games/{id}/vndb/scrape` | `{}` 或 `{vndbId:string}` | 200 `GameScrape` | 手工 ID 格式 `^v[1-9][0-9]{0,9}$` |
| `GET /api/games/{id}/cover` | 路径 ID | 200 图片字节 | 无封面当前为 **400** `cover_unavailable`；`no-cache`、`nosniff` |

游戏写请求使用字段白名单、JSON 对象、28000 字节体上限和本机来源检查。`path` 1–4096 字节、不含 NUL；可剥离外层复制引号，要求存在、可读取、有 MZ 头的绝对 EXE，并拒绝各级重解析路径。标题非空且 ≤600 字节，developer ≤600 字节，summary ≤20000 字节，不含 NUL。

重复导入同一路径复用现有记录，但仍返回 201 并尝试刮削；编辑路径与其他条目冲突时 409 `game_path_already_imported`。删除可访问游戏返回 409 `game_is_accessible`；404 `game_not_found`；403 `cross_site_request_forbidden`；413 `request_too_large`；500 `game_storage_error`。启动失败 `game_executable_unavailable / game_launch_failed` 当前为 400。

游戏搜索的远端限流／网络错误也常映射为 400；刮削回调错误通过 HTTP 200 的 `errorCode` 返回，如 `game_match_requires_confirmation / vndb_match_requires_confirmation / invalid_vndb_id / game_scrape_superseded`。不要将动画搜索的状态码映射直接用于小说／游戏。

## 调用示例

以下为手工调用示例，不会由应用自动执行。使用当前后端端口；路径替换为准备好的临时测试目录。

```powershell
$base = 'http://127.0.0.1:8848'
Invoke-RestMethod "$base/health"
Invoke-RestMethod "$base/api/anime?limit=50&offset=0&sort=import_desc"
Invoke-RestMethod "$base/api/inbox?limit=100&offset=0&origin=folder_import"
```

只预览一个已修正条目的整理计划，先检查冲突与目标路径：

```powershell
$mediaId = 123 # 替换为实际条目 ID
$previewBody = @{ mediaFileId = $mediaId; operation = 'copy' } | ConvertTo-Json
$preview = Invoke-RestMethod "$base/api/organize/preview" -Method Post `
  -ContentType 'application/json' -Body $previewBody
$preview | Format-List
```

只有用户已检查无冲突、未过期并明确确认后，才提交执行；qB 来源还需确认下载已完成。执行请求结构如下，重试时复用同一 `idempotencyKey`：

```json
{
  "planId": 456,
  "idempotencyKey": "organize-456-unique-client-key",
  "confirmed": true,
  "qbDownloadComplete": true
}
```

新接口或行为修改时，必须一并更新本说明、OpenAPI 和相应契约测试；保留字段缺失与 null、HTTP 成功内业务失败、异步排队和实际文件操作之间的区别。
