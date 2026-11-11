#include "AssetConfig.h"

#include "ByteOrder.h"
#include "ContentError.h"

#include <charconv>
#include <cstdio>
#include <cstring>

namespace trainz::content {

namespace {

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') {
            x = static_cast<char>(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = static_cast<char>(y - 'A' + 'a');
        }
        if (x != y) {
            return false;
        }
    }
    return true;
}

float loadFloatLE(const std::uint8_t* p) {
    const std::uint32_t bits = loadU32LE(p);
    float f;
    static_assert(sizeof f == sizeof bits);
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// ------------------------------------------------------------------ .chump

// The item walk follows trainz.exe 0x7ff1f0:
//   while more than 5 bytes remain:
//     u32 size, u8 keyLen, key[keyLen], u8 type, value  (size counts from keyLen on)
//     type 0 -> the value is a nested item list of (size - keyLen - 2) bytes
//   up to 5 left-over bytes at the end of a list are ignored.
// The game checks that the type byte and the item lie inside the list; we also
// check sizes that the game's arithmetic would let underflow.
void parseChumpItems(const std::uint8_t* p, std::size_t len, ConfigNode& parent, std::size_t depth,
                     std::size_t baseOffset, AssetConfig& cfg) {
    if (depth > kMaxConfigDepth) {
        throw ContentError("chump groups nested more than " + std::to_string(kMaxConfigDepth) + " deep");
    }
    std::size_t pos = 0;
    while (len - pos > 5) {
        const std::size_t at = baseOffset + pos;
        const std::uint32_t itemSize = loadU32LE(p + pos);
        const std::size_t keyLen = p[pos + 4];
        const std::size_t typeAt = pos + 5 + keyLen;
        if (typeAt >= len) {
            throw ContentError("chump item at byte " + std::to_string(at) + ": key runs past the end");
        }
        if (itemSize > len - pos - 4) {
            throw ContentError("chump item at byte " + std::to_string(at) + ": size " + std::to_string(itemSize) +
                               " runs past the end");
        }
        if (itemSize < keyLen + 2) {
            throw ContentError("chump item at byte " + std::to_string(at) + ": size is smaller than its key");
        }
        const std::uint8_t* keyBytes = p + pos + 5;
        const void* nul = std::memchr(keyBytes, 0, keyLen);
        const std::size_t keyChars =
            nul ? static_cast<std::size_t>(static_cast<const std::uint8_t*>(nul) - keyBytes) : keyLen;

        ConfigNode node;
        node.key.assign(reinterpret_cast<const char*>(keyBytes), keyChars);
        node.chumpType = p[typeAt];
        const std::uint8_t* value = p + typeAt + 1;
        const std::size_t valueLen = itemSize - keyLen - 2;

        switch (node.chumpType) {
            case 0:
                node.type = ConfigType::Group;
                parseChumpItems(value, valueLen, node, depth + 1, baseOffset + typeAt + 1, cfg);
                break;
            case 1:
            case 2:
                if (valueLen % 4 != 0) {
                    cfg.warnings.push_back("'" + node.key + "': number list of " + std::to_string(valueLen) +
                                           " bytes is not a multiple of 4; kept as raw bytes");
                    node.type = ConfigType::Unknown;
                    node.bytes.assign(value, value + valueLen);
                    break;
                }
                for (std::size_t i = 0; i < valueLen; i += 4) {
                    if (node.chumpType == 1) {
                        node.ints.push_back(static_cast<std::int32_t>(loadU32LE(value + i)));
                    } else {
                        node.floats.push_back(loadFloatLE(value + i));
                    }
                }
                node.type = node.chumpType == 1 ? ConfigType::Integers : ConfigType::Floats;
                break;
            case 3: {
                node.type = ConfigType::String;
                std::size_t n = valueLen;
                while (n > 0 && value[n - 1] == 0) {  // NUL-terminated, sometimes padded with extra NULs
                    --n;
                }
                node.text.assign(reinterpret_cast<const char*>(value), n);
                break;
            }
            case 4:
                node.type = ConfigType::Binary;
                node.bytes.assign(value, value + valueLen);
                break;
            case 5:
                node.type = ConfigType::Null;
                break;
            case 13:
                if (valueLen != 8) {
                    cfg.warnings.push_back("'" + node.key + "': KUID value of " + std::to_string(valueLen) +
                                           " bytes; kept as raw bytes");
                    node.type = ConfigType::Unknown;
                    node.bytes.assign(value, value + valueLen);
                    break;
                }
                node.type = ConfigType::Kuid;
                node.kuid = unpackChumpKuid(loadU32LE(value), static_cast<std::int32_t>(loadU32LE(value + 4)));
                break;
            default:
                cfg.warnings.push_back("'" + node.key + "': unknown chump type " +
                                       std::to_string(node.chumpType) + "; kept as raw bytes");
                node.type = ConfigType::Unknown;
                node.bytes.assign(value, value + valueLen);
                break;
        }
        parent.children.push_back(std::move(node));
        pos += 4 + static_cast<std::size_t>(itemSize);
    }
}

// --------------------------------------------------------------- config.txt

class TextParser {
public:
    explicit TextParser(std::string_view text) : t_(text) {}

    void parseInto(ConfigNode& root) { parseBlock(root, 0); }

private:
    [[noreturn]] void fail(const std::string& what) const {
        throw ContentError("config.txt line " + std::to_string(line_) + ": " + what);
    }

    bool atEnd() const { return i_ >= t_.size(); }

    // Skips spaces, tabs, line ends and "//" comments.
    void skipSpace() {
        while (!atEnd()) {
            const char c = t_[i_];
            if (c == ' ' || c == '\t' || c == '\r') {
                ++i_;
            } else if (c == '\n') {
                ++i_;
                ++line_;
            } else if (c == '/' && i_ + 1 < t_.size() && t_[i_ + 1] == '/') {
                while (!atEnd() && t_[i_] != '\n') {
                    ++i_;
                }
            } else {
                break;
            }
        }
    }

    void skipBlanks() {
        while (!atEnd() && (t_[i_] == ' ' || t_[i_] == '\t')) {
            ++i_;
        }
    }

    void parseBlock(ConfigNode& parent, std::size_t depth) {
        if (depth > kMaxConfigDepth) {
            fail("groups nested more than " + std::to_string(kMaxConfigDepth) + " deep");
        }
        for (;;) {
            skipSpace();
            if (atEnd()) {
                if (depth > 0) {
                    fail("a '{' group is never closed with '}'");
                }
                return;
            }
            if (t_[i_] == '}') {
                if (depth == 0) {
                    fail("'}' without a matching '{'");
                }
                ++i_;
                return;
            }
            const std::size_t keyStart = i_;
            while (!atEnd() && !isKeyEnd(t_[i_])) {
                ++i_;
            }
            if (i_ == keyStart) {
                fail(std::string("expected a key, found '") + t_[i_] + "'");
            }
            ConfigNode node;
            node.key.assign(t_.substr(keyStart, i_ - keyStart));
            skipBlanks();
            if (!atEnd() && t_[i_] == '"') {
                parseString(node);
            } else if (!atEnd() && t_[i_] == '{') {
                ++i_;
                node.type = ConfigType::Group;
                parseBlock(node, depth + 1);
            } else if (atEnd() || t_[i_] == '\r' || t_[i_] == '\n') {
                // Nothing else on the line: a group whose '{' is on the next line, or a key with no value.
                const std::size_t saveI = i_;
                const std::size_t saveLine = line_;
                skipSpace();
                if (!atEnd() && t_[i_] == '{') {
                    ++i_;
                    node.type = ConfigType::Group;
                    parseBlock(node, depth + 1);
                } else if (!atEnd() && t_[i_] == '"') {
                    // The value is a string that starts on the next line (seen in
                    // Builtin\3.ja kuid -13:151000 "description"). Keys never start with '"'.
                    parseString(node);
                } else {
                    i_ = saveI;
                    line_ = saveLine;
                    node.type = ConfigType::Null;
                }
            } else {
                parseBareValue(node);
            }
            parent.children.push_back(std::move(node));
        }
    }

    static bool isKeyEnd(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}' || c == '"';
    }

    // "..." may run over several lines. No escape sequences were found in any sample.
    void parseString(ConfigNode& node) {
        const std::size_t startLine = line_;
        const std::size_t close = t_.find('"', i_ + 1);
        if (close == std::string_view::npos) {
            line_ = startLine;
            fail("string for '" + node.key + "' is never closed with '\"'");
        }
        node.type = ConfigType::String;
        node.text.assign(t_.substr(i_ + 1, close - i_ - 1));
        for (char c : node.text) {
            if (c == '\n') {
                ++line_;
            }
        }
        i_ = close + 1;
    }

    // An unquoted value runs to the end of the line.
    void parseBareValue(ConfigNode& node) {
        const std::size_t start = i_;
        while (!atEnd() && t_[i_] != '\r' && t_[i_] != '\n') {
            ++i_;
        }
        std::string_view v = t_.substr(start, i_ - start);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) {
            v.remove_suffix(1);
        }
        node.text.assign(v);
        if (!v.empty() && v.front() == '<') {
            if (auto k = parseKuid(v)) {
                node.type = ConfigType::Kuid;
                node.kuid = *k;
                return;
            }
        }
        if (parseNumbers(v, node)) {
            return;
        }
        node.type = ConfigType::String;
    }

    // "1", "1,2,3", "0.5, -2" -> Integers or Floats.
    static bool parseNumbers(std::string_view v, ConfigNode& node) {
        std::vector<std::int32_t> ints;
        std::vector<float> floats;
        bool allInts = true;
        std::size_t pos = 0;
        while (pos <= v.size()) {
            std::size_t comma = v.find(',', pos);
            if (comma == std::string_view::npos) {
                comma = v.size();
            }
            std::string_view part = v.substr(pos, comma - pos);
            while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) {
                part.remove_prefix(1);
            }
            while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) {
                part.remove_suffix(1);
            }
            if (part.empty()) {
                return false;
            }
            const char* first = part.data();
            const char* last = part.data() + part.size();
            std::int32_t iv = 0;
            auto [ip, iec] = std::from_chars(first, last, iv);
            float fv = 0;
            auto [fp, fec] = std::from_chars(first, last, fv);
            if (fec != std::errc() || fp != last) {
                return false;
            }
            if (iec != std::errc() || ip != last) {
                allInts = false;
            }
            ints.push_back(iv);
            floats.push_back(fv);
            pos = comma + 1;
        }
        if (allInts) {
            node.type = ConfigType::Integers;
            node.ints = std::move(ints);
        } else {
            node.type = ConfigType::Floats;
            node.floats = std::move(floats);
        }
        return true;
    }

    std::string_view t_;
    std::size_t i_ = 0;
    std::size_t line_ = 1;
};

