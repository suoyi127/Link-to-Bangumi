#pragma once
#include <drogon/HttpRequest.h>
#include <string>

namespace anime_vault::api {
inline bool localRequestAllowed(const drogon::HttpRequestPtr& request) {
    if (request->getHeader("sec-fetch-site") == "cross-site") return false;
    const auto origin = request->getHeader("origin");
    if (origin.empty()) return true;
    if (origin == "http://127.0.0.1:5173" || origin == "http://localhost:5173" ||
        origin == "http://127.0.0.1:8848" || origin == "http://localhost:8848") return true;
    // 安装版使用随机端口；取实际监听地址而非可伪造的 Host，避免放行任意本机端口。
    const auto& address = request->getLocalAddr();
    const auto ip = address.toIp();
    if ((ip != "127.0.0.1" && ip != "::1") || address.toPort() == 0) return false;
    const auto port = std::to_string(address.toPort());
    return origin == "http://127.0.0.1:" + port || origin == "http://localhost:" + port;
}
}
