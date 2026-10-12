# 番剧刮削匹配检查与修复设计（已确认）

日期：2026-10-11。

## 调查方法
从 https://mikanani.me/RSS/Classic 的 100 条记录以随机种子 20261011 抽取 18 条，使用当前 C++ MikanTitleParser 与 FilenameParser 运行隔离诊断。未访问用户数据库或媒体，未修改生产代码。
RSS 双语映射提取成功 2/18。此数值不是整体刮削率：单语文章无需映射，RSS 标题也不等于实际文件名，斜杠在磁盘文件名中仍必须拒绝。

## 已复现问题
- 双语映射正则不支持标签间空格、01v2、双语短横线集数及部分多语言格式。
- Mikan 默认把第一名称作为规范名称，ANi 常见英文/中文顺序需要正确选取中文名。
- 文件解析对方括号集数两侧空白、不换行空格和紧贴集数的短横线缺少支持。
- 标题规范化已处理普通空格、大小写、下划线和常见标点，需补充回归测试而非盲目重复处理。

## 推荐方案
1. 保存带来源链接的固定随机样本，离线测试可复现；增加定向格式变体和错误匹配反例。
2. RSS 识别先处理空白、括号和明确集数边界，再拆分双语/多语名称，优先中文规范名并保存可验证的罗马拼音/别名。含糊格式维持不自动绑定。
3. 文件名处理兼容实际空白和分隔符变体，保留路径检查、季数、版本、合集与多集保护，不把 RSS 中的斜杠直接当成合法文件名。
4. 测试覆盖 RSS→别名→本地标题查找及 Bangumi 候选验证；同名多季、短标题和多候选继续拒绝不可靠自动绑定，不降低自动绑定阈值。
5. 报告修复前后各类测试结果；运行 CMake 配置/构建、CTest、前端测试/lint/build。不自动替换已发布安装包。

替代方案：仅扩展现有正则，改动小但多语言和相邻格式容易再次遗漏；使用宽松模糊匹配，匹配率可能升高但错绑风险不可控。推荐上述明确格式解析与严格验证方案。

## 已确认追加：累计集数转换

用户确认采用已绑定 Bangumi 词条的正片分集表 `sort → ep`，不按上一季长度猜测偏移。2026-10-11 核验官方 API `https://api.bgm.tv/v0/episodes?subject_id=548156&type=0&limit=100`：青之箱第二季 sort=26/27 对应 ep=1/2。

- 文件名解析保留原始 27，并提取明确的 `Season 2`；绑定后有效集数为 2，详情页显示 02，整理预览使用有效集数。
- 迁移 011 保存原始集数和来源（parsed/bangumi/manual），映射按词条持久化；重复转换始终从原始值计算，换绑后重新计算。
- 人工修正优先；旧版本有人工别名或集数编辑记录的文件保守视为人工值。旧版已经人工改写过的原始数无法凭迁移恢复。
- 只转换可靠的一一对应正整数正片映射，特殊集保持原值；分页不完整、异常、歧义、零值或网络失败不推算。最多 1000 条/10 页。
- 新扫描可复用已保存映射；旧绑定按扫描刷新，后台轮转补查未映射文件。分集查询成功缓存 15 分钟，失败退避 1 分钟。异步刷新校验当前绑定 ID。
- 保留已整理文件的实际路径，不自动重命名或移动媒体。

## 修复后的随机样本结果
 
18 条固定 RSS 样本中，15 条具有可验证的双语或多语映射，已全部识别，共保留 19 个别名映射；另外 3 条没有明确映射，维持不生成别名。修复前仅识别其中 2 条。这是 RSS 别名提取结果，并非整体 Bangumi 自动绑定率。

## 随机样本与基线
·+-
0咩咩咩咩咩咩咩咩咩咩咩咩咩咩咩咩咩咩咩，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，，
- 【悠哈璃羽字幕社&西农YUI汉化组】[暗芝居 第十七季_Yami Shibai 17][12][x264 1080p][CHT]
  - 来源：https://mikanani.me/Home/Episode/c557191b7aee39b8ddaad98cae1cd8ca64e10898
  - 双语映射：未识别

- [猎户压制部] 擅长逃跑的殿下 第二季 / Nige Jouzu no Wakagimi S2 [24] [1080p] [简日内嵌] [2026年7月番]
  - 来源：https://mikanani.me/Home/Episode/ed30abb908f2569763653074705f014c2e582a4d
  - 双语映射：未识别

- 【悠哈璃羽字幕社&西农YUI汉化组】[暗芝居 第十七季_Yami Shibai 17][12][x264 1080p][CHS]
  - 来源：https://mikanani.me/Home/Episode/db2578e1d72b2d423ac85dc11e81d87e3d2d4047
  - 双语映射：未识别

- [猎户压制部] 擅长逃跑的殿下 第二季 / Nige Jouzu no Wakagimi S2 [24] [1080p] [繁日内嵌] [2026年7月番]
  - 来源：https://mikanani.me/Home/Episode/9ffa1badb7f492ad010655f3b61973691404fc5e
  - 双语映射：未识别

- [Nix-Raws] 至高之力 / 至高之力 / バーテックスフォース / VERTEX FORCE S01E02 [CR WEB-DL 1080p AVC AAC][简繁内封]
  - 来源：https://mikanani.me/Home/Episode/fd19a1186452206b84682695f356220561588899
  - 双语映射：未识别

- [黒ネズミたち] Pop Pap Polters - 02 (ABEMA 1920x1080 AVC AAC MKV)
  - 来源：https://mikanani.me/Home/Episode/d7d8aaf9ab16281731b10ad34e601f12653d09ce
  - 双语映射：未识别

