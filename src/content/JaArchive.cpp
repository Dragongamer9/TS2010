#include "JaArchive.h"

#include "ArchivePath.h"
#include "ByteOrder.h"
#include "CfilCodec.h"
#include "ContentError.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace trainz::content {

namespace {

constexpr std::uint32_t kArc0Magic = 0x41524330;  // 'ARC0', compared at 0x10024ceb
constexpr std::uint32_t kInfoBlockSize = 0x20;    // passed to the chunk reader at 0x10024cd6
constexpr std::size_t kIndexBatch = 0x1000;       // the game reads 4096 rows at a time (0x10026636)
constexpr std::size_t kSourceChunk = 1 << 20;     // 1 MiB reads for streaming
constexpr std::size_t kMaxWarnings = 200;

// Offsets inside one 296-byte IDX row.
constexpr std::size_t kRowOffset = 0;
constexpr std::size_t kRowStoredSize = 4;
constexpr std::size_t kRowName = 8;
constexpr std::size_t kRowOriginalSize = kRowName + kJaNameFieldSize;  // 268
constexpr std::size_t kRowStoredSizeCopy = kRowOriginalSize + 4;       // 272
constexpr std::size_t kRowHash = kRowStoredSizeCopy + 4;               // 276
constexpr std::size_t kRowFlags = kRowHash + 4 + 12;                   // 292 (u16, read at 0x10026e94)

std::string describe(const std::filesystem::path& p) { return "'" + p.string() + "'"; }

}  // namespace

// ---------------------------------------------------------------- JaArchive

JaArchive::JaArchive(std::shared_ptr<const ReadOnlyFile> file, JaArchiveInfo info)
    : file_(std::move(file)), info_(info) {}

JaArchive JaArchive::open(const std::filesystem::path& path) {
    auto file = std::make_shared<const ReadOnlyFile>(ReadOnlyFile::open(path));
    const std::string where = describe(path);

    if (file->size() < kJaHeaderSize) {
        throw ContentError(where + " is too small to be a .ja archive (" + std::to_string(file->size()) +
                           " bytes)");
    }
    std::array<std::uint8_t, kJaHeaderSize> h{};
    file->readExact(0, h);

    // 0x10024ca6..0x10024cce: 4 bytes compared with "ARCH" (string 0x10053448).
    if (std::memcmp(h.data(), "ARCH", 4) != 0) {
        throw ContentError(where + " is not a Jet archive (it does not start with \"ARCH\")");
    }
    // 0x10024cd6: then an "INFO" chunk (string 0x10053450) of 0x20 bytes.
    if (std::memcmp(h.data() + 4, "INFO", 4) != 0) {
        throw ContentError(where + ": missing \"INFO\" block after \"ARCH\"");
    }
    const std::uint32_t infoSize = loadU32LE(h.data() + 8);
    if (infoSize != kInfoBlockSize) {
        throw ContentError(where + ": INFO block is " + std::to_string(infoSize) + " bytes, expected 32");
    }
    const std::uint32_t magic = loadU32LE(h.data() + 12);
    if (magic != kArc0Magic) {
        throw ContentError(where + ": INFO block does not hold the 'ARC0' marker");
    }

    JaArchiveInfo info;
    info.version = loadU32LE(h.data() + 16);
    // 0x10024cfd..0x10024d09: only versions 0x101..0x102 are accepted.
    if (info.version < 0x101 || info.version > 0x102) {
        throw ContentError(where + ": unsupported archive version 0x" + [&] {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%x", info.version);
            return std::string(buf);
        }());
    }
    info.fileCount = loadU32LE(h.data() + 20);
    info.indexOffset = loadU32LE(h.data() + 24);
    info.infoField5 = loadU32LE(h.data() + 28);
    info.fileSize = file->size();

    JaArchive archive(std::move(file), info);
    archive.loadIndex();
    return archive;
}

