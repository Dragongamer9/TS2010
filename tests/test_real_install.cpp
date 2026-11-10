// Checks against a real Trainz Simulator 2010 install. Runs only when
// TRAINZ_DATA_DIR points at the install folder (the one holding Base.ja and
// Builtin/); otherwise every test here is skipped. Everything is read-only.
//
//   TRAINZ_DATA_DIR=/path/to/TS2010 ctest --test-dir build -R RealInstall --output-on-failure
//   TRAINZ_FULL_VERIFY=1 ...   decode every entry (slow on a hard drive: about 7 GB)
#include "content/ContentError.h"
#include "content/GroundFile.h"
#include "content/JaArchive.h"
#include "content/AnimationFile.h"
#include "content/AssetConfig.h"
#include "content/AssetIndex.h"
#include "content/Kuid.h"
#include "content/MeshFile.h"
#include "content/SoundFile.h"
#include "content/TextureFile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include <sys/stat.h>

using namespace trainz::content;
namespace fs = std::filesystem;

namespace {

std::vector<fs::path> findArchives(const fs::path& root) {
    std::vector<fs::path> out;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() > 2) {
            it.disable_recursion_pending();
            continue;
        }
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".ja" && it->is_regular_file(ec)) {
            out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

class RealInstall : public ::testing::Test {
protected:
    void SetUp() override {
        const char* dir = std::getenv("TRAINZ_DATA_DIR");
        if (dir == nullptr || dir[0] == '\0') {
            GTEST_SKIP() << "TRAINZ_DATA_DIR is not set";
        }
        root = dir;
        archives = findArchives(root);
        if (archives.empty()) {
            GTEST_SKIP() << "no .ja files found under " << root;
        }
        const char* full = std::getenv("TRAINZ_FULL_VERIFY");
        fullVerify = full != nullptr && std::string(full) == "1";
    }

    fs::path root;
    std::vector<fs::path> archives;
    bool fullVerify = false;
};

}  // namespace

TEST_F(RealInstall, EveryArchiveOpensAndLooksUp) {
    for (const auto& path : archives) {
        SCOPED_TRACE(path.string());
        const auto a = JaArchive::open(path);
        EXPECT_GT(a.entries().size(), 0u);
        EXPECT_LE(a.entries().size(), a.info().fileCount);
        for (const auto& w : a.warnings()) {
            std::cout << "  warning (" << path.filename().string() << "): " << w << "\n";
        }
        for (const auto& e : a.entries()) {
            const JaEntry* found = a.find(e.name);
            ASSERT_NE(found, nullptr) << e.name;
            EXPECT_EQ(found->name, e.name);
        }
    }
}

TEST_F(RealInstall, KuidFolderHashRule) {
    // Checks the observed hash-XX rule on every asset folder in every archive.
    const std::regex folder(R"(^hash-([0-9a-f]{2})/(kuid2? -?\d+ -?\d+(?: -?\d+)?)/)");
    std::size_t checked = 0;
    std::size_t mismatched = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        std::string last;
        for (const auto& e : a.entries()) {
            std::smatch m;
            if (!std::regex_search(e.name, m, folder) || m[2].str() == last) {
                continue;
            }
            last = m[2].str();
            const auto k = parseKuid(last);
            ASSERT_TRUE(k) << e.name;
            ++checked;
            if (kuidFolderKey(*k) != "hash-" + m[1].str() + "/" + last) {
                ++mismatched;
                std::cout << "  hash rule mismatch: " << e.name << "\n";
            }
        }
    }
    std::cout << "  hash rule checked on " << checked << " folders\n";
    EXPECT_EQ(mismatched, 0u);
}

TEST_F(RealInstall, EntriesDecodeToTheirSize) {
    for (const auto& path : archives) {
        SCOPED_TRACE(path.string());
        struct stat before {};
        ASSERT_EQ(::stat(path.c_str(), &before), 0);

        const auto a = JaArchive::open(path);
        std::vector<const JaEntry*> pick;
        const auto& all = a.entries();
        if (fullVerify) {
            for (const auto& e : all) {
                pick.push_back(&e);
            }
        } else {
            const std::size_t step = std::max<std::size_t>(1, all.size() / 200);
            for (std::size_t i = 0; i < all.size(); i += step) {
                pick.push_back(&all[i]);
            }
        }
        std::sort(pick.begin(), pick.end(),
                  [](const JaEntry* x, const JaEntry* y) { return x->dataOffset < y->dataOffset; });

        std::vector<std::uint8_t> buf(1 << 20);
        std::size_t bad = 0;
        for (const JaEntry* e : pick) {
            try {
                auto s = a.openStream(*e);
                std::uint64_t total = 0;
                for (std::size_t n; (n = s.read(buf)) != 0;) {
                    total += n;
                }
                if (total != e->originalSize) {
                    ++bad;
                    std::cout << "  size mismatch: " << e->name << " " << total << " vs " << e->originalSize << "\n";
                }
            } catch (const ContentError& err) {
                ++bad;
                std::cout << "  unreadable: " << err.what() << "\n";
            }
        }
        std::cout << "  " << path.filename().string() << ": decoded " << pick.size() << " of " << all.size()
                  << " entries, " << bad << " problems\n";
        EXPECT_EQ(bad, 0u);

        struct stat after {};
        ASSERT_EQ(::stat(path.c_str(), &after), 0);
        EXPECT_EQ(before.st_size, after.st_size);
        EXPECT_EQ(before.st_mtim.tv_sec, after.st_mtim.tv_sec);
        EXPECT_EQ(before.st_mtim.tv_nsec, after.st_mtim.tv_nsec);
    }
}

