// Where the port is allowed to write, and how it keeps away from the game.
//
// Rule for the whole project: everything the port makes (caches, indexes,
// settings, extracted files) goes under ~/.local/share/trainz-native/
// ($XDG_DATA_HOME/trainz-native when XDG_DATA_HOME is set). Nothing is ever
// written inside a Trainz install or the user's own content.
#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace trainz::common {

// ~/.local/share/trainz-native (or $XDG_DATA_HOME/trainz-native). Not created here.
// Throws std::runtime_error if neither HOME nor XDG_DATA_HOME is usable.
std::filesystem::path portDataDir();

// True if `path` is `dir` or lies inside it (both made absolute and
// normalized; existing parts are resolved through symlinks).
bool isInsideOrSame(const std::filesystem::path& path, const std::filesystem::path& dir);

// Walks up from a file inside a Trainz install (e.g. .../Builtin/8.ja) and
// returns the install folder if one is recognised (it holds Trainz.exe,
// bin/trainz.exe, a Builtin folder or a UserData folder). Looks at most 4 levels up.
std::optional<std::filesystem::path> findTrainzInstallRoot(const std::filesystem::path& fileInside);

// The folders that must never be written to for an archive at `archivePath`:
// the install root if found (otherwise the archive's own folder), plus
// $TRAINZ_DATA_DIR when it is set.
std::vector<std::filesystem::path> protectedDirsFor(const std::filesystem::path& archivePath);

}  // namespace trainz::common
