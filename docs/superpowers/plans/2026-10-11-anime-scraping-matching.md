# 番剧刮削匹配修复计划

目标：修复已复现的 RSS 双语/多语格式和文件名空白、分隔符问题，保留路径、集数和绑定安全边界。

- [x] 固定 18 条随机 RSS 样本和来源；增加 MikanTitle、FilenameParser、QbWebClient 及别名查找回归测试，运行验证失败。
- [x] MikanTitleParser.cpp 分离标题、明确集数与发布标签；兼容括号空白、v2、双语短横线、SxxExx、多语言；新增 parseAll 保留不同语言别名，parse 保持兼容。
- [x] QbWebClient.cpp 消费 parseAll，保持规范化别名冲突过滤。
- [x] FilenameParser.cpp 统一 Unicode 空白和短横线，处理括号空白及紧贴集数分隔符；合集、多集和路径仍拒绝。
- [x] 为 RSS 别名到本地查询加入集成测试，验证空格/标点等价且冲突和季数边界不变。
- [x] CMake 配置/构建、CTest、前端测试、lint、生产构建；独立审查并记录修复前后结果。


追加里程碑（用户已确认）：
- [x] 累计 27、季数解析、人工优先、重复绑定、新文件复用、换绑恢复的失败测试与实现。
- [x] 011 迁移保存 raw/source，按 Bangumi 正片 sort→ep 保存一一映射，API 显式输出来源。
- [x] 分页完整性、歧义、特殊集、离线回退、旧绑定刷新、后台重试与条件更新保护。
- [x] 详情展示本季 02 及原始 27，组件测试验证。
- [x] 迁移与完整前后端验证，最终独立审查。


最终验证（2026-10-11）：
- cmake --preset test：配置成功；cmake --build build/test --config Debug --parallel 4：成功。
- ctest --test-dir build/test -C Debug --output-on-failure：31/31 通过，26.28 秒。
- npm.cmd run lint：通过；npm.cmd test -- --run --maxWorkers=2 --testTimeout=20000：14 文件、83/83 测试通过。
- npm.cmd run build：TypeScript/Vite 成功。保留现有 576 KB chunk 提示；后端保留现有 getenv 编译警告。
- 第一轮并发构建期间小说页面焦点刷新测试遇到 5 秒轮询导致请求次数多一次；未改动该页面，编译结束后完整复跑通过。
- 独立审查指出的特殊集事务检查、已绑定失败恢复及配额饥饿均已补失败回归并修正。旧版显式 Season 标题缺少季字段的绑定复用已补回归。
- 测试仅使用固定网络样本与临时数据库/目录，没有用真实媒体执行整理。

线上转发补修（2026-10-11）：BangumiConnectionManager 未覆盖 episodes，已补转发与红绿回归；真实 API 返回媒体30 episodeNumber=2、rawEpisodeNumber=27、source=bangumi。CMake配置/构建、CTest31/31（24.72秒）、前端83/83、lint/build通过。
