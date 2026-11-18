#include "DataDirs.h"

#include <cstdlib>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace trainz::common {

namespace fs = std::filesystem;

namespace {

fs::path normalized(const fs::path& p) {
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    if (ec) {
        abs = p;
    }
    fs::path canon = fs::weakly_canonical(abs, ec);
    if (ec) {
        canon = abs.lexically_normal();
    }
    return canon;
}

bool pathExists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

}  // namespace

fs::path portDataDir() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] == '/') {
        return fs::path(xdg) / "trainz-native";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] == '/') {
        return fs::path(home) / ".local" / "share" / "trainz-native";
    }
    throw std::runtime_error("cannot find the home folder (HOME is not set)");
}

bool isInsideOrSame(const fs::path& path, const fs::path& dir) {
    const fs::path p = normalized(path);
    const fs::path d = normalized(dir);
    auto pi = p.begin();
    for (auto di = d.begin(); di != d.end(); ++di) {
        if (di->empty()) {
            continue;  // trailing separator
        }
        if (pi == p.end() || *pi != *di) {
            return false;
        }
        ++pi;
    }
    return true;
}

std::optional<fs::path> findTrainzInstallRoot(const fs::path& fileInside) {
    fs::path dir = normalized(fileInside).parent_path();
    for (int level = 0; level < 5 && !dir.empty(); ++level) {
        if (pathExists(dir / "Trainz.exe") || pathExists(dir / "trainz.exe") || pathExists(dir / "bin" / "trainz.exe") ||
            pathExists(dir / "Builtin") || pathExists(dir / "builtin") || pathExists(dir / "UserData") ||
            pathExists(dir / "userdata")) {
            return dir;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return std::nullopt;
}

std::vector<fs::path> protectedDirsFor(const fs::path& archivePath) {
    std::vector<fs::path> dirs;
    if (auto root = findTrainzInstallRoot(archivePath)) {
        dirs.push_back(*root);
    } else {
        dirs.push_back(normalized(archivePath).parent_path());
    }
    if (const char* data = std::getenv("TRAINZ_DATA_DIR"); data != nullptr && data[0] != '\0') {
        dirs.push_back(normalized(data));
    }
    return dirs;
}

}  // namespace trainz::common
