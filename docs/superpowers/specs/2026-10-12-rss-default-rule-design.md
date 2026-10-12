# RSS 默认自动下载规则（2026-10-12）

依据用户提供的策略更新新订阅默认行为：每源一条启用规则；正则加智能过滤；五种格式只捕获数字集数；首个符合条件版本优先；立即开始、零天间隔、关闭 REPACK/PROPER。保存目录沿用生效的 qB 下载来源目录。默认不限定字幕组、平台或简繁。

不再批量标记旧文章已读。启用前读取当前任务，与同源 RSS 文章通过精确标题（可去除视频扩展名）或 magnet hash 关联后提取集数，合并同源单源旧规则中 0–999 的数字历史。无法可靠关联时不凭空推断下载完成。其他源的任务及已读标志不作为证据。已有启用规则或同名规则拒绝重复创建，现存规则与媒体不做批量迁移或删除。

全局过滤精确替换为五项，避免旧季度、日期与分辨率模式干扰。横线分支使用 \x20 保留 qB 扩展模式中的字面空格。手动关键词规则保持用户定制行为。提供默认禁用、不关联源的导入模板。

最小验证：QbWebClient 定向测试、模板正则正反例、前端设置页测试及构建。不得访问真实媒体目录或触发真实下载。

官方实现依据：[智能正则组合与扩展模式](https://github.com/qbittorrent/qBittorrent/blob/master/src/base/rss/rss_autodownloader.cpp)、[集数记录与 REPACK 处理](https://github.com/qbittorrent/qBittorrent/blob/master/src/base/rss/rss_autodownloadrule.cpp)。默认 mustNotContain 显式排除 REPACK，避免首次下载仍接受 REPACK。

验证完成：CMake test 配置与后端构建成功；QbWebClient 21 个用例全部通过；正则普通/扩展模式正反例 28 项通过；SettingsPage 14 个测试通过；前端 lint、TypeScript 与生产构建通过。构建仍有既有 getenv 及 chunk 大小警告。未操作真实 qB 配置、媒体或下载任务；未重新生成安装包。