void formatNode(const ConfigNode& n, std::size_t depth, std::string& out) {
    const std::string indent(depth * 2, ' ');
    out += indent;
    out += n.key;
    if (n.key.size() < 38) {
        out.append(38 - n.key.size(), ' ');
    }
    out += "  ";
    char buf[64];
    switch (n.type) {
        case ConfigType::Group:
            out += "\n" + indent + "{\n";
            for (const auto& c : n.children) {
                formatNode(c, depth + 1, out);
            }
            out += indent + "}\n";
            return;
        case ConfigType::Integers:
            for (std::size_t i = 0; i < n.ints.size(); ++i) {
                out += (i ? "," : "") + std::to_string(n.ints[i]);
            }
            break;
        case ConfigType::Floats:
            for (std::size_t i = 0; i < n.floats.size(); ++i) {
                std::snprintf(buf, sizeof buf, "%s%.7g", i ? "," : "", static_cast<double>(n.floats[i]));
                out += buf;
            }
            break;
        case ConfigType::String:
            out += "\"" + n.text + "\"";
            break;
        case ConfigType::Kuid:
            out += formatKuid(n.kuid);
            break;
        case ConfigType::Binary:
            out += "<" + std::to_string(n.bytes.size()) + " bytes of binary data>";
            break;
        case ConfigType::Null:
            break;
        case ConfigType::Unknown:
            out += "<unknown type " + std::to_string(n.chumpType) + ", " + std::to_string(n.bytes.size()) +
                   " bytes>";
            break;
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    out += "\n";
}

}  // namespace

