# 播放器选择实施计划

**目标：** 设置页保存默认播放器，番剧详情支持临时覆盖，两者共享后端检测结果。

**结构：** PlayerCatalog 只负责固定播放器类型、可执行文件检测与校验；PlaybackService 保留媒体路径边界并按类型生成独立参数。配置存入现有 setting 键值表，不改变 SQL 表结构。旧 mpv 路径作为兼容回退。

**技术：** C++20、Windows 注册表/App Paths、CreateProcessW/ShellExecuteExW、React。

- [x] 增加 PlaybackServiceTest：VLC 使用单独文件参数，系统默认经过同样媒体路径验证，拒绝未知类型；先运行失败，再实现。
- [x] 新增 PlayerCatalog.hpp/.cpp；检测常见安装目录、App Paths 与 PATH，不递归扫描磁盘。增加固定类型 mpv/potplayer/vlc/mpchc/mpcbe/custom/system。
- [x] 保存 playerType/playerExecutable 到已有 setting 键；旧偏好保存不得清空播放器配置。
- [x] 在 registerPlaybackEndpoint 中增加 GET/PUT /api/players；播放 POST 只接收可选 playerId，不允许临时传任意执行参数或路径。
- [x] 新增 PlayerSettings、PlaybackPlayerSelect，替换 mpv 输入与按钮文案；更新 API 类型和客户端。
- [x] 运行 CMake 配置/构建、CTest；前端相关测试、lint、build；接口冒烟不启动真实播放器。更新 README，重启后端保留运行。

**验收：** 默认设置持久化；临时选择不修改默认；现有 mpv 路径继续可用；未安装、路径非法、文件缺失和越界有稳定错误；系统默认仅打开支持的视频扩展名，不接受脚本或命令。

**验证结果：** CTest 28/28；PlayerSettings、AnimeDetailPage、SettingsPage 测试 26/26；lint 与生产构建通过。真实接口检测到本机 mpv，缺失媒体 404，跨站配置 403。未安装其他播放器，未做真实启动验证；进程启动参数通过录制启动器测试。
