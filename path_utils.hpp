#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

inline std::vector<std::filesystem::path> ancestor_search_roots(
        const std::vector<std::filesystem::path>& hints,
        int max_depth = 8) {
    std::vector<std::filesystem::path> roots;
    for (const auto& hint : hints) {
        if (hint.empty())
            continue;

        std::filesystem::path current = hint;
        std::error_code ec;
        if (!std::filesystem::exists(current, ec))
            continue;
        if (!std::filesystem::is_directory(current, ec))
            current = current.parent_path();

        for (int depth = 0; depth <= max_depth && !current.empty(); ++depth) {
            roots.push_back(current);
            const auto parent = current.parent_path();
            if (parent == current)
                break;
            current = parent;
        }
    }
    return roots;
}

inline std::string resolve_existing_path(
        const std::string& raw,
        const std::vector<std::filesystem::path>& hints = {}) {
    namespace fs = std::filesystem;

    if (raw.empty())
        return "";

    std::error_code ec;
    fs::path input(raw);
    if (fs::exists(input, ec))
        return input.lexically_normal().string();
    if (input.is_absolute())
        return "";

    const auto roots = ancestor_search_roots(hints);
    for (const auto& root : roots) {
        const fs::path candidate = root / input;
        if (fs::exists(candidate, ec))
            return candidate.lexically_normal().string();
    }
    return "";
}

inline std::string find_demo_path(
        const std::vector<std::filesystem::path>& hints = {}) {
    return resolve_existing_path("demo.cpp", hints);
}

inline std::string compact_input_path_label(const std::string& raw) {
    namespace fs = std::filesystem;

    if (raw.empty())
        return "";

    fs::path path(raw);
    path = path.lexically_normal();

    std::string leaf = path.filename().string();
    if (leaf.empty())
        leaf = path.parent_path().filename().string();
    if (leaf.empty())
        return path.string();

    if (!path.has_parent_path())
        return leaf;
    return "../" + leaf;
}
