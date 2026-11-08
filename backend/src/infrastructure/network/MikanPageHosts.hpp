#pragma once

#include "anime_vault/infrastructure/network/WindowsSystemHttp.hpp"

namespace anime_vault::detail {
template<typename Fetch>
WindowsHttpResult fetchMikanPage(Fetch&& fetch) {
    auto primary = fetch("mikanani.me");
    // 主站已响应时保留其条目判断；只在请求失败时尝试固定镜像。
    if (primary.status == 200 && primary.body) return primary;
    return fetch("mikanani.kas.pub");
}
}
