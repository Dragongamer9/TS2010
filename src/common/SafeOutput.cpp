#include "SafeOutput.h"

#include "DataDirs.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace trainz::common {

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string errnoText() { return std::strerror(errno); }

class Fd {
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    Fd(Fd&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
    Fd& operator=(Fd&& o) noexcept {
        if (this != &o) {
            reset();
            fd_ = std::exchange(o.fd_, -1);
        }
        return *this;
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    ~Fd() { reset(); }
    int get() const { return fd_; }
    int release() { return std::exchange(fd_, -1); }
    void reset() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_;
};

// The kernel's idea of where an open folder really is (no symlinks).
fs::path realPathOf(int fd) {
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/fd/" + std::to_string(fd), ec);
    if (ec) {
        fail("cannot tell where an output folder really is: " + ec.message());
    }
    return p;
}

void checkNotProtected(const fs::path& real, const std::vector<fs::path>& protectedDirs) {
    for (const auto& guard : protectedDirs) {
        if (isInsideOrSame(real, guard)) {
            fail("refusing to write into '" + real.string() + "': it is inside '" + guard.string() +
                 "', which belongs to the Trainz install or your content");
        }
    }
}

bool isPlainPart(const fs::path& part) {
    const std::string s = part.string();
    return !s.empty() && s != "." && s != ".." && s.find('/') == std::string::npos;
}

// Opens (creating if needed) the folder `name` inside dirFd without following
// symlinks. The protected folders are checked BEFORE anything is created: the
// parent is a real folder (opened with O_NOFOLLOW), so its real path plus
// `name` is exactly where a new folder would appear.
Fd openSubdir(int dirFd, const fs::path& name, const std::string& shownPath,
              const std::vector<fs::path>& protectedDirs) {
    checkNotProtected(realPathOf(dirFd) / name, protectedDirs);
    if (::mkdirat(dirFd, name.c_str(), 0755) != 0 && errno != EEXIST) {
        fail("cannot create folder '" + shownPath + "': " + errnoText());
    }
    const int fd = ::openat(dirFd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ELOOP || errno == ENOTDIR) {
            fail("refusing '" + shownPath + "': it is a symlink or not a folder");
        }
        fail("cannot open folder '" + shownPath + "': " + errnoText());
    }
    return Fd(fd);
}

}  // namespace

SafeOutputDir SafeOutputDir::open(const fs::path& requested, const std::vector<fs::path>& protectedDirs) {
    const fs::path port = portDataDir();
    std::error_code ec;
    // Check before creating anything: where the port folder and the requested
    // folder would really be (existing symlinks resolved) must not be protected.
    const fs::path portResolved = fs::weakly_canonical(fs::absolute(port, ec), ec);
    if (ec) {
        fail("cannot resolve '" + port.string() + "': " + ec.message());
    }
    checkNotProtected(portResolved, protectedDirs);
    const fs::path requestedResolved = fs::weakly_canonical(fs::absolute(requested, ec), ec);
    if (ec) {
        fail("cannot resolve output folder '" + requested.string() + "': " + ec.message());
    }
    checkNotProtected(requestedResolved, protectedDirs);
    if (!isInsideOrSame(requestedResolved, portResolved)) {
        fail("refusing to write to '" + requested.string() + "': output must be inside '" + portResolved.string() +
             "'");
    }

    fs::create_directories(port, ec);  // folders above the port folder are the user's own setup
    if (ec) {
        fail("cannot create '" + port.string() + "': " + ec.message());
    }
    Fd cur(::open(port.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (cur.get() < 0) {
        fail("cannot open '" + port.string() + "' (" + errnoText() + "); it must be a real folder, not a symlink");
    }
    const fs::path portReal = realPathOf(cur.get());
    checkNotProtected(portReal, protectedDirs);

    // Where the request points, resolved through any existing symlinks, must be inside the port folder.
    fs::path wanted = fs::weakly_canonical(fs::absolute(requested, ec), ec);
    if (ec) {
        fail("cannot resolve output folder '" + requested.string() + "': " + ec.message());
    }
    const fs::path rel = wanted.lexically_relative(portReal);
    if (rel.empty() || *rel.begin() == "..") {
        fail("refusing to write to '" + requested.string() + "': output must be inside '" + portReal.string() +
             "'");
    }
    fs::path shown = portReal;
    for (const auto& part : rel) {
        if (part == ".") {
            continue;
        }
        if (!isPlainPart(part)) {
            fail("refusing output folder '" + requested.string() + "'");
        }
        shown /= part;
        cur = openSubdir(cur.get(), part, shown.string(), protectedDirs);
    }
    const fs::path real = realPathOf(cur.get());
    checkNotProtected(real, protectedDirs);
    if (!isInsideOrSame(real, portReal)) {
        fail("refusing '" + real.string() + "': not inside '" + portReal.string() + "'");
    }
    return SafeOutputDir(cur.release(), real, protectedDirs);
}

SafeOutputDir::SafeOutputDir(SafeOutputDir&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), path_(std::move(other.path_)), protected_(std::move(other.protected_)) {}

SafeOutputDir::~SafeOutputDir() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void SafeOutputDir::createFile(const fs::path& relPath, const std::function<void(int fd)>& fill) const {
    if (relPath.empty() || relPath.is_absolute()) {
        fail("refusing file path '" + relPath.string() + "'");
    }
    std::vector<fs::path> parts;
    for (const auto& part : relPath) {
        if (!isPlainPart(part)) {
            fail("refusing file path '" + relPath.string() + "'");
        }
        parts.push_back(part);
    }

    // Walk the folders without following symlinks. dirFd stays owned by `held`
    // (or is fd_ itself at the top level).
    Fd held;
    int dirFd = fd_;
    fs::path shown = path_;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        shown /= parts[i];
        held = openSubdir(dirFd, parts[i], shown.string(), protected_);
        dirFd = held.get();
    }

    const fs::path& name = parts.back();
    shown /= name;
    checkNotProtected(realPathOf(dirFd) / name, protected_);
    const int fd = ::openat(dirFd, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (errno == EEXIST) {
            fail("'" + shown.string() + "' already exists; not overwriting it");
        }
        fail("cannot create '" + shown.string() + "': " + errnoText());
    }
    try {
        fill(fd);
    } catch (...) {
        ::close(fd);
        ::unlinkat(dirFd, name.c_str(), 0);  // the file this call just created, in the same folder
        throw;
    }
    if (::close(fd) != 0) {
        const std::string why = errnoText();
        ::unlinkat(dirFd, name.c_str(), 0);
        fail("cannot finish '" + shown.string() + "': " + why);
    }
}

}  // namespace trainz::common
