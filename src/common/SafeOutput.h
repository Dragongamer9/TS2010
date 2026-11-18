// Writing files only inside the port's own data folder, without following symlinks.
//
// SafeOutputDir is the only way the tools write files. It
//   - accepts an output folder only if it lies inside portDataDir()
//     (~/.local/share/trainz-native),
//   - refuses it if it lies inside a protected folder (the Trainz install, $TRAINZ_DATA_DIR),
//     checking BEFORE every folder or file it creates, including the port folder itself,
//   - creates and opens every folder from portDataDir() downwards with
//     openat()/mkdirat() and O_NOFOLLOW, so a symlink planted anywhere below
//     the port folder cannot redirect a write into the game install,
//   - creates files with O_CREAT|O_EXCL|O_NOFOLLOW (never replaces anything)
//     and removes only a partial file it created itself.
#pragma once

#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

namespace trainz::common {

class SafeOutputDir {
public:
    // Throws std::runtime_error with a readable reason if the folder is not allowed.
    static SafeOutputDir open(const std::filesystem::path& requested,
                              const std::vector<std::filesystem::path>& protectedDirs);

    SafeOutputDir(SafeOutputDir&& other) noexcept;
    SafeOutputDir& operator=(SafeOutputDir&&) = delete;
    SafeOutputDir(const SafeOutputDir&) = delete;
    SafeOutputDir& operator=(const SafeOutputDir&) = delete;
    ~SafeOutputDir();

    // The real (symlink-free) path of the folder.
    const std::filesystem::path& path() const { return path_; }

    // Creates a new file at relPath below this folder (making subfolders as
    // needed) and calls fill(fd) to write it. relPath must be relative with no
    // "." or ".." parts. Throws std::runtime_error if any folder on the way is a
    // symlink or not a folder, if the file already exists, or if fill throws
    // (the partial file is then removed).
    void createFile(const std::filesystem::path& relPath, const std::function<void(int fd)>& fill) const;

private:
    SafeOutputDir(int fd, std::filesystem::path path, std::vector<std::filesystem::path> protectedDirs)
        : fd_(fd), path_(std::move(path)), protected_(std::move(protectedDirs)) {}

    int fd_ = -1;
    std::filesystem::path path_;
    std::vector<std::filesystem::path> protected_;
};

}  // namespace trainz::common
