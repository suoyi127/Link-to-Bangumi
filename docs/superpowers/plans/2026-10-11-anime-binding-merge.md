# 番剧绑定合并 Implementation Plan

**Goal:** 绑定已有 Bangumi 条目时，允许预览并确认将当前番剧合并到已有番剧。

**Architecture:** 使用独立 SQLite 合并实现文件，预览与确认共享一致性快照。012 迁移保存五分钟有效的预览和完成状态，确保重启、并发变化和重复确认可验证。接口只传 subjectId / previewId，目标由数据库确定。前端在绑定冲突时展示已有目标和合并预览。

**Tech Stack:** C++20、SQLite、Drogon、React/Ant Design、Catch2、Vitest。

### 1. 持久化与领域测试
- [ ] 在 `backend/tests/integration/AnimeBindingTest.cpp` 添加临时数据库合并用例：目标资料不变、人工集数保留、原始27按目标映射变2、媒体ID和路径不变、源删除、旧预览失效、重复确认返回目标；运行 AnimeBinding 看见失败。
- [ ] 在 `MediaRepository.hpp` 定义 `AnimeMergePreview { previewId, sourceId, targetId, subjectId, sourceTitle, targetTitle, mediaCount, aliasCount, duplicateEpisodeCount, expiresAt }`，新增 `previewAnimeMerge(sourceId,subjectId)` / `mergeAnime(sourceId,previewId)`。
- [ ] 新建 `012_anime_merge_preview.sql`，表字段 `id TEXT PRIMARY KEY, source_id INTEGER, target_id INTEGER, subject_id INTEGER, snapshot TEXT, expires_at INTEGER, completed INTEGER`，更新迁移到12、CMake和迁移测试。
- [ ] 新建 `SqliteAnimeMerge.cpp`，使用 SQLite randomblob 生成预览ID，JSON数组记录两个条目、全部媒体和别名的稳定字段快照；预览清理过期记录，五分钟后失效。确认在 BEGIN IMMEDIATE 中重新验证，拒绝锁定源/运行中整理、变化的快照；批量转移媒体及别名并重算非人工集数，删除源，记录审计，完成预览。所有步骤在同一事务。
- [ ] 添加预览后媒体增加、目标换绑、人工编辑、事务失败回滚、锁定及重复集数保留回归，再运行 `ctest --test-dir build/test -C Debug -R 'AnimeBinding|SqliteRepository|OrganizationService' --output-on-failure`。

### 2. HTTP 交互
- [ ] 在 `OrganizationHttpTest.cpp` 添加合并DTO测试及已有本机HTTP服务中的预览、取消无副作用、确认、无确认拒绝和过期冲突用例。
- [ ] `MediaController.hpp/.cpp` 定义预览请求 `{subjectId}` 和确认请求 `{previewId,confirmed:true}`，拒绝未知字段、无效ID、过大请求体、查询参数；端点为 POST `/api/anime/{id}/merge/preview` 与 POST `/api/anime/{id}/merge`。所有稳定错误经 ApiError 映射。
- [ ] 绑定409错误增加 `targetAnimeId` 与 `targetTitle`，由实际占用词条的条目生成；在API客户端 ApiError 中保留这两个字段。
- [ ] 返回显式预览JSON与合并后的 AnimeDetail，运行 OrganizationHttp 和前端 client 测试。

### 3. 前端
- [ ] `AnimeDetailPage.test.tsx` 先覆盖绑定冲突出现合并入口、预览取消不写入、确认后跳转目标、旧详情回包不生效。
- [ ] `types.ts/client.ts` 增加 `AnimeMergePreview`、`previewAnimeMerge`、`mergeAnime`；详情组件记录冲突及预览状态，展示目标、媒体/别名/重复集数数量和数据保留规则，确认按钮标识“确认合并”。
- [ ] `App.tsx` 给详情组件传 `onOpenAnime={setAnimeId}`，确认成功跳转已有番剧；切换条目时清空所有合并状态，异步结果按generation检查。
- [ ] 运行 `npm.cmd test -- --run src/components/AnimeDetailPage.test.tsx src/api/client.test.ts --maxWorkers=2 --testTimeout=20000`。

### 4. 最终验证与审查
- [ ] CMake配置/完整构建、CTest、前端lint/全部测试/生产构建；只用临时目录做文件测试。
- [ ] 独立审查事务、外键、快照变化、人工保护与前端异步状态；修复后重跑受影响验证，恢复开发服务并记录结果。

## 执行结果与测试范围调整

用户追加要求采用最小测试原则，因此合并HTTP关键验证复用ManagementApi现有临时数据库服务；前端只增加一条冲突→预览→确认→跳转交互测试，不新增重复DTO、客户端或异步测试，也不重跑无关前端测试。

已完成持久化事务、012迁移、预览/确认API、冲突目标信息、前端合并入口及跳转。审查发现的ID复用和跨季别名遗漏已修复：身份高水位防止旧回包命中新条目，人工确认的合并别名保留user优先并参与唯一候选回退。

验证：CMake配置及完整构建成功；CTest 31/31通过（25.44秒）；前端详情15/15通过、lint及生产构建成功。最终别名来源修复后仅重跑AnimeBinding（1/1，1.62秒），重新链接服务成功。生产构建仍有既有576KB分块提示。开发后端恢复8848；未执行真实资料库合并，需页面预览确认。
