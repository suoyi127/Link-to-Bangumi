#include "anime_vault/infrastructure/ProcessLauncher.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
extern char** environ;
#endif

namespace anime_vault {
namespace {
#ifdef _WIN32
std::wstring fromUtf8(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                             text.data(), static_cast<int>(text.size()), result.data(), size)) return {};
    return result;
}
std::wstring quote(const std::wstring& value) {
    // Windows parses the command line itself; quote each argument using its backslash rules.
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'"') {
            result.append(slashes * 2 + 1, L'\\');
            result += L'"';
        } else {
            result.append(slashes, L'\\');
            result += ch;
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#endif
}

bool NativeProcessLauncher::launch(const std::filesystem::path& executable,
                                   const std::vector<std::string>& arguments) {
#ifdef _WIN32
    std::wstring command = quote(executable.wstring());
    for (const auto& argument : arguments) {
        const auto wide = fromUtf8(argument);
        if (!argument.empty() && wide.empty()) return false;
        command += L' ' + quote(wide);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                                        FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr,
                                        &startup, &process);
    if (!started) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    const auto program = executable.string();
    std::vector<std::string> owned{program};
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& item : owned) argv.push_back(item.data());
    argv.push_back(nullptr);
    pid_t pid{};
    return posix_spawn(&pid, program.c_str(), nullptr, nullptr, argv.data(), environ) == 0;
#endif
}

} // namespace anime_vault