void JaArchive::loadIndex() {
    const std::string where = describe(file_->path());
    std::size_t warningCount = 0;
    auto warn = [&](std::string message) {
        ++warningCount;
        if (warnings_.size() < kMaxWarnings) {
            warnings_.push_back(std::move(message));
        }
    };

    const std::uint64_t fileSize = file_->size();
    const std::uint64_t idx = info_.indexOffset;
    if (idx < kJaHeaderSize || idx > fileSize || fileSize - idx < 8) {
        throw ContentError(where + ": index offset " + std::to_string(idx) + " is outside the file");
    }
    // 0x100265c2..0x100265f8: seek to the index, read 8 bytes, compare with "IDX ".
    std::array<std::uint8_t, 8> ih{};
    file_->readExact(idx, ih);
    if (std::memcmp(ih.data(), "IDX ", 4) != 0) {
        throw ContentError(where + ": no \"IDX \" block at offset " + std::to_string(idx));
    }
    info_.indexBlockSize = loadU32LE(ih.data() + 4);

    const std::uint64_t tableBytes = static_cast<std::uint64_t>(info_.fileCount) * kJaIndexEntrySize;
    if (tableBytes > fileSize - idx - 8) {
        throw ContentError(where + ": the index for " + std::to_string(info_.fileCount) +
                           " entries does not fit in the file (damaged or cut short?)");
    }
    if (info_.indexBlockSize != tableBytes) {
        // The game ignores this field; it reads fileCount rows regardless.
        warn("IDX block size is " + std::to_string(info_.indexBlockSize) + " but " +
             std::to_string(info_.fileCount) + " rows need " + std::to_string(tableBytes));
    }

    entries_.reserve(info_.fileCount);
    byName_.reserve(info_.fileCount);
    std::vector<std::uint8_t> buf;
    for (std::uint64_t first = 0; first < info_.fileCount; first += kIndexBatch) {
        const std::size_t rows = static_cast<std::size_t>(
            std::min<std::uint64_t>(kIndexBatch, info_.fileCount - first));
        buf.resize(rows * kJaIndexEntrySize);
        file_->readExact(idx + 8 + first * kJaIndexEntrySize, buf);

        for (std::size_t i = 0; i < rows; ++i) {
            const std::uint8_t* r = buf.data() + i * kJaIndexEntrySize;
            const auto rowNumber = static_cast<std::uint32_t>(first + i);
            const char* nameField = reinterpret_cast<const char*>(r + kRowName);
            const void* nul = std::memchr(nameField, 0, kJaNameFieldSize);
            if (nul == nullptr) {
                // 0x100266d0..0x100266e0: the game stops reading the index here.
                throw ContentError(where + ": index row " + std::to_string(rowNumber) +
                                   " has a name that is not terminated within 260 bytes");
            }
            const auto len = static_cast<std::size_t>(static_cast<const char*>(nul) - nameField);
            if (len == 0) {
                // 0x1002610c: empty names are skipped.
                warn("index row " + std::to_string(rowNumber) + " has an empty name and is skipped");
                continue;
            }
            if (len > kMaxArchiveNameLength) {
                // 0x100237ab: names of 259+ characters become empty and are skipped.
                warn("index row " + std::to_string(rowNumber) + " has a " + std::to_string(len) +
                     "-character name; the game skips names this long");
                continue;
            }

            JaEntry e;
            e.name = asciiLower(std::string_view(nameField, len));  // _strlwr at 0x100260e9
            e.dataOffset = loadU32LE(r + kRowOffset);
            e.storedSize = loadU32LE(r + kRowStoredSize);
            e.originalSize = loadU32LE(r + kRowOriginalSize);
            e.storedHash = loadU32LE(r + kRowHash);
            e.flags = loadU16LE(r + kRowFlags);
            e.indexPosition = rowNumber;

            if (loadU32LE(r + kRowStoredSizeCopy) != e.storedSize) {
                warn("'" + e.name + "': the two stored-size fields in its index row differ");
            }
            if (e.dataOffset + static_cast<std::uint64_t>(e.storedSize) > fileSize) {
                warn("'" + e.name + "': its data (" + std::to_string(e.storedSize) + " bytes at " +
                     std::to_string(e.dataOffset) + ") lies past the end of the file");
            }

            // The game's name table keeps every row and its lookup returns the
            // first match in insertion order (0x10027a30 / 0x10027b70), so the
            // first row with a given name wins.
            const auto [it, inserted] = byName_.emplace(e.name, entries_.size());
            if (!inserted) {
                warn("'" + e.name + "' appears more than once; row " +
                     std::to_string(entries_[it->second].indexPosition) + " is used");
            }
            entries_.push_back(std::move(e));
        }
    }
    if (warningCount > warnings_.size()) {
        warnings_.push_back("... and " + std::to_string(warningCount - warnings_.size()) + " more warnings");
    }
}

