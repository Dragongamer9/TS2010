#include "ReadOnlyFile.h"

#include "ContentError.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace trainz::content {

namespace {

std::string errnoText(int err) { return std::strerror(err); }

}  // namespace

ReadOnlyFile ReadOnlyFile::open(const std::filesystem::path& path) {
    // O_NOATIME is only allowed for the file's owner; fall back without it.
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NOATIME);
    if (fd < 0 && errno == EPERM) {
        fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY);
    }
    if (fd < 0) {
        throw ContentError("cannot open '" + path.string() + "' for reading: " + errnoText(errno));
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        const int err = errno;
        ::close(fd);
        throw ContentError("cannot stat '" + path.string() + "': " + errnoText(err));
    }
    if (!S_ISREG(st.st_mode)) {
        ::close(fd);
        throw ContentError("'" + path.string() + "' is not a regular file");
    }
    return ReadOnlyFile(fd, static_cast<std::uint64_t>(st.st_size), path);
}

ReadOnlyFile::ReadOnlyFile(int fd, std::uint64_t size, std::filesystem::path path)
    : fd_(fd), size_(size), path_(std::move(path)) {}

ReadOnlyFile::ReadOnlyFile(ReadOnlyFile&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), size_(other.size_), path_(std::move(other.path_)) {}

ReadOnlyFile& ReadOnlyFile::operator=(ReadOnlyFile&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = std::exchange(other.fd_, -1);
        size_ = other.size_;
        path_ = std::move(other.path_);
    }
    return *this;
}

ReadOnlyFile::~ReadOnlyFile() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void ReadOnlyFile::readExact(std::uint64_t offset, std::span<std::uint8_t> out) const {
    if (out.empty()) {
        return;
    }
    if (offset > size_ || out.size() > size_ - offset) {
        throw ContentError("'" + path_.string() + "': read of " + std::to_string(out.size()) +
                           " bytes at offset " + std::to_string(offset) + " goes past the end of the file (" +
                           std::to_string(size_) + " bytes)");
    }
    std::size_t done = 0;
    while (done < out.size()) {
        const ssize_t got = ::pread(fd_, out.data() + done, out.size() - done,
                                    static_cast<off_t>(offset + done));
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw ContentError("'" + path_.string() + "': read failed at offset " +
                               std::to_string(offset + done) + ": " + errnoText(errno));
        }
        if (got == 0) {
            throw ContentError("'" + path_.string() + "': file ended early at offset " +
                               std::to_string(offset + done) + " (did it shrink while open?)");
        }
        done += static_cast<std::size_t>(got);
    }
}

void ReadOnlyFile::adviseSequential() const {
    (void)::posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
}

}  // namespace trainz::content
