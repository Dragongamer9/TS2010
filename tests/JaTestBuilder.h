// Builds small .ja archives byte by byte, following docs/formats/ja.md, so the
// tests never need the game's own files.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

namespace trainz::test {

using Bytes = std::vector<std::uint8_t>;

inline Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

inline void putU16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}

inline void putU32(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

inline void setU32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

inline void putName(Bytes& b, const std::string& name) {
    Bytes field(260, 0);
    for (std::size_t i = 0; i < name.size() && i < 260; ++i) {
        field[i] = static_cast<std::uint8_t>(name[i]);
    }
    b.insert(b.end(), field.begin(), field.end());
}

// A BPE sub-block whose pair table is all literals: "FF FF" skips all 256
// codes, then a big-endian count and the bytes themselves.
inline Bytes bpeLiteralSubBlock(const Bytes& data) {
    if (data.size() > 0xFFFF) {
        throw std::invalid_argument("BPE sub-block too large");
    }
    Bytes b = {0xFF, 0xFF, static_cast<std::uint8_t>(data.size() >> 8), static_cast<std::uint8_t>(data.size())};
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

// "CFIL" header for version 0x102 archives (or just the magic for 0x101).
inline Bytes cfilHeader(std::uint32_t originalSize, std::uint32_t archiveVersion = 0x102) {
    Bytes b = bytesOf("CFIL");
    if (archiveVersion != 0x101) {
        putU32(b, 0x101);
        putU32(b, originalSize);
    }
    return b;
}

// CFIL made only of raw (0x8000-flagged) blocks of up to 16 KiB.
inline Bytes cfilRaw(const Bytes& data, std::uint32_t archiveVersion = 0x102) {
    Bytes b = cfilHeader(static_cast<std::uint32_t>(data.size()), archiveVersion);
    for (std::size_t pos = 0; pos < data.size(); pos += 0x4000) {
        const std::size_t len = std::min<std::size_t>(0x4000, data.size() - pos);
        putU16(b, static_cast<std::uint16_t>(0x8000 | len));
        b.insert(b.end(), data.begin() + static_cast<std::ptrdiff_t>(pos),
                 data.begin() + static_cast<std::ptrdiff_t>(pos + len));
    }
    return b;
}

// CFIL made of packed blocks, each one literal BPE sub-block of up to 16 KiB.
inline Bytes cfilBpe(const Bytes& data, std::uint32_t archiveVersion = 0x102) {
    Bytes b = cfilHeader(static_cast<std::uint32_t>(data.size()), archiveVersion);
    for (std::size_t pos = 0; pos < data.size(); pos += 0x4000) {
        const std::size_t len = std::min<std::size_t>(0x4000, data.size() - pos);
        const Bytes chunk(data.begin() + static_cast<std::ptrdiff_t>(pos),
                          data.begin() + static_cast<std::ptrdiff_t>(pos + len));
        const Bytes payload = bpeLiteralSubBlock(chunk);
        putU16(b, static_cast<std::uint16_t>(payload.size()));
        b.insert(b.end(), payload.begin(), payload.end());
    }
    return b;
}

struct TestEntry {
    std::string name;
    Bytes stored;
    std::uint32_t originalSize = 0;
    std::uint16_t flags = 0;
    std::uint32_t hash = 0;
};

class JaTestBuilder {
public:
    explicit JaTestBuilder(std::uint32_t version = 0x102) : version_(version) {}

    JaTestBuilder& addPlain(const std::string& name, const Bytes& data) {
        entries_.push_back({name, data, static_cast<std::uint32_t>(data.size()), 0, 0x1234});
        return *this;
    }
    JaTestBuilder& addCfilRaw(const std::string& name, const Bytes& data) {
        entries_.push_back({name, cfilRaw(data, version_), static_cast<std::uint32_t>(data.size()), 0x80, 0});
        return *this;
    }
    JaTestBuilder& addCfilBpe(const std::string& name, const Bytes& data) {
        entries_.push_back({name, cfilBpe(data, version_), static_cast<std::uint32_t>(data.size()), 0x80, 0});
        return *this;
    }
    JaTestBuilder& addEntry(TestEntry e) {
        entries_.push_back(std::move(e));
        return *this;
    }

    Bytes build() const {
        Bytes b = bytesOf("ARCHINFO");
        putU32(b, 32);
        putU32(b, 0x41524330);  // 'ARC0'
        putU32(b, version_);
        putU32(b, static_cast<std::uint32_t>(entries_.size()));
        const std::size_t idxOffsetAt = b.size();
        putU32(b, 0);
        putU32(b, 42203);
        b.resize(b.size() + 12, 0);

        std::vector<std::uint32_t> offsets;
        for (const auto& e : entries_) {
            b.insert(b.end(), {'F', 'I', 'L', ' '});
            putU32(b, static_cast<std::uint32_t>(288 + e.stored.size()));
            putName(b, e.name);
            putU32(b, e.originalSize);
            putU32(b, static_cast<std::uint32_t>(e.stored.size()));
            putU32(b, e.hash);
            b.resize(b.size() + 12, 0);
            putU32(b, e.flags);
            offsets.push_back(static_cast<std::uint32_t>(b.size()));
            b.insert(b.end(), e.stored.begin(), e.stored.end());
        }

        setU32(b, idxOffsetAt, static_cast<std::uint32_t>(b.size()));
        b.insert(b.end(), {'I', 'D', 'X', ' '});
        putU32(b, static_cast<std::uint32_t>(entries_.size() * 296));
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const auto& e = entries_[i];
            std::string lower = e.name;
            for (char& c : lower) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            putU32(b, offsets[i]);
            putU32(b, static_cast<std::uint32_t>(e.stored.size()));
            putName(b, lower);
            putU32(b, e.originalSize);
            putU32(b, static_cast<std::uint32_t>(e.stored.size()));
            putU32(b, e.hash);
            b.resize(b.size() + 12, 0);
            putU32(b, e.flags);
        }
        return b;
    }

private:
    std::uint32_t version_;
    std::vector<TestEntry> entries_;
};

// A private temporary folder, removed when the object goes away.
class TempDir {
public:
    TempDir() {
        std::string tmpl = (std::filesystem::temp_directory_path() / "trainz-native-test-XXXXXX").string();
        if (::mkdtemp(tmpl.data()) == nullptr) {
            throw std::runtime_error("mkdtemp failed");
        }
        path_ = tmpl;
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    const std::filesystem::path& path() const { return path_; }

    std::filesystem::path write(const std::string& name, const Bytes& data) const {
        const auto p = path_ / name;
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) {
            throw std::runtime_error("cannot write test file " + p.string());
        }
        return p;
    }

private:
    std::filesystem::path path_;
};

inline Bytes patternBytes(std::size_t n, std::uint32_t seed) {
    Bytes b(n);
    std::uint32_t x = seed * 2654435761u + 1;
    for (auto& v : b) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        v = static_cast<std::uint8_t>(x);
    }
    return b;
}

}  // namespace trainz::test
