#include "anime_vault/services/PlayerCatalog.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace anime_vault {
namespace {
namespace fs = std::filesystem;
fs::path fromUtf8(const std::string& s) {
    return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
std::string utf8(const fs::path& p) {
    const auto bytes = p.u8string(); return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
struct Profile { const char* id; const char* name; std::vector<std::string> names, relative; };
const std::vector<Profile> profiles{
    {"mpv", "mpv", {"mpv.exe"}, {"mpv/mpv.exe"}},
    {"potplayer", "PotPlayer", {"PotPlayerMini64.exe", "PotPlayerMini.exe"}, {"DAUM/PotPlayer/PotPlayerMini64.exe", "DAUM/PotPlayer/PotPlayerMini.exe"}},
    {"vlc", "VLC", {"vlc.exe"}, {"VideoLAN/VLC/vlc.exe"}},
    {"mpchc", "MPC-HC", {"mpc-hc64.exe", "mpc-hc.exe"}, {"MPC-HC/mpc-hc64.exe", "MPC-HC/mpc-hc.exe"}},
    {"mpcbe", "MPC-BE", {"mpc-be64.exe", "mpc-be.exe"}, {"MPC-BE/mpc-be64.exe", "MPC-BE/mpc-be.exe"}}
};
std::vector<fs::path> roots() {
    std::vector<fs::path> result;
    for (const auto key : {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA", "ANIME_VAULT_HOME"}) {
        if (const auto value = std::getenv(key); value && *value) {
            const auto base = fromUtf8(value); result.push_back(base);
            if (std::string(key) == "LOCALAPPDATA") result.push_back(base / "Programs");
        }
    }
    return result;
}
std::string registryPath(const std::string& name) {
#ifdef _WIN32
    const auto key = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + fromUtf8(name).wstring();
    for (const auto hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        for (const auto view : {RRF_SUBKEY_WOW6464KEY, RRF_SUBKEY_WOW6432KEY}) {
            wchar_t buffer[2048]{}; DWORD size = sizeof(buffer);
            if (RegGetValueW(hive, key.c_str(), nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | view,
                            nullptr, buffer, &size) == ERROR_SUCCESS) {
                const auto path = normalizePlayerExecutable(utf8(fs::path(buffer)));
                if (validPlayerExecutable(path)) return path;
            }
        }
    }
#else
    (void)name;
#endif
    return {};
}
}
bool validPlayerType(const std::string& type) {
    return type == "system" || type == "custom" ||
        std::any_of(profiles.begin(), profiles.end(), [&](const auto& p) { return type == p.id; });
}
std::string normalizePlayerExecutable(std::string path) {
    const auto first = path.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    path = path.substr(first, path.find_last_not_of(' ') - first + 1);
    // 兼容 Windows“复制文件地址”产生的外层引号，仍按单一路径处理而非命令行。
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.substr(1, path.size() - 2);
    return path;
}
bool validPlayerExecutable(const std::string& path) {
    if (path.empty() || path.size() > 1024 || std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; })) return false;
    std::error_code error;
    const auto file = fromUtf8(path);
    auto ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return file.is_absolute() && ext == ".exe" && fs::is_regular_file(file, error) && !error;
}
std::vector<PlayerOption> discoverPlayers(const UiPreferences& preferences) {
    std::vector<PlayerOption> result;
    const auto bases = roots();
    for (const auto& profile : profiles) {
        PlayerOption option{profile.id, profile.name, {}, false};
        // 已保存路径优先；明确配置但文件丢失时不悄悄改用其他可执行文件。
        if (preferences.playerType == profile.id && !preferences.playerExecutable.empty()) option.executable = preferences.playerExecutable;
        else if (std::string(profile.id) == "mpv" && !preferences.mpvExecutable.empty()) option.executable = preferences.mpvExecutable;
        else {
            for (const auto& name : profile.names) {
                option.executable = registryPath(name);
                if (!option.executable.empty()) break;
            }
            if (option.executable.empty()) for (const auto& base : bases) {
                for (const auto& relative : profile.relative) {
                    const auto candidate = utf8(base / fromUtf8(relative));
                    if (validPlayerExecutable(candidate)) { option.executable = candidate; break; }
                }
                if (!option.executable.empty()) break;
            }
            if (option.executable.empty()) if (const auto path = std::getenv("PATH")) {
                std::istringstream parts(path); std::string part;
                while (std::getline(parts, part, ';')) {
                    if (part.empty()) continue;
                    for (const auto& name : profile.names) {
                        const auto candidate = utf8(fromUtf8(part) / fromUtf8(name));
                        if (validPlayerExecutable(candidate)) { option.executable = candidate; break; }
                    }
                    if (!option.executable.empty()) break;
                }
            }
        }
        option.executable = normalizePlayerExecutable(option.executable);
        option.available = validPlayerExecutable(option.executable);
        result.push_back(std::move(option));
    }
    const auto custom = preferences.playerType == "custom" ? normalizePlayerExecutable(preferences.playerExecutable) : std::string{};
    result.push_back({"custom", "自定义播放器", custom, validPlayerExecutable(custom)});
#ifdef _WIN32
    result.push_back({"system", "系统默认播放器", {}, true});
#else
    result.push_back({"system", "系统默认播放器", {}, false});
#endif
    return result;
}
}
