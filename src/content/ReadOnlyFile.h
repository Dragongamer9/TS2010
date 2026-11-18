// A file opened strictly read-only.
//
// This is the only way src/content touches the user's game files. It opens
// with O_RDONLY (plus O_NOATIME when the kernel allows it, so not even the
// access time is updated), never truncates, never writes, and only accepts
// regular files. Reads use pread(), so there is no shared file position and a
// read can never go past the size measured at open time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace trainz::content {

class ReadOnlyFile {
public:
    // Throws ContentError if the path cannot be opened or is not a regular file.
    static ReadOnlyFile open(const std::filesystem::path& path);

    ReadOnlyFile(ReadOnlyFile&& other) noexcept;
    ReadOnlyFile& operator=(ReadOnlyFile&& other) noexcept;
    ReadOnlyFile(const ReadOnlyFile&) = delete;
    ReadOnlyFile& operator=(const ReadOnlyFile&) = delete;
    ~ReadOnlyFile();

    std::uint64_t size() const { return size_; }
    const std::filesystem::path& path() const { return path_; }

    // Reads exactly out.size() bytes starting at offset. Throws ContentError if
    // the range is outside the file or the read comes back short.
    void readExact(std::uint64_t offset, std::span<std::uint8_t> out) const;

    // Tells the kernel the file will be read front to back (helps on hard drives).
    void adviseSequential() const;

private:
    ReadOnlyFile(int fd, std::uint64_t size, std::filesystem::path path);

    int fd_ = -1;
    std::uint64_t size_ = 0;
    std::filesystem::path path_;
};

}  // namespace trainz::content
