// Read-only reader for Trainz "Jet Archive" (.ja) files: Base.ja, Data.ja,
// Builtin\1.ja ... Builtin\8.ja.
//
// Follows Jet::FileSubArchive in KernelStandard_7c.dll (TS2010 SP2 build).
// The byte layout, what each part is based on and what is still unknown are
// written up in docs/formats/ja.md.
//
// Opening reads the 44-byte header and then the whole index (in large batches)
// with one seek, and keeps it in memory. Reading an entry is a single
// sequential read of its stored bytes. The file is never written.
#pragma once

#include "Kuid.h"
#include "ReadOnlyFile.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trainz::content {

inline constexpr std::size_t kJaHeaderSize = 44;      // "ARCH" "INFO" u32 + 32-byte info block
inline constexpr std::size_t kJaNameFieldSize = 260;  // char[260] (MAX_PATH)
inline constexpr std::size_t kJaEntryHeaderSize = 288;  // name + sizes + hash + 12 bytes + flags
inline constexpr std::size_t kJaIndexEntrySize = 296;   // 0x128: offset + stored size + entry header
inline constexpr std::uint16_t kJaFlagCompressed = 0x80;  // bit 7, File::Entry::IsCompressed (0x10015470)

struct JaArchiveInfo {
    std::uint32_t version = 0;       // 0x101 or 0x102
    std::uint32_t fileCount = 0;     // entries the header promises
    std::uint32_t indexOffset = 0;   // file offset of the "IDX " block
    std::uint32_t infoField5 = 0;    // last info field; meaning unknown (42203 / 42195 seen)
    std::uint32_t indexBlockSize = 0;  // size field of "IDX " (the game ignores it)
    std::uint64_t fileSize = 0;
};

struct JaEntry {
    std::string name;            // lowercased stored name, the key the game looks up
    std::uint64_t dataOffset = 0;  // absolute offset of the stored bytes
    std::uint32_t storedSize = 0;
    std::uint32_t originalSize = 0;
    std::uint32_t storedHash = 0;  // algorithm unknown; not checked by the game's reader
    std::uint16_t flags = 0;
    std::uint32_t indexPosition = 0;  // row number in the IDX table

    bool flaggedCompressed() const { return (flags & kJaFlagCompressed) != 0; }
};

// Streams one entry's decoded bytes with bounded memory (one 16 KiB block of
// output, up to 1 MiB of input buffered at a time).
class JaEntryStream {
public:
    JaEntryStream(std::shared_ptr<const ReadOnlyFile> file, const JaEntry& entry, std::uint32_t archiveVersion);

    // Total decoded size (known after construction).
    std::uint64_t size() const { return size_; }
    std::uint64_t position() const { return produced_; }
    bool compressed() const { return compressed_; }

    // Fills up to out.size() bytes; returns how many were written, 0 at the end.
    // Throws ContentError on damaged data.
    std::size_t read(std::span<std::uint8_t> out);

private:
    void sourceRead(std::uint8_t* dst, std::size_t n);
    void refillBlock();

    std::shared_ptr<const ReadOnlyFile> file_;
    std::string name_;
    std::uint64_t base_ = 0;
    std::uint64_t storedSize_ = 0;
    std::uint64_t sourcePos_ = 0;  // bytes of the stored range consumed
    std::vector<std::uint8_t> sourceBuf_;
    std::size_t sourceBufPos_ = 0;
    std::size_t sourceBufLen_ = 0;

    bool compressed_ = false;
    std::uint64_t size_ = 0;
    std::uint64_t produced_ = 0;
    std::vector<std::uint8_t> block_;
    std::size_t blockPos_ = 0;
    std::vector<std::uint8_t> payload_;
};

class JaArchive {
public:
    // Opens and indexes an archive. Throws ContentError if it is not a valid .ja file.
    static JaArchive open(const std::filesystem::path& path);

    const std::filesystem::path& path() const { return file_->path(); }
    const JaArchiveInfo& info() const { return info_; }

    // Entries in index order. Rows the game would skip (empty or over-long
    // names) are not included; see warnings().
    const std::vector<JaEntry>& entries() const { return entries_; }

    // Non-fatal oddities found while indexing (skipped rows, duplicates,
    // entries whose data lies outside the file).
    const std::vector<std::string>& warnings() const { return warnings_; }

    // Look up by file name, the way the game does: lowercase, canonicalize,
    // exact match; with duplicate names the first row in the index wins.
    const JaEntry* find(std::string_view name) const;

    // All entries whose name starts with folder + "/" (folder is lowercased and
    // canonicalized). Includes rows hidden by an earlier duplicate name.
    std::vector<const JaEntry*> listFolder(std::string_view folder) const;

    // False for a row whose name is already used by an earlier row: the game
    // never reads such a row (the first row with a name wins).
    bool isLookupWinner(const JaEntry& entry) const;

    // All files of one asset, from its "hash-XX/kuid ..." folder. With
    // anyVersion, a KUID without a version also matches every kuid2 folder with
    // the same user and content numbers.
    std::vector<const JaEntry*> findKuid(const Kuid& kuid, bool anyVersion = false) const;

    // Whole decoded entry in memory.
    std::vector<std::uint8_t> read(const JaEntry& entry) const;

    // Streaming access for large entries.
    JaEntryStream openStream(const JaEntry& entry) const;

private:
    JaArchive(std::shared_ptr<const ReadOnlyFile> file, JaArchiveInfo info);
    void loadIndex();

    std::shared_ptr<const ReadOnlyFile> file_;
    JaArchiveInfo info_;
    std::vector<JaEntry> entries_;
    std::unordered_map<std::string, std::size_t> byName_;
    std::vector<std::string> warnings_;
};

}  // namespace trainz::content
