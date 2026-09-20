#pragma once
#include "core.hpp"
#include <atomic>
#include <filesystem>
#include <set>
#include <system_error>

namespace mb {
inline std::string utf8(std::filesystem::path const& path) {
    auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}
inline std::filesystem::path fromUtf8(std::string const& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

inline std::filesystem::path absolutePath(std::filesystem::path path) {
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : normalized;
}
inline std::string pathKey(std::filesystem::path const& path) {
    auto key = utf8(path.lexically_normal());
#ifdef _WIN32
    return lowerAscii(std::move(key));
#else
    return key;
#endif
}
struct ScanIssue { std::filesystem::path path; std::string message; };
struct CatalogResult {
    std::vector<std::filesystem::path> files;
    std::vector<ScanIssue> issues;
    size_t folders = 0;
};

// Disk-only code: safe to run away from the game thread. Do not follow directory
// links; overlapping roots must not duplicate files or recurse forever.
inline CatalogResult scanFolders(std::vector<std::filesystem::path> const& roots,
                                 std::atomic_bool const& cancelled) {
    namespace fs = std::filesystem;
    CatalogResult result;
    std::vector<fs::path> pending;
    for (auto const& root : roots) if (!root.empty()) pending.push_back(root);
    std::set<std::string> folders, files;
    while (!pending.empty() && !cancelled.load()) {
        auto folder = absolutePath(pending.back());
        pending.pop_back();
        if (!folders.insert(pathKey(folder)).second) continue;
        std::error_code ec;
        fs::directory_iterator it(folder, ec), end;
        if (ec) { result.issues.push_back({folder, ec.message()}); continue; }
        ++result.folders;
        while (it != end && !cancelled.load()) {
            auto path = it->path();
            std::error_code itemError;
            auto info = it->symlink_status(itemError);
            if (itemError) result.issues.push_back({path, itemError.message()});
            else if (fs::is_directory(info)) pending.push_back(path);
            else if (macroExtension(utf8(path.extension())) && it->is_regular_file(itemError)) {
                path = absolutePath(path);
                if (files.insert(pathKey(path)).second) result.files.push_back(std::move(path));
            }
            it.increment(ec);
            if (ec) { result.issues.push_back({folder, ec.message()}); break; }
        }
    }
    std::sort(result.files.begin(), result.files.end());
    return result;
}
}
