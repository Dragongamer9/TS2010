// ja-tool: list, check and extract Trainz .ja archives, read-only.
//
//   ja-tool info    ARCHIVE
//   ja-tool list    ARCHIVE [--folder F] [--long]
//   ja-tool find    ARCHIVE NAME
//   ja-tool kuid    ARCHIVE KUID [--any-version]
//   ja-tool cat     ARCHIVE NAME            (decoded bytes to stdout)
//   ja-tool verify  ARCHIVE                 (decode every entry, report problems)
//   ja-tool extract ARCHIVE [--out DIR] [--folder F] [NAME...]
//
// Archives are only ever opened read-only. extract writes only inside
// ~/.local/share/trainz-native (default .../extracted/<archive>; --out must be
// inside it too), refuses anything inside the Trainz install or
// $TRAINZ_DATA_DIR, follows no symlinks below the port folder, and never
// overwrites an existing file.
#include "common/DataDirs.h"
#include "common/SafeOutput.h"
#include "content/ArchivePath.h"
#include "content/ContentError.h"
#include "content/JaArchive.h"
#include "content/Kuid.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace trainz::content;

namespace {

int usage() {
    std::cerr << "usage:\n"
                 "  ja-tool info    ARCHIVE\n"
                 "  ja-tool list    ARCHIVE [--folder F] [--long]\n"
                 "  ja-tool find    ARCHIVE NAME\n"
                 "  ja-tool kuid    ARCHIVE KUID [--any-version]\n"
                 "  ja-tool cat     ARCHIVE NAME\n"
                 "  ja-tool verify  ARCHIVE\n"
                 "  ja-tool extract ARCHIVE [--out DIR] [--folder F] [NAME...]\n";
    return 2;
}

std::string hex32(std::uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08x", v);
    return buf;
}

void printEntry(const JaEntry& e, bool longForm) {
    if (longForm) {
        std::printf("%10u %10u %s %s  %s\n", e.originalSize, e.storedSize, e.flaggedCompressed() ? "C" : "-",
                    hex32(e.storedHash).c_str(), e.name.c_str());
    } else {
        std::printf("%s\n", e.name.c_str());
    }
}

void printWarnings(const JaArchive& a) {
    for (const auto& w : a.warnings()) {
        std::cerr << "warning: " << w << "\n";
    }
}

// Turns an archive name into a safe relative path, or returns an empty path
// if the name could escape the output folder.
fs::path safeRelativePath(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos) {
        return {};
    }
    fs::path rel;
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t slash = name.find('/', start);
        const std::string part = name.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part.empty() || part == "." || part == "..") {
            return {};
        }
        rel /= part;
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return rel;
}

// Streams one entry's decoded bytes into an already-open new file.
void writeEntry(const JaArchive& archive, const JaEntry& entry, int fd) {
    auto stream = archive.openStream(entry);
    std::vector<std::uint8_t> buf(1 << 20);
    for (;;) {
        const std::size_t n = stream.read(buf);
        if (n == 0) {
            break;
        }
        std::size_t done = 0;
        while (done < n) {
            const ssize_t w = ::write(fd, buf.data() + done, n - done);
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw ContentError("cannot write '" + entry.name + "': " + std::strerror(errno));
            }
            done += static_cast<std::size_t>(w);
        }
    }
}

int cmdInfo(const JaArchive& a) {
    const auto& i = a.info();
    std::size_t flagged = 0;
    for (const auto& e : a.entries()) {
        flagged += e.flaggedCompressed() ? 1 : 0;
    }
    std::printf("archive:        %s\n", a.path().c_str());
    std::printf("file size:      %llu\n", static_cast<unsigned long long>(i.fileSize));
    std::printf("version:        0x%x\n", i.version);
    std::printf("files (header): %u\n", i.fileCount);
    std::printf("files (usable): %zu\n", a.entries().size());
    std::printf("compressed:     %zu (by flag)\n", flagged);
    std::printf("index offset:   %u\n", i.indexOffset);
    std::printf("info field 5:   %u (meaning unknown)\n", i.infoField5);
    std::printf("warnings:       %zu\n", a.warnings().size());
    printWarnings(a);
    return 0;
}

int cmdVerify(const JaArchive& a) {
    // Decode in offset order so the drive reads front to back.
    std::vector<const JaEntry*> order;
    order.reserve(a.entries().size());
    for (const auto& e : a.entries()) {
        order.push_back(&e);
    }
    std::sort(order.begin(), order.end(),
              [](const JaEntry* x, const JaEntry* y) { return x->dataOffset < y->dataOffset; });
    std::size_t bad = 0;
    std::size_t sizeMismatch = 0;
    std::size_t flagMismatch = 0;
    std::vector<std::uint8_t> buf(1 << 20);
    for (const JaEntry* e : order) {
        try {
            auto s = a.openStream(*e);
            std::uint64_t total = 0;
            for (std::size_t n; (n = s.read(buf)) != 0;) {
                total += n;
            }
            if (total != e->originalSize) {
                ++sizeMismatch;
                std::cerr << "size: '" << e->name << "' decodes to " << total << " bytes, index says "
                          << e->originalSize << "\n";
            }
            if (s.compressed() != e->flaggedCompressed()) {
                ++flagMismatch;
                std::cerr << "flag: '" << e->name << "' compression flag does not match its data\n";
            }
        } catch (const ContentError& err) {
            ++bad;
            std::cerr << "error: " << err.what() << "\n";
        }
    }
    printWarnings(a);
    std::printf("%zu entries checked: %zu unreadable, %zu size mismatches, %zu flag mismatches\n", order.size(), bad,
                sizeMismatch, flagMismatch);
    return (bad || sizeMismatch) ? 1 : 0;
}

