#include "ArchivePath.h"

#include <vector>

namespace trainz::content {

std::string asciiLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string canonicalizePath(std::string_view path) {
    const bool leadingSeparator = !path.empty() && (path.front() == '/' || path.front() == '\\');
    std::vector<std::string_view> parts;
    std::size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && (path[i] == '/' || path[i] == '\\')) {
            ++i;
        }
        std::size_t j = i;
        while (j < path.size() && path[j] != '/' && path[j] != '\\') {
            ++j;
        }
        if (j > i) {
            const std::string_view part = path.substr(i, j - i);
            if (part == ".") {
                // skipped (compare with "." at 0x10023c39)
            } else if (part == "..") {
                // removes the previous part (0x10023b6f .. 0x10023bcc); nothing to remove at the top
                if (!parts.empty()) {
                    parts.pop_back();
                }
            } else {
                parts.push_back(part);
            }
        }
        i = j;
    }
    std::string out;
    if (leadingSeparator) {
        out.push_back('/');
    }
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (k > 0) {
            out.push_back('/');
        }
        out.append(parts[k]);
    }
    return out;
}

std::optional<std::string> makeLookupKey(std::string_view requested) {
    if (requested.size() > kMaxArchiveNameLength) {
        return std::nullopt;
    }
    return canonicalizePath(asciiLower(requested));
}

std::uint32_t jetNameHash(std::string_view s) {
    std::uint32_t h = 0;
    for (char c : s) {
        // movsx: the byte is sign-extended before the xor
        const auto signedByte = static_cast<std::int32_t>(static_cast<signed char>(c));
        h = (h << 3) ^ static_cast<std::uint32_t>(signedByte);
    }
    return h;
}

}  // namespace trainz::content