TEST_F(RealInstall, StreamAndWholeReadAgree) {
    // A few compressed and plain entries from each archive.
    for (const auto& path : archives) {
        SCOPED_TRACE(path.string());
        const auto a = JaArchive::open(path);
        int compressed = 0;
        int plain = 0;
        for (const auto& e : a.entries()) {
            if (e.originalSize > (8u << 20)) {
                continue;
            }
            int& counter = e.flaggedCompressed() ? compressed : plain;
            if (counter >= 5) {
                continue;
            }
            ++counter;
            const auto whole = a.read(e);
            auto s = a.openStream(e);
            std::vector<std::uint8_t> streamed;
            std::vector<std::uint8_t> buf(777);
            for (std::size_t n; (n = s.read(buf)) != 0;) {
                streamed.insert(streamed.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
            }
            EXPECT_EQ(whole, streamed) << e.name;
            if (compressed >= 5 && plain >= 5) {
                break;
            }
        }
    }
}

TEST_F(RealInstall, AssetConfigsParseAndMatchTheirFolder) {
    // Every config.txt / config.chump must parse, and an asset's own config
    // (directly in its hash-XX/kuid... folder) must name that folder's KUID.
    const std::regex folder(R"(^hash-[0-9a-f]{2}/(kuid2? -?\d+ -?\d+(?: -?\d+)?)/config\.(txt|chump)$)");
    std::size_t parsed = 0;
    std::size_t matched = 0;
    std::size_t failures = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        std::vector<const JaEntry*> configs;
        for (const auto& e : a.entries()) {
            const auto slash = e.name.rfind('/');
            const std::string base = slash == std::string::npos ? e.name : e.name.substr(slash + 1);
            if (base == "config.txt" || base == "config.chump") {
                configs.push_back(&e);
            }
        }
        std::sort(configs.begin(), configs.end(),
                  [](const JaEntry* x, const JaEntry* y) { return x->dataOffset < y->dataOffset; });
        for (const JaEntry* e : configs) {
            try {
                const auto cfg = trainz::content::parseAssetConfig(a.read(*e));
                ++parsed;
                std::smatch m;
                if (std::regex_match(e->name, m, folder)) {
                    const auto want = parseKuid(m[1].str());
                    const auto got = cfg.assetKuid();
                    if (want && got && got->normalized() == want->normalized()) {
                        ++matched;
                    } else {
                        ++failures;
                        std::cout << "  kuid mismatch: " << e->name << " says "
                                  << (got ? formatKuid(*got) : std::string("nothing")) << "\n";
                    }
                }
            } catch (const ContentError& err) {
                ++failures;
                std::cout << "  cannot parse " << e->name << ": " << err.what() << "\n";
            }
        }
    }
    std::cout << "  configs parsed: " << parsed << ", asset KUIDs matching their folder: " << matched << "\n";
    EXPECT_EQ(failures, 0u);
}

TEST_F(RealInstall, AssetIndexOfTheWholeInstall) {
    // TRAINZ_DATA_DIR should be the install folder (holding Base.ja and Builtin/).
    const auto idx = trainz::content::AssetIndex::scanInstall(root);
    std::size_t kuidWarnings = 0;
    for (const auto& w : idx.warnings()) {
        if (w.find("folder says") != std::string::npos) {
            ++kuidWarnings;
        }
        std::cout << "  index warning: " << w << "\n";
    }
    std::cout << "  assets indexed: " << idx.records().size() << "\n";
    EXPECT_GT(idx.records().size(), 0u);
    EXPECT_EQ(kuidWarnings, 0u);
}

TEST_F(RealInstall, TexturesParseAndDecode) {
    // Every .texture must parse with mip sizes matching its format; a sample is also decoded.
    std::size_t parsed = 0;
    std::size_t failures = 0;
    std::map<std::string, std::size_t> formats;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        std::vector<const JaEntry*> textures;
        for (const auto& e : a.entries()) {
            if (e.name.size() > 8 && e.name.compare(e.name.size() - 8, 8, ".texture") == 0 && a.isLookupWinner(e)) {
                textures.push_back(&e);
            }
        }
        std::sort(textures.begin(), textures.end(),
                  [](const JaEntry* x, const JaEntry* y) { return x->dataOffset < y->dataOffset; });
        const std::size_t step = fullVerify ? 1 : std::max<std::size_t>(1, textures.size() / 300);
        for (std::size_t i = 0; i < textures.size(); i += step) {
            try {
                const auto tex = trainz::content::parseTexture(a.readLimited(*textures[i], std::size_t{128} << 20));
                ++parsed;
                ++formats[trainz::content::pixelFormatName(tex.info.format)];
                if (i % (step * 10) == 0) {
                    (void)trainz::content::decodeToRgba8(tex.mips.back(), tex.info.format);
                }
            } catch (const ContentError& err) {
                ++failures;
                std::cout << "  texture problem: " << textures[i]->name << ": " << err.what() << "\n";
            }
        }
    }
    std::cout << "  textures parsed: " << parsed << " (";
    for (const auto& [k, v] : formats) {
        std::cout << k << " " << v << " ";
    }
    std::cout << ")\n";
    EXPECT_EQ(failures, 0u);
}

