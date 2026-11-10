#include "Kuid.h"

#include <charconv>
#include <cstdio>
#include <vector>

namespace trainz::content {

namespace {

bool parseInt32(std::string_view s, std::int32_t& out) {
    if (s.empty()) {
        return false;
    }
    const char* first = s.data();
    const char* last = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc() && ptr == last;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

bool startsWithNoCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c != prefix[i]) {
            return false;
        }
    }
    return true;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

}  // namespace

std::optional<Kuid> parseKuid(std::string_view text) {
    std::string_view s = trim(text);
    if (!s.empty() && s.front() == '<') {
        if (s.size() < 2 || s.back() != '>') {
            return std::nullopt;
        }
        s = s.substr(1, s.size() - 2);
    }
    bool isKuid2 = false;
    char sep = 0;
    if (startsWithNoCase(s, "kuid2:") || startsWithNoCase(s, "kuid2 ")) {
        isKuid2 = true;
        sep = s[5];
        s.remove_prefix(6);
    } else if (startsWithNoCase(s, "kuid:") || startsWithNoCase(s, "kuid ")) {
        sep = s[4];
        s.remove_prefix(5);
    } else {
        return std::nullopt;
    }
    const auto parts = split(s, sep);
    if (parts.size() != (isKuid2 ? 3u : 2u)) {
        return std::nullopt;
    }
    Kuid k;
    if (!parseInt32(parts[0], k.user) || !parseInt32(parts[1], k.content)) {
        return std::nullopt;
    }
    if (isKuid2) {
        std::int32_t v = 0;
        if (!parseInt32(parts[2], v)) {
            return std::nullopt;
        }
        k.version = v;
    }
    return k;
}

std::optional<Kuid> parseKuidLoose(std::string_view text) {
    while (!text.empty() && (text.back() == '\0' || text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    if (auto k = parseKuid(text)) {
        return k;
    }
    const std::string_view s = trim(text);
    const auto parts = split(s, ':');
    if (parts.size() != 2) {
        return std::nullopt;
    }
    Kuid k;
    if (!parseInt32(parts[0], k.user) || !parseInt32(parts[1], k.content)) {
        return std::nullopt;
    }
    return k;
}

Kuid unpackChumpKuid(std::uint32_t packedUser, std::int32_t content) {
    const std::uint32_t low25 = packedUser & 0x1FFFFFFu;
    const std::uint32_t top7 = packedUser >> 25;
    Kuid k;
    k.content = content;
    if (low25 & 0x1000000u) {
        k.user = static_cast<std::int32_t>(low25) - 0x2000000;  // sign-extend 25 bits
    } else {
        k.user = static_cast<std::int32_t>(low25);
    }
    if (!(k.user < 0 && top7 == 0x7F) && top7 != 0) {
        k.version = static_cast<std::int32_t>(top7);
    }
    return k;
}

std::string formatKuid(const Kuid& k) {
    if (k.version) {
        return "<kuid2:" + std::to_string(k.user) + ":" + std::to_string(k.content) + ":" +
               std::to_string(*k.version) + ">";
    }
    return "<kuid:" + std::to_string(k.user) + ":" + std::to_string(k.content) + ">";
}

std::uint8_t kuidFolderHash(const Kuid& kuid) {
    const Kuid k = kuid.normalized();
    const auto u = static_cast<std::uint32_t>(k.user);
    const auto c = static_cast<std::uint32_t>(k.content);
    std::uint8_t h = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        h ^= static_cast<std::uint8_t>(u >> shift);
        h ^= static_cast<std::uint8_t>(c >> shift);
    }
    return h;
}

std::string kuidFolderKey(const Kuid& kuid) {
    const Kuid k = kuid.normalized();
    char hash[8];
    std::snprintf(hash, sizeof hash, "%02x", static_cast<unsigned>(kuidFolderHash(k)));
    std::string key = "hash-";
    key += hash;
    if (k.version) {
        key += "/kuid2 " + std::to_string(k.user) + " " + std::to_string(k.content) + " " +
               std::to_string(*k.version);
    } else {
        key += "/kuid " + std::to_string(k.user) + " " + std::to_string(k.content);
    }
    return key;
}

}  // namespace trainz::content
