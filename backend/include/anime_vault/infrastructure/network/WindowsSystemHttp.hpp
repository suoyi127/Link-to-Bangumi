#pragma once

#include <functional>
#include <optional>
#include <string>

namespace anime_vault {
struct WindowsHttpResult {
    int status{};
    std::optional<std::string> body;
};

// Windows desktop proxy settings are distinct from WinHTTP's machine defaults.
// This adapter follows the user's system proxy without routing via qB's RSS bridge.
void windowsSystemHttpsRequest(std::string host, std::string path, std::string method,
                               std::string userAgent, std::string body, std::size_t maxBytes,
                               std::function<void(WindowsHttpResult)> completion);
}