TEST_F(RealInstall, MeshesParse) {
    // Every .im / .pm must parse: GEOM and MATL accounted for byte by byte, indices in range.
    std::size_t parsed = 0;
    std::size_t parts = 0;
    std::size_t failures = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        std::vector<const JaEntry*> meshes;
        for (const auto& e : a.entries()) {
            const bool isMesh = e.name.size() > 3 && (e.name.compare(e.name.size() - 3, 3, ".im") == 0 ||
                                                      e.name.compare(e.name.size() - 3, 3, ".pm") == 0);
            if (isMesh && a.isLookupWinner(e)) {
                meshes.push_back(&e);
            }
        }
        std::sort(meshes.begin(), meshes.end(),
                  [](const JaEntry* x, const JaEntry* y) { return x->dataOffset < y->dataOffset; });
        const std::size_t step = fullVerify ? 1 : std::max<std::size_t>(1, meshes.size() / 300);
        for (std::size_t i = 0; i < meshes.size(); i += step) {
            try {
                const auto m = trainz::content::parseMesh(a.readLimited(*meshes[i], std::size_t{256} << 20));
                ++parsed;
                parts += m.parts.size();
            } catch (const ContentError& err) {
                ++failures;
                std::cout << "  mesh problem: " << meshes[i]->name << ": " << err.what() << "\n";
            }
        }
    }
    std::cout << "  meshes parsed: " << parsed << " (" << parts << " parts)\n";
    EXPECT_EQ(failures, 0u);
}

TEST_F(RealInstall, AnimationsParse) {
    std::size_t parsed = 0;
    std::size_t failures = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        for (const auto& e : a.entries()) {
            if (e.name.size() <= 4 || e.name.compare(e.name.size() - 4, 4, ".kin") != 0 || !a.isLookupWinner(e)) {
                continue;
            }
            try {
                (void)trainz::content::parseAnimation(a.readLimited(e, std::size_t{256} << 20));
                ++parsed;
            } catch (const ContentError& err) {
                ++failures;
                std::cout << "  animation problem: " << e.name << ": " << err.what() << "\n";
            }
        }
    }
    std::cout << "  animations parsed: " << parsed << "\n";
    EXPECT_EQ(failures, 0u);
}

TEST_F(RealInstall, RouteGroundFilesParse) {
    std::size_t parsed = 0;
    std::size_t failures = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        for (const auto& e : a.entries()) {
            const std::string want = "/mapfile.gnd";
            if (e.name.size() <= want.size() || e.name.compare(e.name.size() - want.size(), want.size(), want) != 0 ||
                !a.isLookupWinner(e)) {
                continue;
            }
            try {
                const auto g = trainz::content::parseGround(a.readLimited(e, std::size_t{1} << 30));
                ++parsed;
                std::cout << "  " << e.name << ": " << g.sections.size() << " sections\n";
            } catch (const ContentError& err) {
                ++failures;
                std::cout << "  ground problem: " << e.name << ": " << err.what() << "\n";
            }
        }
    }
    std::cout << "  mapfile.gnd parsed: " << parsed << "\n";
    EXPECT_EQ(failures, 0u);
}

TEST_F(RealInstall, SoundsDecode) {
    // Every .wav must decode, except the formats known not to be handled yet
    // (MP3 inside WAV) and the one MPEG stream named .wav on the disc.
    std::size_t decoded = 0;
    std::size_t knownUnsupported = 0;
    std::size_t failures = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        for (const auto& e : a.entries()) {
            if (e.name.size() <= 4 || e.name.compare(e.name.size() - 4, 4, ".wav") != 0 || !a.isLookupWinner(e)) {
                continue;
            }
            try {
                (void)trainz::content::decodeWav(a.readLimited(e, std::size_t{256} << 20));
                ++decoded;
            } catch (const ContentError& err) {
                const std::string why = err.what();
                if (why.find("MP3 inside") != std::string::npos || why.find("MPEG stream") != std::string::npos) {
                    ++knownUnsupported;
                } else {
                    ++failures;
                    std::cout << "  sound problem: " << e.name << ": " << why << "\n";
                }
            }
        }
    }
    std::cout << "  sounds decoded: " << decoded << ", known unsupported: " << knownUnsupported << "\n";
    EXPECT_EQ(failures, 0u);
}