const ConfigNode* ConfigNode::child(std::string_view name) const {
    for (const auto& c : children) {
        if (equalsNoCase(c.key, name)) {
            return &c;
        }
    }
    return nullptr;
}

std::optional<trainz::content::Kuid> ConfigNode::asKuid() const {
    if (type == ConfigType::Kuid) {
        return kuid;
    }
    if (type == ConfigType::String) {
        return parseKuidLoose(text);
    }
    return std::nullopt;
}

std::optional<trainz::content::Kuid> AssetConfig::assetKuid() const {
    const ConfigNode* k = root.child("kuid");
    if (k == nullptr) {
        return std::nullopt;
    }
    // A bare number ("kuid 101135") is the content number with user -1. Observed, not
    // traced: in all 488 disc assets that use it inside a KUID-named folder, the folder
    // is "kuid _1 <that number>". (12 more are in Base.ja/Data.ja folders not named
    // after a KUID, so they don't confirm it either way.)
    if (k->type == ConfigType::Integers && k->ints.size() == 1) {
        return trainz::content::Kuid{-1, k->ints[0], std::nullopt};
    }
    // The same written as text ("101060"): one more disc asset, folder "kuid _1 101060".
    if (k->type == ConfigType::String && !k->text.empty() && k->text.size() <= 9 &&
        k->text.find_first_not_of("0123456789") == std::string::npos) {
        return trainz::content::Kuid{-1, std::stoi(k->text), std::nullopt};
    }
    return k->asKuid();
}

