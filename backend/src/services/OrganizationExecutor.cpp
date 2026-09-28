#include "anime_vault/services/OrganizationExecutor.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
#include <random>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace anime_vault {
namespace fs = std::filesystem;

namespace {
bool within(const fs::path& root, const fs::path& path) {
    const auto [rootEnd, pathEnd] = std::mismatch(root.begin(), root.end(), path.begin(), path.end());
    (void)pathEnd;
    return rootEnd == root.end();
}

bool isLinkOrReparse(const fs::path& path) {
    std::error_code error;
    if (fs::is_symlink(fs::symlink_status(path, error))) return true;
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}

void requireDirectory(const fs::path& path) {
    std::error_code error;
    if (isLinkOrReparse(path) || !fs::is_directory(path, error) || error)
        throw OrganizationError("invalid_root");
}

fs::path canonicalRoot(const fs::path& input) {
    if (input.empty()) throw OrganizationError("invalid_root");
    try {
        const auto absolute = fs::absolute(input).lexically_normal();
        requireDirectory(absolute);
        return fs::canonical(absolute);
    } catch (const fs::filesystem_error&) {
        throw OrganizationError("invalid_root");
    }
}

void requireSafeDirectoryChain(const fs::path& root, const fs::path& path, bool create) {
    // 逐级检查目标目录，拒绝链接/重解析点绕过词法路径的根目录限制。
    requireDirectory(root);
    if (!within(root, path)) throw OrganizationError("invalid_root");
    auto current = root;
    const auto relative = path.lexically_relative(root);
    for (const auto& part : relative) {
        if (part.empty() || part == ".") continue;
        if (part == "..") throw OrganizationError("invalid_root");
        current /= part;
        std::error_code error;
        const auto status = fs::symlink_status(current, error);
        if (error && error != std::errc::no_such_file_or_directory) throw OrganizationError("invalid_root");
        if ((status.type() == fs::file_type::not_found ||
             error == std::errc::no_such_file_or_directory) && create) {
            error.clear();
            fs::create_directory(current, error);
            if (error && error != std::errc::file_exists) throw OrganizationError("invalid_root");
        }
        requireDirectory(current);
        if (!within(root, fs::canonical(current, error)) || error)
            throw OrganizationError("invalid_root");
    }
}

fs::path tempName(const fs::path& parent) {
    std::random_device random;
    constexpr char hex[] = "0123456789abcdef";
    std::string name = ".anime-vault-";
    for (int i = 0; i < 32; ++i) name.push_back(hex[random() & 15]);
    name += ".tmp";
    return parent / name;
}

bool copyToNewTemporary(const fs::path& source, const fs::path& temporary) {
#ifdef _WIN32
    const HANDLE output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) return false;
        throw OrganizationError("publish_failed");
    }
#else
    const int output = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (output < 0) {
        if (errno == EEXIST) return false;
        throw OrganizationError("publish_failed");
    }
#endif
    struct RemoveOnFailure {
        fs::path path;
        bool keep{};
        ~RemoveOnFailure() { if (!keep) { std::error_code ignored; fs::remove(path, ignored); } }
    } cleanup{temporary};
#ifdef _WIN32
    struct CloseOutput { HANDLE handle; ~CloseOutput() { CloseHandle(handle); } } close{output};
#else
    struct CloseOutput { int handle; ~CloseOutput() { close(handle); } } closeOutput{output};
#endif
    // The caller now owns this exact temporary path and removes it on any failure.
    std::ifstream input(source, std::ios::binary);
    if (!input) throw OrganizationError("source_changed");
    std::array<char, 65536> buffer{};
    while (input) {
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        if (count <= 0) break;
#ifdef _WIN32
        DWORD written{};
        if (!WriteFile(output, buffer.data(), static_cast<DWORD>(count), &written, nullptr) ||
            written != static_cast<DWORD>(count)) throw OrganizationError("publish_failed");
#else
        std::streamsize offset = 0;
        while (offset < count) {
            const auto written = write(output, buffer.data() + offset,
                                       static_cast<std::size_t>(count - offset));
            if (written <= 0) throw OrganizationError("publish_failed");
            offset += written;
        }
#endif
    }
    if (!input.eof()) throw OrganizationError("source_changed");
