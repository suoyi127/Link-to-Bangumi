# 媒体库布局调整实施计划

- [x] 为 LibraryPage.test.tsx 增加网格与列表月份分组、未知月份测试；为 SettingsPage.test.tsx 增加默认收起和展开收起测试，验证失败。
- [x] LibraryPage.tsx 按 importedMonth 分组，复用现有网格与表格。
- [x] LibraryCalendar.tsx 与 styles.css 改为单列星期区域及小卡片自动换行。
- [x] SettingsPage.tsx 使用 details/summary 折叠订阅列表。
- [x] 定向测试 21/21；lint、生产构建通过。
- [x] CMake 配置、构建、CTest 31/31 通过。
- [x] 全量前端测试 82/82（14 个文件）通过；差异检查通过，独立只读审查未发现阻碍交付问题。
