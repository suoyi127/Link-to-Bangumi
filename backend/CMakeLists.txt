find_package(Drogon CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/001_initial.sql" ANIME_VAULT_INITIAL_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/002_media_correction.sql" ANIME_VAULT_CORRECTION_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/003_source_snapshot.sql" ANIME_VAULT_SOURCE_SNAPSHOT_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/004_media_origin.sql" ANIME_VAULT_MEDIA_ORIGIN_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/005_organization_execution.sql" ANIME_VAULT_ORGANIZATION_EXECUTION_SQL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/migrations/006_parsed_title.sql" ANIME_VAULT_PARSED_TITLE_SQL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/001_initial.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/002_media_correction.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/003_source_snapshot.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/004_media_origin.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/005_organization_execution.sql"
  "${CMAKE_CURRENT_SOURCE_DIR}/migrations/006_parsed_title.sql")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure/database/InitialSql.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/InitialSql.hpp" @ONLY)

add_library(anime_vault_storage STATIC
  src/infrastructure/database/SqliteDatabase.cpp
  src/infrastructure/database/SqliteMediaRepository.cpp
)
target_include_directories(anime_vault_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(anime_vault_storage PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_features(anime_vault_storage PUBLIC cxx_std_20)
target_link_libraries(anime_vault_storage PUBLIC unofficial::sqlite3::sqlite3)
anime_vault_apply_warnings(anime_vault_storage)
anime_vault_apply_sanitizers(anime_vault_storage)
anime_vault_apply_lto(anime_vault_storage)

add_library(anime_vault_core STATIC
  src/Core.cpp
  src/domain/EpisodeNumber.cpp
  src/services/FilenameParser.cpp
  src/services/MikanTitleParser.cpp
  src/services/DirectoryScanner.cpp
  src/services/TargetPlanner.cpp
  src/services/OrganizationExecutor.cpp
  src/services/OrganizationService.cpp
  src/services/BangumiMatcher.cpp
  src/services/PlaybackService.cpp
  src/services/RuntimePaths.cpp
  src/services/RuntimeWebRoot.cpp
  src/infrastructure/ProcessLauncher.cpp
)
target_include_directories(anime_vault_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(anime_vault_core PUBLIC cxx_std_20)
anime_vault_apply_warnings(anime_vault_core)
anime_vault_apply_sanitizers(anime_vault_core)
anime_vault_apply_lto(anime_vault_core)

add_library(anime_vault_bangumi_service STATIC src/services/BangumiService.cpp src/services/AnimeEnricher.cpp src/services/MikanEnricher.cpp src/services/CoverScraper.cpp)
target_include_directories(anime_vault_bangumi_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(anime_vault_bangumi_service PUBLIC cxx_std_20)
target_link_libraries(anime_vault_bangumi_service PUBLIC anime_vault_core anime_vault_storage nlohmann_json::nlohmann_json)
anime_vault_apply_warnings(anime_vault_bangumi_service)
anime_vault_apply_sanitizers(anime_vault_bangumi_service)
anime_vault_apply_lto(anime_vault_bangumi_service)

add_library(anime_vault_bangumi_transport STATIC src/infrastructure/network/DrogonBangumiTransport.cpp src/infrastructure/network/DrogonCoverTransport.cpp src/infrastructure/network/WindowsSystemHttp.cpp src/infrastructure/network/QbWebClient.cpp)
target_include_directories(anime_vault_bangumi_transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(anime_vault_bangumi_transport PUBLIC cxx_std_20)
target_link_libraries(anime_vault_bangumi_transport PUBLIC Drogon::Drogon nlohmann_json::nlohmann_json anime_vault_core anime_vault_bangumi_service)
if(WIN32)
  target_link_libraries(anime_vault_bangumi_transport PRIVATE winhttp)
endif()
anime_vault_apply_warnings(anime_vault_bangumi_transport)
anime_vault_apply_sanitizers(anime_vault_bangumi_transport)
anime_vault_apply_lto(anime_vault_bangumi_transport)

add_library(anime_vault_api STATIC
  src/api/HealthController.cpp
  src/api/MediaController.cpp
  src/api/MediaService.cpp
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
