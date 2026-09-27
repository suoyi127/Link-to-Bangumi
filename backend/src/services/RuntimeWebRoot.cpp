#include "anime_vault/services/RuntimeWebRoot.hpp"

#include <stdexcept>

namespace anime_vault {
namespace fs = std::filesystem;

fs::path validatedWebRoot(const fs::path& candidate) {
    std::error_code error;
    if (!candidate.is_absolute() || !fs::is_directory(candidate, error) || error)
        throw std::invalid_argument("ANIME_VAULT_WEB_DIR must be an existing absolute directory");
    const auto root = fs::canonical(candidate, error);
    if (error || root == root.root_path())
        throw std::invalid_argument("ANIME_VAULT_WEB_DIR must not be a filesystem root");
    const auto index = root / "index.html";
    if (!fs::is_regular_file(index, error) || error)
        throw std::invalid_argument("ANIME_VAULT_WEB_DIR must contain index.html");
    const auto resolvedIndex = fs::canonical(index, error);
    if (error || resolvedIndex.parent_path() != root)
        throw std::invalid_argument("ANIME_VAULT_WEB_DIR index.html escapes its root");
    return root;
}
} // namespace anime_vault