#ifdef _WIN32
    if (!FlushFileBuffers(output)) throw OrganizationError("publish_failed");
#else
    if (fsync(output) != 0) throw OrganizationError("publish_failed");
#endif
    cleanup.keep = true;
    return true;
}
}  // namespace

OrganizationError::OrganizationError(std::string code) : std::runtime_error(code), code_(std::move(code)) {}

OrganizationExecutor::OrganizationExecutor(fs::path sourceRoot, fs::path libraryRoot)
    : sourceRoot_(canonicalRoot(sourceRoot)), libraryRoot_(canonicalRoot(libraryRoot)) {}

ExecuteFileResult OrganizationExecutor::execute(const ExecuteFileRequest& request) const {
    if (request.operation != "hardlink" && request.operation != "copy" &&
        request.operation != "symlink") throw OrganizationError("unsupported_operation");
    if (request.source.empty() || request.target.empty()) throw OrganizationError("invalid_root");
    fs::path source;
    fs::path target;
    try {
        source = fs::absolute(request.source).lexically_normal();
        target = fs::absolute(request.target).lexically_normal();
        requireSafeDirectoryChain(sourceRoot_, source.parent_path(), false);
        requireSafeDirectoryChain(libraryRoot_, target.parent_path(), true);
    } catch (const fs::filesystem_error&) {
        throw OrganizationError("invalid_root");
    }
    // 同时约束规范化路径和物理路径，源文件不能通过链接逃出授权目录。
    if (!within(sourceRoot_, source) || !within(libraryRoot_, target) ||
        source == sourceRoot_ || target == libraryRoot_ || isLinkOrReparse(source))
        throw OrganizationError("invalid_root");
    std::error_code error;
    if (!fs::is_regular_file(source, error) || error || fs::canonical(source, error) != source || error)
        throw OrganizationError("source_changed");
    if (fs::file_size(source, error) != request.expectedSize || error ||
        fs::last_write_time(source, error) != request.expectedModifiedAt || error)
        throw OrganizationError("source_changed");
    if (fs::symlink_status(target, error).type() != fs::file_type::not_found ||
        (error && error != std::errc::no_such_file_or_directory))
        throw OrganizationError("target_exists");

    // 先在目标目录创建唯一临时文件，再原子发布；清理逻辑只拥有本次随机临时名。
    fs::path temporary;
    bool created = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        temporary = tempName(target.parent_path());
        error.clear();
        if (request.operation == "hardlink") {
            fs::create_hard_link(source, temporary, error);
        } else if (request.operation == "symlink") {
            if (fs::file_size(source, error) != request.expectedSize || error ||
                fs::last_write_time(source, error) != request.expectedModifiedAt || error)
                throw OrganizationError("source_changed");
            fs::create_symlink(source, temporary, error);
            if (error && error != std::errc::file_exists)
                throw OrganizationError("symlink_unavailable");
        } else {
            created = copyToNewTemporary(source, temporary);
            if (!created) error = std::make_error_code(std::errc::file_exists);
        }
        if (!error) { created = true; break; }
        if (error != std::errc::file_exists) throw OrganizationError("publish_failed");
    }
    if (!created) throw OrganizationError("publish_failed");
    struct RemoveTemporary {
        fs::path path;
        ~RemoveTemporary() { std::error_code ignored; fs::remove(path, ignored); }
    } cleanup{temporary};
    if (fs::file_size(temporary, error) != request.expectedSize || error ||
        fs::file_size(source, error) != request.expectedSize || error ||
        fs::last_write_time(source, error) != request.expectedModifiedAt || error)
        throw OrganizationError("source_changed");
    requireSafeDirectoryChain(libraryRoot_, target.parent_path(), false);
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_FILE_EXISTS)
            throw OrganizationError("target_exists");
        throw OrganizationError("publish_failed");
    }
#else
    fs::create_hard_link(temporary, target, error);
    if (error == std::errc::file_exists) throw OrganizationError("target_exists");
    if (error) throw OrganizationError("publish_failed");
#endif
    return {target, request.expectedSize};
}

}  // namespace anime_vault
