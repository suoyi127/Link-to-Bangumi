#include "anime_vault/services/SteamLibrary.hpp"
#include "anime_vault/domain/Game.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace anime_vault {
namespace {
namespace fs = std::filesystem;
std::string utf8(const fs::path& path) {
    const auto s = path.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
fs::path pathOf(const std::string& s) { return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(s.data()), s.size())); }
std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return s;
}
struct Node {
    bool object{};
    std::string value;
    std::map<std::string, Node> children;
};
class VdfParser {
public:
    explicit VdfParser(std::string text) : text_(std::move(text)) {}
    Node parse() {
        if (text_.starts_with("\xef\xbb\xbf")) pos_ = 3;
        return object(0, false);
    }
private:
    std::string text_;
    std::size_t pos_{}, count_{};
    void skip() {
        while (pos_ < text_.size()) {
            if (std::isspace(static_cast<unsigned char>(text_[pos_]))) { ++pos_; continue; }
            if (text_.compare(pos_, 2, "//") == 0) {
                auto end = text_.find('\n', pos_ + 2);
                pos_ = end == std::string::npos ? text_.size() : end;
                continue;
            }
            break;
        }
    }
    std::string token() {
        skip();
        if (pos_ == text_.size()) throw std::runtime_error("missing VDF token");
        std::string value;
        if (text_[pos_] == '"') {
            ++pos_;
            bool closed = false;
            while (pos_ < text_.size()) {
                char c = text_[pos_++];
                if (c == '"') { closed = true; break; }
                if (c == '\\') {
                    if (pos_ == text_.size()) throw std::runtime_error("unfinished VDF escape");
                    c = text_[pos_++];
                    if (c != '\\' && c != '"') value.push_back('\\');
                }
                if (c == '\0') throw std::runtime_error("NUL in VDF");
                value.push_back(c);
                if (value.size() > 65536) throw std::runtime_error("VDF token too long");
            }
            if (!closed) throw std::runtime_error("unclosed VDF string");
        } else {
            while (pos_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[pos_])) && text_[pos_] != '{' && text_[pos_] != '}') {
                if (text_[pos_] == '\0' || text_[pos_] == '"') throw std::runtime_error("invalid VDF token");
                value.push_back(text_[pos_++]);
                if (value.size() > 65536) throw std::runtime_error("VDF token too long");
            }
            if (value.empty()) throw std::runtime_error("missing VDF value");
        }
        return value;
    }
    Node object(unsigned depth, bool nested) {
        // 限制文件、深度和节点数量，避免损坏清单耗尽资源。
        if (depth > 32) throw std::runtime_error("VDF nesting limit");
        Node result; result.object = true;
        for (;;) {
            skip();
            if (pos_ == text_.size()) {
                if (nested) throw std::runtime_error("unclosed VDF object");
                return result;
            }
            if (text_[pos_] == '}') {
                if (!nested) throw std::runtime_error("unexpected VDF brace");
                ++pos_; return result;
            }
            const auto key = lower(token());
            if (++count_ > 100000) throw std::runtime_error("VDF node limit");
            skip();
            Node child;
            if (pos_ < text_.size() && text_[pos_] == '{') { ++pos_; child = object(depth + 1, true); }
            else child.value = token();
            if (!result.children.emplace(key, std::move(child)).second) throw std::runtime_error("duplicate VDF key");
        }
    }
};
Node readVdf(const fs::path& path) {
    constexpr std::size_t limit = 4 * 1024 * 1024;
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read VDF");
    std::string data(limit + 1, '\0');
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(file.gcount()));
    if (data.size() > limit || file.bad()) throw std::runtime_error("VDF file limit or read failure");
    return VdfParser(std::move(data)).parse();
}
const Node& child(const Node& node, const std::string& key) {
    const auto found = node.children.find(key);
    if (found == node.children.end()) throw std::runtime_error("missing VDF field: " + key);
    return found->second;
}
std::string field(const Node& node, const std::string& key) {
    const auto& n = child(node, key);
    if (n.object) throw std::runtime_error("unexpected VDF object");
    return n.value;
}
bool digits(std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }
std::uint64_t number(std::string_view s) {
    std::uint64_t result{};
    auto parsed = std::from_chars(s.data(), s.data() + s.size(), result);
    if (!digits(s) || parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size()) throw std::runtime_error("invalid numeric VDF field");
    return result;
}
bool within(const fs::path& path, const fs::path& root) {
    auto p = path.begin();
    for (auto r = root.begin(); r != root.end(); ++r, ++p) if (p == path.end() || *p != *r) return false;
    return p != path.end();
}
bool validRoot(const fs::path& root) {
    std::error_code ec;
    return fs::is_directory(root / "steamapps", ec) && fs::is_regular_file(root / "steam.exe", ec);
}
std::optional<SteamInstalledGame> readGame(const fs::path& manifest, const fs::path& executable) {
    const auto filename = utf8(manifest.filename());
    if (!filename.starts_with("appmanifest_") || !filename.ends_with(".acf")) return {};
    const auto id = filename.substr(12, filename.size() - 16);
    if (!digits(id) || number(id) == 0 || number(id) > 4294967295ULL || (id.size() > 1 && id.front() == '0')) throw std::runtime_error("invalid manifest AppID");
    const auto document = readVdf(manifest);
    const auto& app = child(document, "appstate");
    if (field(app, "appid") != id) throw std::runtime_error("manifest AppID mismatch");
    if (id == "228980") return {};
    const auto state = number(field(app, "stateflags"));
    // 仅接受安装完成（允许正在运行），更新和下载等状态均不可导入。
    if ((state & 4) == 0 || (state & ~(std::uint64_t{4} | 64)) != 0) return {};
    for (const auto& counters : {std::pair{"bytestodownload", "bytesdownloaded"}, std::pair{"bytestostage", "bytesstaged"}}) {
        const auto total = app.children.find(counters.first);
        if (total != app.children.end()) {
            const auto done = app.children.find(counters.second);
            if (number(field(app, counters.first)) > (done == app.children.end() ? 0 : number(field(app, counters.second)))) return {};
        }
    }
    auto dir = field(app, "installdir");
    std::replace(dir.begin(), dir.end(), '\\', '/');
    const auto relative = pathOf(dir);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || dir.find(':') != std::string::npos) throw std::runtime_error("unsafe install directory");
    for (const auto& component : relative) if (component == ".." || component == ".") throw std::runtime_error("unsafe install directory");
    // 规范化后再校验边界，防止目录联接或符号链接越界。
    const auto common = fs::canonical(manifest.parent_path() / "common");
    const auto install = fs::canonical(common / relative);
    if (!within(install, common) || !fs::is_directory(install) || !fs::is_regular_file(executable)) throw std::runtime_error("install unavailable or outside common directory");
    auto title = field(app, "name");
    if (title.empty()) title = utf8(relative.filename());
    return SteamInstalledGame{id, title, utf8(install), utf8(fs::canonical(manifest)), utf8(fs::canonical(executable))};
}
}