int cmdExtract(const JaArchive& a, const fs::path& archivePath, fs::path outDir, const std::string& folder,
               const std::vector<std::string>& names) {
    if (outDir.empty()) {
        outDir = trainz::common::portDataDir() / "extracted" / archivePath.stem();
    }
    // Only inside ~/.local/share/trainz-native, never inside the install, no symlinks followed.
    std::optional<trainz::common::SafeOutputDir> out;
    try {
        out.emplace(trainz::common::SafeOutputDir::open(outDir, trainz::common::protectedDirsFor(archivePath)));
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << "\n";
        return 2;
    }

    std::vector<const JaEntry*> chosen;
    std::size_t hidden = 0;
    if (!names.empty()) {
        for (const auto& n : names) {
            const JaEntry* e = a.find(n);
            if (e == nullptr) {
                std::cerr << "not found: " << n << "\n";
                return 1;
            }
            chosen.push_back(e);
        }
    } else {
        // Pick the row the game would use for each name (first in the index)
        // before sorting by offset, so duplicates resolve like find().
        for (const JaEntry* e : a.listFolder(folder)) {
            if (a.isLookupWinner(*e)) {
                chosen.push_back(e);
            } else {
                ++hidden;
            }
        }
    }
    std::sort(chosen.begin(), chosen.end(),
              [](const JaEntry* x, const JaEntry* y) { return x->dataOffset < y->dataOffset; });
    chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());

    std::size_t ok = 0;
    std::size_t failed = 0;
    for (const JaEntry* e : chosen) {
        const fs::path rel = safeRelativePath(e->name);
        if (rel.empty()) {
            ++failed;
            std::cerr << "skipped unsafe name: '" << e->name << "'\n";
            continue;
        }
        try {
            out->createFile(rel, [&](int fd) { writeEntry(a, *e, fd); });
            ++ok;
        } catch (const std::exception& err) {
            ++failed;
            std::cerr << "error: " << err.what() << "\n";
        }
    }
    if (hidden > 0) {
        std::cerr << hidden << " duplicate row(s) skipped (the game uses the first row with each name)\n";
    }
    std::printf("extracted %zu file(s) to %s, %zu failed\n", ok, out->path().c_str(), failed);
    return failed ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        return usage();
    }
    const std::string cmd = argv[1];
    const fs::path archivePath = argv[2];
    std::vector<std::string> positional;
    std::string folder;
    fs::path outDir;
    bool longForm = false;
    bool anyVersion = false;
    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--long" || arg == "-l") {
            longForm = true;
        } else if (arg == "--any-version") {
            anyVersion = true;
        } else if (arg == "--folder" && i + 1 < argc) {
            folder = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else if (arg.starts_with("--")) {
            return usage();
        } else {
            positional.emplace_back(arg);
        }
    }

    try {
        const JaArchive archive = JaArchive::open(archivePath);
        if (cmd == "info") {
            return cmdInfo(archive);
        }
        if (cmd == "list") {
            for (const JaEntry* e : archive.listFolder(folder)) {
                printEntry(*e, longForm);
            }
            printWarnings(archive);
            return 0;
        }
        if (cmd == "find") {
            if (positional.size() != 1) {
                return usage();
            }
            const JaEntry* e = archive.find(positional[0]);
            if (e == nullptr) {
                std::cerr << "not found\n";
                return 1;
            }
            printEntry(*e, true);
            return 0;
        }
        if (cmd == "kuid") {
            if (positional.size() != 1) {
                return usage();
            }
            const auto k = parseKuid(positional[0]);
            if (!k) {
                std::cerr << "not a KUID: " << positional[0] << " (use <kuid:U:C> or <kuid2:U:C:V>)\n";
                return 2;
            }
            const auto found = archive.findKuid(*k, anyVersion);
            for (const JaEntry* e : found) {
                printEntry(*e, longForm);
            }
            if (found.empty()) {
                std::cerr << "no files for " << formatKuid(*k) << " (folder " << kuidFolderKey(*k) << ")\n";
                return 1;
            }
            return 0;
        }
        if (cmd == "cat") {
            if (positional.size() != 1) {
                return usage();
            }
            const JaEntry* e = archive.find(positional[0]);
            if (e == nullptr) {
                std::cerr << "not found\n";
                return 1;
            }
            auto s = archive.openStream(*e);
            std::vector<std::uint8_t> buf(1 << 20);
            for (std::size_t n; (n = s.read(buf)) != 0;) {
                if (std::fwrite(buf.data(), 1, n, stdout) != n) {
                    std::cerr << "write to stdout failed\n";
                    return 1;
                }
            }
            return 0;
        }
        if (cmd == "verify") {
            return cmdVerify(archive);
        }
        if (cmd == "extract") {
            return cmdExtract(archive, archivePath, outDir, folder, positional);
        }
        return usage();
    } catch (const ContentError& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
