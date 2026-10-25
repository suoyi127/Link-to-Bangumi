#pragma once
#include <memory>
namespace anime_vault {
// 同一用例共享持久化锁；须支持同线程递归获取，持锁期间仍可调用仓库方法。
struct RepositoryLock { virtual ~RepositoryLock() = default; };
}