std::filesystem::path SteamLibrary::detectRoot() {
#ifdef _WIN32
    wchar_t value[32768]{};
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS) {
        fs::path root(value);
        if (validRoot(root)) return root;
    }
    for (const auto* variable : {L"ProgramFiles(x86)", L"ProgramFiles"}) {
        const auto size = GetEnvironmentVariableW(variable, value, static_cast<DWORD>(std::size(value)));
        if (size > 0 && size < std::size(value)) {
            fs::path root = fs::path(value) / "Steam";
            if (validRoot(root)) return root;
        }
    }
#endif
    throw GameError("steam_not_found");
}
SteamScanResult SteamLibrary::scan(const std::filesystem::path& input) {
    const auto root = input.empty() ? detectRoot() : input;
    if (!validRoot(root)) throw GameError("invalid_steam_path");
    SteamScanResult result;
    std::vector<fs::path> libraries{root};
    const auto folders = root / "steamapps/libraryfolders.vdf";
    std::error_code ec;
    if (fs::exists(folders, ec)) {
        try {
            const auto doc = readVdf(folders);
            const auto& entries = child(doc, "libraryfolders");
            if (!entries.object) throw std::runtime_error("invalid libraryfolders");
            for (const auto& [key, entry] : entries.children) {
                if (!digits(key)) continue;
                try {
                    const auto value = entry.object ? field(entry, "path") : entry.value;
                    auto path = pathOf(value);
                    if (!path.is_absolute()) throw std::runtime_error("relative Steam library path");
                    libraries.push_back(path);
                } catch (const std::exception& e) { result.warnings.push_back(utf8(folders) + ": " + e.what()); }
            }
        } catch (const std::exception& e) { result.warnings.push_back(utf8(folders) + ": " + e.what()); }
    }
    std::set<fs::path> seenLibraries;
    std::set<std::string> seenIds;
    for (const auto& library : libraries) {
        try {
            const auto apps = fs::canonical(library / "steamapps");
            if (!seenLibraries.insert(apps).second) continue;
            std::vector<fs::path> manifests;
            for (const auto& entry : fs::directory_iterator(apps)) {
                const auto name = utf8(entry.path().filename());
                if (name.starts_with("appmanifest_") && name.ends_with(".acf")) manifests.push_back(entry.path());
            }
            std::sort(manifests.begin(), manifests.end());
            for (const auto& manifest : manifests) {
                try {
                    // 清单本身也必须留在当前库，避免通过链接读取外部文件。
                    if (!within(fs::canonical(manifest), apps)) throw std::runtime_error("manifest outside Steam library");
                    const auto game = readGame(manifest, root / "steam.exe");
                    if (game && seenIds.insert(game->appId).second) result.games.push_back(*game);
                } catch (const std::exception& e) { result.warnings.push_back(utf8(manifest) + ": " + e.what()); }
            }
        } catch (const std::exception& e) { result.warnings.push_back(utf8(library) + ": " + e.what()); }
    }
    std::sort(result.games.begin(), result.games.end(), [](const auto& a, const auto& b) { return a.appId < b.appId; });
    return result;
}
bool SteamLibrary::available(const SteamInstalledGame& game) {
    try {
        const auto manifest = pathOf(game.manifestPath);
        if (!within(fs::canonical(manifest), fs::canonical(manifest.parent_path()))) return false;
        const auto current = readGame(manifest, pathOf(game.steamExecutable));
        return current && current->appId == game.appId && fs::equivalent(pathOf(current->installPath), pathOf(game.installPath));
    } catch (const std::exception&) { return false; }
}
}
