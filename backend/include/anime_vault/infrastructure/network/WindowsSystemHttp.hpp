#pragma once

#include <functional>
#include <optional>
#include <string>

namespace anime_vault {
struct WindowsHttpResult {
    int status{};
    std::optional<std::string> body;
};
WindowsHttpResult authenticatedBangumiRequest(const std::string& path,const std::string& method,
                                              const std::string& agent,const std::string& body,
                                              const std::string& token);

// Windows desktop proxy settings are distinct from WinHTTP's machine defaults.
// This adapter follows the user's system proxy without routing via qB's RSS bridge.
void windowsSystemHttpsRequest(std::string host, std::string path, std::string method,
                               std::string userAgent, std::string body, std::size_t maxBytes,
                               std::function<void(WindowsHttpResult)> completion);
}