- [黒ネズミたち] 重生者的魔法一定要特别 第二季 / Kikansha no Mahou wa Tokubetsu desu 2nd Season - 13 (ABEMA 1280x720 AVC AAC MKV)
  - 来源：https://mikanani.me/Home/Episode/6affdb4a6f3292089398a16ea0eeb49fe3f1426f
  - 双语映射：未识别

- [LoliHouse] 桃源暗鬼 日光·华严瀑布篇 / TOUGEN ANKI: Nikko Kegon no Taki-hen - 02 [WebRip 1080p HEVC-10bit AAC][简繁内封字幕]
  - 来源：https://mikanani.me/Home/Episode/9a073c5c6185ea0d56d0be5198bd1519cc3db3c8
  - 双语映射：未识别

- [黑白字幕组]铠真传武士骑兵 / Yoroi Shin Den Samurai Troopers Part 2 [20] [WebRip 1080p AVC-8bit AAC][CHS](检索用：魔神坛斗士)
  - 来源：https://mikanani.me/Home/Episode/4a2537ea8baf6780a6729cf05ea2d41cd90236b2
  - 双语映射：未识别

- [桜都字幕组] 不擅交际的未亡人雪女与诅咒戒指 / Hitozukiai ga Nigate na Miboujin no Yukionna-san to Noroi no Yubiwa [01v2][1080P][简体内嵌]
  - 来源：https://mikanani.me/Home/Episode/a5cc9b3c6fb36418a039e506e4221a68c5b02c47
  - 双语映射：未识别

- [黒ネズミたち] 紫禁·御喵房（日配版） / Paws & Palaces (Japanese Dub) - 03 (CR 1920x1080 AVC AAC MKV)
  - 来源：https://mikanani.me/Home/Episode/d01af75f424dc6814a6d4836330fac1caa4dc15d
  - 双语映射：未识别

- [jibaketa合成&音频压制][TVB粤语]安妮的童话 / 安妮·雪莉 / Anne Shirley - 17 [粤语+无字幕][WEB 1920x1080 AVC AAC YUE]
  - 来源：https://mikanani.me/Home/Episode/ff909afd4b51d9683ff5588d4ea6a3349970edac
  - 双语映射：未识别

- [黒ネズミたち] 朱色假面 / Shuiro no Kamen - 01 (ABEMA 1920x1080 AVC AAC MKV)
  - 来源：https://mikanani.me/Home/Episode/8d1b7c5f4bf7e6874d152fb5df31d3b48a4d51f7
  - 双语映射：未识别

- [盲僧压制][迪迦奥特曼][52][致以辉煌的人][BDRip][1080p][日语发音][简体中字]
  - 来源：https://mikanani.me/Home/Episode/be8d9af7cd72caeef5e63c5891424e7156415f49
  - 双语映射：未识别

- [桜都字幕组] 入间同学入魔了 第四季 / Mairimashita! Iruma-kun (2026) [24][1080P][简体内嵌]
  - 来源：https://mikanani.me/Home/Episode/557e1ea3e1b7430426f1217be465728e571eb00e
  - 双语映射：已识别

- 乱马1/2「らんま1/2」Ranma1/2 2024年版 第3期 S03E02 1080p 日英双语-多国字幕
  - 来源：https://mikanani.me/Home/Episode/347a9c57674c6af541349e7aae37d792a68931db
  - 双语映射：未识别

- [桜都字幕组] 入间同学入魔了 第四季 / Mairimashita! Iruma-kun (2026) [24][1080P][繁体内嵌]
  - 来源：https://mikanani.me/Home/Episode/c82300840add74abfb3cfca1092124913b720500
  - 双语映射：已识别

- [ANi] A Tale of the Secret Saint /  转生后的大圣女，极力隐瞒圣女的身分 - 03 [1080P][Baha][WEB-DL][AAC AVC][CHT][MP4]
  - 来源：https://mikanani.me/Home/Episode/d8f338bb29e276eeb33ca0eb0afad2d21278297f
  - 双语映射：未识别

## 当前库四部定向适配（2026-10-11）

根据用户要求优先适配本地未绑定条目9、11、15、17：铁旅繁体发布、药屋第三季新番标签、FX中英拼接、Horemajo短罗马音。只增加一组真实标题回归；不扩大为随机样本或前端测试。

- 可信Mikan单番订阅（RSS/Bangumi?bangumiId，正常feed，明确Mikan Project标题）以订阅名称提供季数及规范译名；聚合/搜索RSS不使用feed名称作绑定依据。
- 从同一文章保留多语、单语发布及中英拼接别名，仍排除多个规范标题争用的别名；发布标签先清理，旧污染标题通过原文显式别名恢复。
- 匹配键补充确定的鐵/铁字形对应，显示原文保持原样；不降低Bangumi自动绑定阈值。
- 仅未绑定、未锁定、无人工别名的条目可接受RSS标题；既有FX词条不自动合并，使用已实现的预览确认流程。
- 官方Bangumi搜索核验目标：铁旅590553、魔女624923、药屋第三季568244；FX已有622288。实际媒体不移动。

单番feed映射额外标注feedScoped并携带文章集数：仅本地正片原始集数一致且明确季数不冲突时采用；同一别名保留各集证据，避免RSS第一条集数与本地不一致而漏配。新增一条集数/季数保护测试，验证第一季不被第三季feed覆盖、原始49且无冲突才允许采用。

最终验证：五个相关CTest目标5/5通过（0.70秒）；追加季数断言后MikanEnricher再次1/1通过（0.19秒）；后端目标构建成功。遵循最小测试原则，本次无前端修改、不重复前端及无关测试。

开发库实际结果：9→590553，11→568244（原始49映射本季1），17→624923；15已纠正为FX战士久留美，但目标622288已由条目10绑定，保留两者，交由页面预览确认合并。开发服务8848已恢复。
