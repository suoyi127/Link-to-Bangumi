# Phase 0-1 Foundation and Parser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (\`- [ ]\`) syntax for tracking.

**Goal:** Create a reproducible Windows-first C++/React repository with a health endpoint and a test-driven anime filename parser backed by at least 50 fixtures.

**Architecture:** A framework-independent \`anime_vault_core\` static library owns domain values and parsing. A thin Drogon executable exposes health checks. A Vite React application provides the initial shell and verifies backend health without embedding business rules.

**Tech Stack:** C++20, CMake 3.25+, vcpkg, Drogon, Catch2, nlohmann-json, React, TypeScript, Vite, Ant Design, Vitest, ESLint.

---

## File Map

- \`AGENTS.md\`: repository safety and development constraints copied from the approved source specification.
- \`.gitignore\`, \`.gitattributes\`, \`.editorconfig\`: source-tree hygiene and stable text behavior.
- \`CMakeLists.txt\`, \`CMakePresets.json\`, \`vcpkg.json\`: root build contract.
- \`cmake/CompilerWarnings.cmake\`, \`cmake/Sanitizers.cmake\`: target-scoped compiler policy.
- \`backend/include/anime_vault/domain/EpisodeNumber.hpp\`: exact episode value.
- \`backend/include/anime_vault/domain/ParseResult.hpp\`: parser output contract.
- \`backend/include/anime_vault/services/FilenameParser.hpp\`: pure parser interface.
- \`backend/src/domain/EpisodeNumber.cpp\`: strict parse and serialization.
- \`backend/src/services/FilenameParser.cpp\`: staged filename parsing pipeline.
- \`backend/src/main.cpp\`: Drogon bootstrap and \`/health\`.
- \`backend/tests/unit/*\`: Catch2 domain and parser tests.
- \`fixtures/filenames.json\`: expected results for at least 50 filename cases.
- \`frontend/*\`: React/Vite shell, API client, health card, and Vitest tests.
- \`.env.example\`, \`compose.yaml\`, \`README.md\`: documented local configuration and commands.

### Task 1: Repository Policy and Build Skeleton

**Files:**
- Create: \`AGENTS.md\`
- Create: \`.gitignore\`
- Create: \`.gitattributes\`
- Create: \`.editorconfig\`
- Create: \`CMakeLists.txt\`
- Create: \`CMakePresets.json\`
- Create: \`vcpkg.json\`
- Create: \`cmake/CompilerWarnings.cmake\`
- Create: \`cmake/Sanitizers.cmake\`
- Create: \`backend/CMakeLists.txt\`
- Create: \`backend/src/main.cpp\`

- [ ] **Step 1: Add repository policies and generated-file exclusions**

Create \`AGENTS.md\` from section 16 of the approved source specification. Add these exact generated paths to \`.gitignore\`:

\`\`\`gitignore
/build/
/frontend/node_modules/
/frontend/dist/
/.env
/.superpowers/
*.db
*.db-shm
*.db-wal
\`\`\`

Use \`.gitattributes\` to keep source files LF while allowing Windows scripts to use CRLF:

\`\`\`gitattributes
* text=auto
*.cpp text eol=lf
*.hpp text eol=lf
*.cmake text eol=lf
CMakeLists.txt text eol=lf
*.ps1 text eol=crlf
*.bat text eol=crlf
\`\`\`

- [ ] **Step 2: Add a minimal target-scoped CMake graph**

The root must declare options and delegate to \`backend\`:

\`\`\`cmake
cmake_minimum_required(VERSION 3.25)
project(anime_vault VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_LTO "Enable link-time optimization" OFF)

list(APPEND CMAKE_MODULE_PATH "\${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
add_subdirectory(backend)
\`\`\`

The backend graph creates \`anime_vault_core\` and \`anime_vault_server\`, links Drogon only to the server, and registers tests only when \`BUILD_TESTING\` is enabled.

- [ ] **Step 3: Add reproducible presets and vcpkg dependencies**

Define configure presets \`dev\`, \`release\`, and \`test\`; build presets \`dev\` and \`release\`; and a test preset named \`test\`. All binary directories must be below \`build/\`. Declare Drogon, Catch2, and nlohmann-json in \`vcpkg.json\`.

- [ ] **Step 4: Configure before adding implementation**

Run:

\`\`\`powershell
cmake --preset dev
\`\`\`

Expected: configure reaches dependency resolution. If \`VCPKG_ROOT\` or a package is unavailable, record the exact prerequisite rather than changing the dependency strategy.

- [ ] **Step 5: Commit the build skeleton**

\`\`\`powershell
git add AGENTS.md .gitignore .gitattributes .editorconfig CMakeLists.txt CMakePresets.json vcpkg.json cmake backend
git commit -m "build: initialize C++ project structure"
\`\`\`

### Task 2: EpisodeNumber Value Object

**Files:**
- Create: \`backend/include/anime_vault/domain/EpisodeNumber.hpp\`
- Create: \`backend/src/domain/EpisodeNumber.cpp\`
- Create: \`backend/tests/CMakeLists.txt\`
- Create: \`backend/tests/unit/EpisodeNumberTest.cpp\`

- [ ] **Step 1: Write failing exact-value tests**

\`\`\`cpp
TEST_CASE("EpisodeNumber parses integers and decimals exactly") {
    REQUIRE(EpisodeNumber::parse("12")->toString() == "12");
    REQUIRE(EpisodeNumber::parse("12.5")->toString() == "12.5");
    REQUIRE_FALSE(EpisodeNumber::parse("12.50").has_value());
    REQUIRE_FALSE(EpisodeNumber::parse("-1").has_value());
}
\`\`\`

- [ ] **Step 2: Run the focused test and verify RED**

Run:

\`\`\`powershell
cmake --build --preset dev
ctest --test-dir build/dev -R EpisodeNumber --output-on-failure
\`\`\`

Expected: build or test failure because \`EpisodeNumber\` is not implemented.

- [ ] **Step 3: Implement the minimal exact representation**

Expose:

\`\`\`cpp
class EpisodeNumber {
public:
    static std::optional<EpisodeNumber> parse(std::string_view text);
    static EpisodeNumber fromParts(std::uint32_t whole, std::optional<std::uint32_t> tenth);
    [[nodiscard]] std::uint32_t whole() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> tenth() const noexcept;
    [[nodiscard]] std::string toString() const;
    auto operator<=>(const EpisodeNumber&) const = default;
private:
    std::uint32_t whole_{};
    std::optional<std::uint32_t> tenth_;
};
\`\`\`

Accept only non-negative integers or a single decimal digit. This matches the MVP filename contract and prevents floating-point persistence.

- [ ] **Step 4: Run RED-to-GREEN verification**

Run the focused CTest command again, then run \`ctest --preset test --output-on-failure\`. Expected: all registered tests pass.

- [ ] **Step 5: Commit**

\`\`\`powershell
git add backend/include/anime_vault/domain backend/src/domain backend/tests
git commit -m "feat: add exact episode number value"
\`\`\`

### Task 3: Parse Contract and Fixture Loader

**Files:**
- Create: \`backend/include/anime_vault/domain/ParseResult.hpp\`
- Create: \`backend/tests/support/FilenameFixture.hpp\`
- Create: \`backend/tests/support/FilenameFixture.cpp\`
- Create: \`fixtures/filenames.json\`
- Create: \`backend/tests/unit/FilenameFixtureTest.cpp\`

- [ ] **Step 1: Write a failing fixture schema test**

The test loads \`fixtures/filenames.json\`, requires at least 50 cases, and validates that every item contains \`filename\`, \`title\`, \`episode_type\`, \`confidence_band\`, and \`warnings\`.

\`\`\`cpp
TEST_CASE("filename fixture has the required coverage") {
    const auto cases = loadFilenameFixtures(FIXTURE_PATH);
    REQUIRE(cases.size() >= 50);
    REQUIRE(std::ranges::any_of(cases, [](const auto& item) {
        return item.filename.find("12.5") != std::string::npos;
    }));
}
\`\`\`

- [ ] **Step 2: Verify RED**

Run \`ctest --test-dir build/dev -R FilenameFixture --output-on-failure\`.
Expected: failure because the fixture loader or file does not exist.

- [ ] **Step 3: Define the parser result contract**

\`\`\`cpp
enum class EpisodeType { normal, sp, ova, ncop, nced, unknown };

struct ParseResult {
    std::string originalName;
    std::string title;
    std::optional<std::uint32_t> season;
    std::optional<EpisodeNumber> episode;
    EpisodeType episodeType{EpisodeType::unknown};
    std::optional<std::string> releaseGroup;
    std::optional<std::string> resolution;
    std::optional<std::uint32_t> version;
    double confidence{};
    std::vector<std::string> warnings;
};
\`\`\`

Create 50 or more fixtures, including the three observed filenames and synthetic coverage for Chinese, Japanese, English, \`S02E03\`, \`EP03\`, \`[03v2]\`, decimal episodes, SP, OVA, NCOP, NCED, batches, number-bearing titles, and missing episodes. Store filenames only; do not store absolute paths.

- [ ] **Step 4: Implement and verify the loader**

Use nlohmann-json to parse fixtures into a test-only structure. Run the focused test, then the full CTest preset. Expected: GREEN.

- [ ] **Step 5: Commit**

\`\`\`powershell
git add backend/include/anime_vault/domain/ParseResult.hpp backend/tests fixtures
git commit -m "test: add filename parsing fixture corpus"
\`\`\`

### Task 4: Staged Filename Parser

**Files:**
- Create: \`backend/include/anime_vault/services/FilenameParser.hpp\`
- Create: \`backend/src/services/FilenameParser.cpp\`
- Create: \`backend/tests/unit/FilenameParserTest.cpp\`

- [ ] **Step 1: Write failing parameterized behavior tests**

\`\`\`cpp
TEST_CASE("filename parser matches the approved fixture corpus") {
    FilenameParser parser;
    for (const auto& item : loadFilenameFixtures(FIXTURE_PATH)) {
        CAPTURE(item.filename);
        const auto result = parser.parse(item.filename);
        CHECK(result.title == item.title);
        CHECK(result.episodeType == item.episodeType);
        CHECK(optionalText(result.episode) == item.episode);
        CHECK(confidenceBand(result.confidence) == item.confidenceBand);
    }
}
\`\`\`

Add focused cases proving that \`86\` and \`2.5次元\` remain title content when another episode token exists, and that missing episode data cannot reach high confidence.

- [ ] **Step 2: Verify RED**

Run \`ctest --test-dir build/dev -R FilenameParser --output-on-failure\`.
Expected: failure because \`FilenameParser\` is absent.

- [ ] **Step 3: Implement the ordered pipeline**

Expose a pure API:

\`\`\`cpp
class FilenameParser {
public:
    [[nodiscard]] ParseResult parse(std::string_view filename) const;
};
\`\`\`

Implement small private/free helpers for extension removal, separator normalization, bracket tokenization, technical-tag extraction, release-group extraction, ordered episode patterns, title cleanup, and confidence scoring. Each helper must be deterministic and perform no I/O. Use explicit regex priority: \`SxxExx\`, \`EPxx/E\`, bracketed episode/version, dash-delimited episode, decimal, then special types.

- [ ] **Step 4: Iterate fixture-by-fixture to GREEN**

Run the focused test after each parser behavior group. Do not relax expected fixture results merely to match implementation. When all cases pass, run:

\`\`\`powershell
ctest --preset test --output-on-failure
\`\`\`

Expected: all parser and domain tests pass.

- [ ] **Step 5: Commit**

\`\`\`powershell
git add backend/include/anime_vault/services backend/src/services backend/tests/unit
git commit -m "feat: parse anime release filenames"
\`\`\`

### Task 5: Drogon Health Endpoint

**Files:**
- Create: \`backend/include/anime_vault/api/HealthController.hpp\`
- Create: \`backend/src/api/HealthController.cpp\`
- Modify: \`backend/src/main.cpp\`
- Create: \`backend/tests/integration/HealthEndpointTest.cpp\`

- [ ] **Step 1: Write a failing controller response test**

\`\`\`cpp
TEST_CASE("health response reports the service as healthy") {
    const auto body = makeHealthPayload();
    REQUIRE(body["status"] == "ok");
    REQUIRE(body["service"] == "anime-vault");
}
\`\`\`

- [ ] **Step 2: Verify RED**

Run \`ctest --test-dir build/dev -R Health --output-on-failure\`.
Expected: failure because \`makeHealthPayload\` is absent.

- [ ] **Step 3: Add the thin endpoint**

\`GET /health\` returns HTTP 200 and:

\`\`\`json
{"status":"ok","service":"anime-vault"}
\`\`\`

Configure Drogon to bind \`127.0.0.1\` only, with the port read from \`ANIME_VAULT_PORT\` and a documented local default. Keep payload construction testable without opening a socket.

- [ ] **Step 4: Build and verify**

Run:

\`\`\`powershell
cmake --build --preset dev
ctest --preset test --output-on-failure
\`\`\`

Expected: backend builds and all tests pass without warnings from project targets.

- [ ] **Step 5: Commit**

\`\`\`powershell
git add backend
git commit -m "feat: expose local health endpoint"
\`\`\`

### Task 6: React Application Shell

**Files:**
- Create: \`frontend/package.json\`
- Create: \`frontend/tsconfig.json\`
- Create: \`frontend/vite.config.ts\`
- Create: \`frontend/eslint.config.js\`
- Create: \`frontend/index.html\`
- Create: \`frontend/src/main.tsx\`
- Create: \`frontend/src/App.tsx\`
- Create: \`frontend/src/api/health.ts\`
- Create: \`frontend/src/components/HealthStatus.tsx\`
- Create: \`frontend/src/components/HealthStatus.test.tsx\`
- Create: \`frontend/src/test/setup.ts\`

- [ ] **Step 1: Add a failing health-card test**

\`\`\`tsx
it("shows a healthy backend", async () => {
  vi.spyOn(healthApi, "getHealth").mockResolvedValue({
    status: "ok",
    service: "anime-vault",
  });
  render(<HealthStatus />);
  expect(await screen.findByText("服务正常")).toBeInTheDocument();
});
\`\`\`

- [ ] **Step 2: Install and verify RED**

Run:

\`\`\`powershell
npm --prefix frontend install
npm --prefix frontend test -- --run
\`\`\`

Expected: failure because \`HealthStatus\` is not implemented.

- [ ] **Step 3: Implement the minimal desktop shell**

Create an Ant Design layout with Chinese labels for \`仪表盘\`, \`待整理\`, \`番剧库\`, and \`设置\`. Only the dashboard health card is functional in this milestone. \`getHealth\` uses a relative \`/health\` request and validates the minimal response shape.

- [ ] **Step 4: Verify GREEN and production build**

Run:

\`\`\`powershell
npm --prefix frontend test -- --run
npm --prefix frontend run lint
npm --prefix frontend run build
\`\`\`

Expected: tests, lint, and TypeScript/Vite build all exit 0.

- [ ] **Step 5: Commit**

\`\`\`powershell
git add frontend
git commit -m "feat: add React management shell"
\`\`\`

### Task 7: Configuration and Developer Documentation

**Files:**
- Create: \`.env.example\`
- Create: \`compose.yaml\`
- Create: \`README.md\`
- Modify: \`CMakePresets.json\`
- Modify: \`frontend/vite.config.ts\`

- [ ] **Step 1: Write a configuration contract check**

Add a CTest or PowerShell validation test that parses \`.env.example\` and requires:

\`\`\`text
ANIME_VAULT_BIND=127.0.0.1
ANIME_VAULT_SOURCE_DIR=D:\追番\番剧
ANIME_VAULT_LIBRARY_DIR=D:\追番\媒体库
ANIME_VAULT_DATA_DIR=D:\追番\anime-vault-data
ANIME_VAULT_QB_ENABLED=false
\`\`\`

No password, token, or private URL may have a non-empty example value.

- [ ] **Step 2: Verify RED**

Run the focused configuration test. Expected: failure because the example file does not yet exist.

- [ ] **Step 3: Add documentation and non-destructive compose defaults**

Document prerequisites, vcpkg setup, configure/build/test commands, frontend commands, startup order, local URLs, and the phase 0-1 limitation that no scanning or filesystem changes occur. The Compose file is a deployment scaffold only and must not mount the real source directory read-write.

- [ ] **Step 4: Run the full milestone gate**

\`\`\`powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset test --output-on-failure
npm --prefix frontend test -- --run
npm --prefix frontend run lint
npm --prefix frontend run build
\`\`\`

Expected: every command exits 0. Start the backend against test configuration, request \`http://127.0.0.1:<port>/health\`, and verify the exact JSON payload.

- [ ] **Step 5: Review against the phase 0-1 specification**

Confirm that generated files remain below \`build/\` or \`frontend/dist/\`, the fixture count is at least 50, no test touches \`D:\\追番\`, Drogon is absent from core target dependencies, and no placeholder text remains.

- [ ] **Step 6: Commit**

\`\`\`powershell
git add .env.example compose.yaml README.md CMakePresets.json frontend/vite.config.ts
git commit -m "docs: add local development workflow"
\`\`\`

## Subsequent Plans

After this plan passes its milestone gate, write and execute separate plans in this order:

1. Phase 2: SQLite migrations, stable-file directory scanner, inbox repositories/API, target planning, and conflict detection.
2. Phase 3: transactional hard-link/symlink/copy executor, rollback, idempotency, and audit log.
3. Phase 4: Bangumi client, cache/retry, ranking, manual binding, and related API.
4. Phase 5: dashboard, inbox workflow, library/detail pages, settings, and Playwright critical flow.

Each subsequent plan must preserve TDD, use disposable filesystem/database fixtures, and end with the complete repository verification gate.