AssetConfig parseChump(std::span<const std::uint8_t> data) {
    // trainz.exe 0x7ff710: "ACS$", u32 (version), u32, u32 body size, body.
    if (data.size() < 16) {
        throw ContentError("chump file is too small (" + std::to_string(data.size()) + " bytes)");
    }
    if (std::memcmp(data.data(), "ACS$", 4) != 0) {
        throw ContentError("not a chump file (no \"ACS$\" at the start)");
    }
    AssetConfig cfg;
    cfg.format = ConfigFormat::Chump;
    cfg.chumpVersion = loadU32LE(data.data() + 4);
    const std::uint32_t bodySize = loadU32LE(data.data() + 12);
    if (bodySize > data.size() - 16) {
        throw ContentError("chump body of " + std::to_string(bodySize) + " bytes runs past the end of the file");
    }
    if (data.size() - 16 > bodySize) {
        cfg.warnings.push_back(std::to_string(data.size() - 16 - bodySize) + " bytes after the chump body are ignored");
    }
    cfg.root.type = ConfigType::Group;
    parseChumpItems(data.data() + 16, bodySize, cfg.root, 0, 16, cfg);
    return cfg;
}

AssetConfig parseConfigText(std::span<const std::uint8_t> data) {
    std::string_view t(reinterpret_cast<const char*>(data.data()), data.size());
    if (t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF && static_cast<unsigned char>(t[1]) == 0xBB &&
        static_cast<unsigned char>(t[2]) == 0xBF) {
        t.remove_prefix(3);  // UTF-8 byte order mark
    }
    AssetConfig cfg;
    cfg.format = ConfigFormat::Text;
    cfg.root.type = ConfigType::Group;
    TextParser(t).parseInto(cfg.root);
    return cfg;
}

AssetConfig parseAssetConfig(std::span<const std::uint8_t> data) {
    if (data.size() >= 4 && std::memcmp(data.data(), "ACS$", 4) == 0) {
        return parseChump(data);
    }
    return parseConfigText(data);
}

std::string formatConfigText(const ConfigNode& root) {
    std::string out;
    for (const auto& c : root.children) {
        formatNode(c, 0, out);
    }
    return out;
}

}  // namespace trainz::content
