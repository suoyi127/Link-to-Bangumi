# 小说库与 Bangumi 刮削实施计划

> 使用 executing-plans 在当前隔离工作区逐项实施；不自动提交 Git。

**Goal:** 小说按作品聚合，卷文件保留独立绑定；资源库编辑，设置页导入和配置外部阅读器。

**Architecture:** 独立 NovelService 处理 SQLite、原位扫描和阅读；NovelBangumiService 处理书籍搜索、匹配、绑定和封面。NovelController 仅负责 DTO 校验与序列化。前端复用现有简洁页面样式。

**Tech Stack:** C++20 / SQLite / Drogon / React / TypeScript / Ant Design。

## 验收边界

- EPUB、TXT、PDF 原位导入，文件夹递归扫描；不复制、移动或删除原文件，不跟随链接或重解析点。
- 同名作品聚合；识别末尾卷号并保留每个文件。资源库可修改作品名、作者、简介、文件所属作品及卷名。
- 默认使用系统文件关联阅读，也可配置任意本机阅读器 EXE；直接参数数组启动，不执行命令文本。
- Bangumi 搜索限定书籍，小说分类明确且名称唯一精确匹配才自动绑定；未知分类可人工确认，明确漫画拒绝。
- 原名、中文名、别名均可匹配；没有中文名时保留原名。系列和卷独立绑定，不静默丢弃卷号。
- 封面仅使用现有 Bangumi 图片域名白名单并检验图像签名，存 SQLite BLOB，绑定改变后不发布过时结果。
- 导入自动启动刮削，失败不影响本地浏览；资源库提供重新刮削和手动绑定。

## 实施任务

- [x] 先写 `backend/tests/integration/NovelServiceTest.cpp`，验证临时目录导入、去重、丢失文件、聚合、阅读器参数和书籍匹配。
- [x] 新增 `008_novel_library.sql`，在 CMake / SqliteDatabase / InitialSql 更新版本至 8；所有旧版本断言随迁移更新。
- [x] 新增 `services/NovelService.hpp/.cpp`，实现作品、文件、来源、阅读器设置和安全扫描。
- [x] 新增 `services/NovelBangumiService.hpp/.cpp`，扩展 BangumiTransport 的书籍搜索但保持动画接口行为；批量请求串行排队。
- [x] 新增 `api/NovelController.hpp/.cpp` 并在 main 组装；所有写接口拒绝跨站请求，严格限定字段和正文大小。
- [x] 新增前端 NovelLibraryPage / NovelResources / NovelSettings 与类型、客户端方法；App 增加导航，InboxPage 增加小说资源选项。
- [x] 前端 3 个小说测试验证卷详情、缺失禁读、导入、阅读器保存和元数据编辑；实际页面确认空态；相关旧页面回归共 33 项通过。
- [x] 配置、构建、CTest、前端必要测试、lint、build；重启指定后端与前端并做健康及新 API 检查；更新 ToUser 使用说明。

## 交付验证记录

2026-09-30：CTest 29/29 通过（小说套件 7 个用例）；相关前端测试 33/33，lint、生产构建通过。
隔离服务在 18849 测试，`狼と香辛料 Vol.01.txt` 自动绑定 870，标题转换“狼与香辛料”，作者“支倉凍砂”，本地 JPEG 封面 142744 字节。跨站写请求返回 403，多余字段返回 400。
真实媒体未用于测试；隔离服务已停止。正式服务 8848、前端 5173 保持运行；当前工作区保留，未提交或推送 Git。

## 必要命令

```powershell
$env:VCPKG_ROOT='D:\av-build-cache\vcpkg'
cmake --preset test
cmake --build build/test --config Debug -j 4
ctest --test-dir build/test -C Debug --output-on-failure
cd frontend
npm.cmd test -- --run --maxWorkers=1 --testTimeout=20000
npm.cmd run lint
npm.cmd run build
```

预期测试零失败；首次新增测试须先看到缺少小说接口导致的失败，再实现。文件测试只用临时目录，不接触真实媒体。
