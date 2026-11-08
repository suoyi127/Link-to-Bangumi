 find_package(Drogon CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(lexbor CONFIG REQUIRED)
# 按行嵌入字典，离线运行时不依赖外部文件或系统语言设置。
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
foreach(dictionary TSCharacters TSPhrases)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencc/${dictionary}.txt")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
  file(READ "${source}" rows)
  string(REPLACE "\r" "" rows "${rows}")
  string(STRIP "${rows}" rows)
  string(REPLACE "\n" "\",\n\"" rows "${rows}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/${dictionary}.inc" "\"${rows}\"\n")
endforeach()
add_library(anime_vault_chinese STATIC src/services/MikanChinese.cpp)
target_include_directories(anime_vault_chinese PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_features(anime_vault_chinese PUBLIC cxx_std_20)
anime_vault_apply_warnings(anime_vault_chinese)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/001_initial.sql" ANIME_VAULT_INITIAL_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/002_media_correction.sql" ANIME_VAULT_CORRECTION_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/003_source_snapshot.sql" ANIME_VAULT_SOURCE_SNAPSHOT_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/004_media_origin.sql" ANIME_VAULT_MEDIA_ORIGIN_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/005_organization_execution.sql" ANIME_VAULT_ORGANIZATION_EXECUTION_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/006_parsed_title.sql" ANIME_VAULT_PARSED_TITLE_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/007_folder_import.sql" ANIME_VAULT_FOLDER_IMPORT_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/008_novel_library.sql" ANIME_VAULT_NOVEL_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/009_game_library.sql" ANIME_VAULT_GAME_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/010_game_vndb.sql" ANIME_VAULT_GAME_VNDB_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/011_episode_number_mapping.sql" ANIME_VAULT_EPISODE_MAPPING_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/012_anime_merge_preview.sql" ANIME_VAULT_ANIME_MERGE_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/013_game_steam.sql" ANIME_VAULT_GAME_STEAM_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/014_mikan_download.sql" ANIME_VAULT_MIKAN_DOWNLOAD_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/015_mikan_subtitle.sql" ANIME_VAULT_MIKAN_SUBTITLE_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/016_game_import_alias.sql" ANIME_VAULT_GAME_IMPORT_ALIAS_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/017_mikan_rule_sync.sql" ANIME_VAULT_MIKAN_RULE_SYNC_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/018_watch_progress.sql" ANIME_VAULT_WATCH_PROGRESS_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/019_subscription_collection.sql" ANIME_VAULT_SUBSCRIPTION_COLLECTION_SQL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/001_initial.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/002_media_correction.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/003_source_snapshot.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/004_media_origin.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/005_organization_execution.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/006_parsed_title.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/007_folder_import.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/008_novel_library.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/009_game_library.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/010_game_vndb.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/011_episode_number_mapping.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/012_anime_merge_preview.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/013_game_steam.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/014_mikan_download.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/015_mikan_subtitle.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/016_game_import_alias.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/017_mikan_rule_sync.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/018_watch_progress.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/019_subscription_collection.sql")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure/database/InitialSql.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/InitialSql.hpp" @ONLY)

add_library(anime_vault_storage STATIC
  src/infrastructure/database/SqliteNovelRepository.cpp
  src/infrastructure/database/SqliteGameRepository.cpp
  src/infrastructure/database/MikanDownloadStore.cpp
  src/infrastructure/database/WatchProgressStore.cpp
  src/infrastructure/database/SqliteDatabase.cpp
  src/infrastructure/database/SqliteMediaRepository.cpp
  src/infrastructure/database/SqliteScanRepository.cpp
  src/infrastructure/database/SqliteOrganizationRepository.cpp
  src/infrastructure/database/SqliteSettingsRepository.cpp
  src/infrastructure/database/SqliteMetadataCache.cpp
  src/infrastructure/database/SqliteAnimeRepository.cpp
  src/infrastructure/database/SqliteAnimeMerge.cpp
)
target_include_directories(anime_vault_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(anime_vault_storage PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_features(anime_vault_storage PUBLIC cxx_std_20)
target_link_libraries(anime_vault_storage PUBLIC unofficial::sqlite3::sqlite3 PRIVATE anime_vault_chinese nlohmann_json::nlohmann_json)
anime_vault_apply_warnings(anime_vault_storage)
anime_vault_apply_sanitizers(anime_vault_storage)
anime_vault_apply_lto(anime_vault_storage)

add_library(anime_vault_core STATIC
  src/services/NovelService.cpp
  src/services/GameService.cpp
  src/services/SteamLibrary.cpp
  src/infrastructure/NativeLibraryFileAccess.cpp
  src/Core.cpp
  src/application/MediaService.cpp
  src/application/QbCompletedScanCoordinator.cpp
  src/domain/EpisodeNumber.cpp
  src/services/FilenameParser.cpp
  src/services/QbRssRules.cpp
  src/services/MikanDownloads.cpp
  src/services/MikanSubjectLink.cpp
  src/services/MikanTitleParser.cpp
  src/services/DirectoryScanner.cpp
  src/services/TargetPlanner.cpp
  src/services/OrganizationExecutor.cpp
  src/services/OrganizationService.cpp
  src/services/BangumiMatcher.cpp
  src/services/PlaybackService.cpp
  src/services/MpvProgressMonitor.cpp
  src/services/PlayerCatalog.cpp
  src/services/RuntimePaths.cpp
  src/services/RuntimeWebRoot.cpp
  src/infrastructure/ProcessLauncher.cpp
)
target_include_directories(anime_vault_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(anime_vault_core PUBLIC cxx_std_20)
target_link_libraries(anime_vault_core PRIVATE anime_vault_chinese anime_vault_storage nlohmann_json::nlohmann_json Threads::Threads)
if(TARGET lexbor::lexbor)
  target_link_libraries(anime_vault_core PRIVATE lexbor::lexbor)
else()
  target_link_libraries(anime_vault_core PRIVATE lexbor::lexbor_static)
endif()
if(WIN32)
  target_link_libraries(anime_vault_core PRIVATE shell32 ole32 Advapi32)
endif()
anime_vault_apply_warnings(anime_vault_core)
anime_vault_apply_sanitizers(anime_vault_core)
anime_vault_apply_lto(anime_vault_core)

add_library(anime_vault_bangumi_service STATIC src/services/BangumiService.cpp src/services/BangumiCalendarService.cpp src/services/AnimeEnricher.cpp src/services/MikanEnricher.cpp src/services/CoverScraper.cpp src/services/NovelBangumiService.cpp src/services/GameBangumiService.cpp)
target_sources(anime_vault_bangumi_service PRIVATE src/services/VndbTransport.cpp src/services/SteamPoster.cpp)
target_sources(anime_vault_bangumi_service PRIVATE src/services/WatchSyncService.cpp)
target_include_directories(anime_vault_bangumi_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(anime_vault_bangumi_service PUBLIC cxx_std_20)
target_link_libraries(anime_vault_bangumi_service PUBLIC anime_vault_core anime_vault_storage nlohmann_json::nlohmann_json)
anime_vault_apply_warnings(anime_vault_bangumi_service)
anime_vault_apply_sanitizers(anime_vault_bangumi_service)
anime_vault_apply_lto(anime_vault_bangumi_service)

add_library(anime_vault_bangumi_transport STATIC src/infrastructure/network/DrogonBangumiTransport.cpp src/infrastructure/network/DrogonCoverTransport.cpp src/infrastructure/network/BangumiConnectionManager.cpp src/infrastructure/network/WindowsSystemHttp.cpp src/infrastructure/network/QbWebClient.cpp src/infrastructure/network/QbConnectionManager.cpp)
target_sources(anime_vault_bangumi_transport PRIVATE src/infrastructure/network/DrogonVndbTransport.cpp)
target_sources(anime_vault_bangumi_transport PRIVATE src/infrastructure/network/SteamClient.cpp src/services/SteamImportService.cpp)
target_sources(anime_vault_bangumi_transport PRIVATE src/services/SteamLoginService.cpp src/infrastructure/network/MikanDownloadWorker.cpp)
target_sources(anime_vault_bangumi_transport PRIVATE src/infrastructure/network/SteamMetadataClient.cpp)
target_sources(anime_vault_bangumi_transport PRIVATE src/infrastructure/network/WatchSyncClient.cpp)
target_include_directories(anime_vault_bangumi_transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(anime_vault_bangumi_transport PUBLIC cxx_std_20)
target_link_libraries(anime_vault_bangumi_transport PUBLIC Drogon::Drogon nlohmann_json::nlohmann_json anime_vault_core anime_vault_bangumi_service)
if(WIN32)
  target_link_libraries(anime_vault_bangumi_transport PRIVATE winhttp Advapi32 bcrypt)
endif()
anime_vault_apply_warnings(anime_vault_bangumi_transport)
anime_vault_apply_sanitizers(anime_vault_bangumi_transport)
anime_vault_apply_lto(anime_vault_bangumi_transport)

add_library(anime_vault_api STATIC
  src/api/HttpSupport.cpp
  src/api/HealthController.cpp
  src/api/MediaController.cpp
  src/api/MediaJson.cpp
  src/api/ScanController.cpp
  src/api/AnimeController.cpp
  src/api/OrganizationController.cpp
  src/api/PlaybackController.cpp
  src/api/WatchProgressController.cpp
  src/api/WatchSyncController.cpp
  src/api/SettingsController.cpp
  src/api/QbRssController.cpp
  src/api/QbConfigController.cpp
  src/api/BangumiConfigController.cpp
  src/api/NovelController.cpp
  src/api/GameController.cpp
  src/api/SteamController.cpp
)
target_include_directories(anime_vault_api PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(anime_vault_api PUBLIC cxx_std_20)
target_link_libraries(anime_vault_api PUBLIC Drogon::Drogon anime_vault_core anime_vault_storage anime_vault_bangumi_service)
anime_vault_apply_warnings(anime_vault_api)
anime_vault_apply_sanitizers(anime_vault_api)
anime_vault_apply_lto(anime_vault_api)

add_executable(anime_vault_server
  src/main.cpp
)
target_compile_features(anime_vault_server PRIVATE cxx_std_20)
target_link_libraries(anime_vault_server PRIVATE
  anime_vault_core
  anime_vault_api
  anime_vault_storage
  anime_vault_bangumi_transport
)
anime_vault_apply_warnings(anime_vault_server)
anime_vault_apply_sanitizers(anime_vault_server)
anime_vault_apply_lto(anime_vault_server)

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()