const JaEntry* JaArchive::find(std::string_view name) const {
    const auto key = makeLookupKey(name);
    if (!key) {
        return nullptr;
    }
    const auto it = byName_.find(*key);
    return it == byName_.end() ? nullptr : &entries_[it->second];
}

std::vector<const JaEntry*> JaArchive::listFolder(std::string_view folder) const {
    std::vector<const JaEntry*> out;
    const auto key = makeLookupKey(folder);
    if (!key) {
        return out;
    }
    std::string prefix = *key;
    if (!prefix.empty() && prefix.back() != '/') {
        prefix.push_back('/');
    }
    for (const auto& e : entries_) {
        if (e.name.compare(0, prefix.size(), prefix) == 0) {
            out.push_back(&e);
        }
    }
    return out;
}

std::vector<const JaEntry*> JaArchive::findKuid(const Kuid& kuid, bool anyVersion) const {
    if (kuid.version || !anyVersion) {
        return listFolder(kuidFolderKey(kuid));
    }
    // "hash-xx/kuid u c/..." plus "hash-xx/kuid2 u c <version>/..."
    const std::string plain = kuidFolderKey(kuid) + "/";
    Kuid any = kuid;
    any.version = 0;
    std::string versioned = kuidFolderKey(any);
    versioned.resize(versioned.size() - 1);  // drop the "0", keep "... kuid2 u c "
    std::vector<const JaEntry*> out;
    for (const auto& e : entries_) {
        if (e.name.compare(0, plain.size(), plain) == 0) {
            out.push_back(&e);
            continue;
        }
        if (e.name.compare(0, versioned.size(), versioned) == 0) {
            std::size_t p = versioned.size();
            std::size_t digits = 0;
            if (p < e.name.size() && e.name[p] == '-') {
                ++p;
            }
            while (p < e.name.size() && e.name[p] >= '0' && e.name[p] <= '9') {
                ++p;
                ++digits;
            }
            if (digits > 0 && p < e.name.size() && e.name[p] == '/') {
                out.push_back(&e);
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> JaArchive::read(const JaEntry& entry) const {
    if (entry.dataOffset + static_cast<std::uint64_t>(entry.storedSize) > file_->size()) {
        throw ContentError("'" + entry.name + "': its data lies past the end of " + describe(file_->path()));
    }
    std::vector<std::uint8_t> stored(entry.storedSize);
    file_->readExact(entry.dataOffset, stored);
    // 0x10020053: compression is recognised by the "CFIL" magic at the start of the data.
    if (hasCfilMagic(stored)) {
        try {
            return decodeCfil(stored, info_.version, entry.originalSize);
        } catch (const ContentError& err) {
            throw ContentError("'" + entry.name + "': " + err.what());
        }
    }
    return stored;
}

JaEntryStream JaArchive::openStream(const JaEntry& entry) const { return JaEntryStream(file_, entry, info_.version); }

// ------------------------------------------------------------ JaEntryStream

JaEntryStream::JaEntryStream(std::shared_ptr<const ReadOnlyFile> file, const JaEntry& entry,
                             std::uint32_t archiveVersion)
    : file_(std::move(file)), name_(entry.name), base_(entry.dataOffset), storedSize_(entry.storedSize) {
    if (base_ + storedSize_ > file_->size()) {
        throw ContentError("'" + name_ + "': its data lies past the end of the archive");
    }
    std::array<std::uint8_t, 12> peek{};
    const std::size_t peekLen = static_cast<std::size_t>(std::min<std::uint64_t>(peek.size(), storedSize_));
    file_->readExact(base_, std::span<std::uint8_t>(peek.data(), peekLen));
    const std::span<const std::uint8_t> head(peek.data(), peekLen);
    if (hasCfilMagic(head)) {
        CfilHeader h;
        try {
            h = parseCfilHeader(head, archiveVersion, entry.originalSize);
        } catch (const ContentError& err) {
            throw ContentError("'" + name_ + "': " + err.what());
        }
        compressed_ = true;
        size_ = h.originalSize;
        sourcePos_ = h.headerSize;
    } else {
        size_ = storedSize_;
    }
}

void JaEntryStream::sourceRead(std::uint8_t* dst, std::size_t n) {
    while (n > 0) {
        if (sourceBufPos_ == sourceBufLen_) {
            const std::uint64_t remain = storedSize_ - sourcePos_;
            if (remain == 0) {
                throw ContentError("'" + name_ + "': stored data ends before the file is complete");
            }
            const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(kSourceChunk, remain));
            sourceBuf_.resize(std::max(sourceBuf_.size(), chunk));
            file_->readExact(base_ + sourcePos_, std::span<std::uint8_t>(sourceBuf_.data(), chunk));
            sourcePos_ += chunk;
            sourceBufPos_ = 0;
            sourceBufLen_ = chunk;
        }
        const std::size_t take = std::min(n, sourceBufLen_ - sourceBufPos_);
        std::memcpy(dst, sourceBuf_.data() + sourceBufPos_, take);
        sourceBufPos_ += take;
        dst += take;
        n -= take;
    }
}

void JaEntryStream::refillBlock() {
    std::uint8_t hb[2];
    sourceRead(hb, 2);
    const std::uint16_t header = loadU16LE(hb);
    const std::uint64_t remaining = size_ - produced_;
    const std::size_t limit = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, kCfilBlockSize));
    block_.clear();
    blockPos_ = 0;
    if (header & kCfilRawFlag) {
        const std::size_t len = header & 0x7FFF;
        if (len == 0 || len > limit) {
            throw ContentError("'" + name_ + "': CFIL raw block has a bad length (" + std::to_string(len) + ")");
        }
        block_.resize(len);
        sourceRead(block_.data(), len);
    } else {
        const std::size_t len = header;
        if (len == 0) {
            throw ContentError("'" + name_ + "': CFIL packed block of length 0");
        }
        payload_.resize(len);
        sourceRead(payload_.data(), len);
        try {
            expandBpeBlock(payload_, block_, limit);
        } catch (const ContentError& err) {
            throw ContentError("'" + name_ + "': " + err.what());
        }
        if (block_.empty()) {
            throw ContentError("'" + name_ + "': CFIL packed block produced no data");
        }
    }
}

std::size_t JaEntryStream::read(std::span<std::uint8_t> out) {
    std::size_t written = 0;
    while (written < out.size() && produced_ < size_) {
        const std::size_t want = static_cast<std::size_t>(
            std::min<std::uint64_t>(out.size() - written, size_ - produced_));
        if (!compressed_) {
            sourceRead(out.data() + written, want);
            written += want;
            produced_ += want;
            continue;
        }
        if (blockPos_ == block_.size()) {
            refillBlock();
        }
        const std::size_t take = std::min(want, block_.size() - blockPos_);
        std::memcpy(out.data() + written, block_.data() + blockPos_, take);
        blockPos_ += take;
        written += take;
        produced_ += take;
    }
    return written;
}

}  // namespace trainz::content
