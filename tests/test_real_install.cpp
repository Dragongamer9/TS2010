// Checks against a real Trainz Simulator 2010 install. Runs only when
// TRAINZ_DATA_DIR points at the install folder (the one holding Base.ja and
// Builtin/); otherwise every test here is skipped. Everything is read-only.
//
//   TRAINZ_DATA_DIR=/path/to/TS2010 ctest --test-dir build -R RealInstall --output-on-failure
//   TRAINZ_FULL_VERIFY=1 ...   decode every entry (slow on a hard drive: about 7 GB)
#include "content/ContentError.h"
#include "content/GroundFile.h"
#include "content/ImageFile.h"
#include "content/JaArchive.h"
#include "content/AnimationFile.h"
#include "content/ArchivePath.h"
#include "content/AssetConfig.h"
#include "content/AssetIndex.h"
#include "content/Kuid.h"
#include "content/MeshFile.h"
#include "content/ObjectContainer.h"
#include "content/PlacedObject.h"
#include "content/ReadOnlyFile.h"
#include "content/SoundFile.h"
#include "content/TextureDescriptor.h"
#include "content/TrackLayout.h"
#include "content/TextureFile.h"
#include "input/KeyBindings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
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
                // Every section's data must decode to exactly its end offset.
                for (const auto& s : g.sections) {
                    (void)trainz::content::parseGroundSection(s, g.version);
                }
                ++parsed;
                std::cout << "  " << e.name << ": " << g.sections.size() << " sections decoded\n";
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
    // Every .wav and .mp3 must decode. Known exceptions:
    //   - the one MPEG program stream named .wav on the disc (DVD-style video
    //     with AC-3 audio, Builtin\1.ja), always refused;
    //   - MP3 (3 .mp3 files, 3 MP3-inside-WAV files) in builds without libmpg123.
    const bool haveMp3 = mp3DecodingAvailable();
    std::size_t decoded = 0;
    std::size_t decodedMp3 = 0;
    std::size_t knownUnsupported = 0;
    std::size_t failures = 0;
    auto endsWith = [](const std::string& n, const char* ext) {
        return n.size() > 4 && n.compare(n.size() - 4, 4, ext) == 0;
    };
    auto mp3InWav = [](const std::vector<std::uint8_t>& b) {
        try {
            return parseWav(b).formatTag == 85;
        } catch (const ContentError&) {
            return true;  // decoded but not RIFF: a bare MP3 stream named .wav
        }
    };
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        for (const auto& e : a.entries()) {
            const bool isMp3 = endsWith(e.name, ".mp3");
            if ((!isMp3 && !endsWith(e.name, ".wav")) || !a.isLookupWinner(e)) {
                continue;
            }
            const auto bytes = a.readLimited(e, std::size_t{256} << 20);
            try {
                const auto s = decodeSoundFile(e.name, bytes);
                ++decoded;
                if (isMp3 || mp3InWav(bytes)) {
                    ++decodedMp3;
                    std::cout << "  mp3: " << e.name << ": " << s.channels << " ch, " << s.sampleRate << " Hz, "
                              << s.samples.size() / s.channels << " frames\n";
                }
            } catch (const ContentError& err) {
                const std::string why = err.what();
                const bool mp3Missing = !haveMp3 && (why.find("MP3 inside") != std::string::npos ||
                                                     why.find("not built in") != std::string::npos);
                if (mp3Missing || why.find("MPEG program stream") != std::string::npos) {
                    ++knownUnsupported;
                } else {
                    ++failures;
                    std::cout << "  sound problem: " << e.name << ": " << why << "\n";
                }
            }
        }
    }
    std::cout << "  sounds decoded: " << decoded << " (" << decodedMp3 << " MP3), known unsupported: "
              << knownUnsupported << (haveMp3 ? "" : " (built without libmpg123)") << "\n";
    EXPECT_EQ(failures, 0u);
}

TEST_F(RealInstall, KeyboardBindingsRead) {
    const fs::path file = root / "UserData" / "settings" / "keyboard.txt";
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        GTEST_SKIP() << "no " << file;
    }
    const auto f = ReadOnlyFile::open(file);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(std::min<std::uint64_t>(f.size(), 1u << 20)));
    f.readExact(0, data);
    const auto kb = trainz::input::parseKeyBindings(data);
    std::size_t actions = 0;
    for (const auto& [group, list] : kb.groups) {
        actions += list.size();
    }
    std::cout << "  keyboard.txt: " << kb.groups.size() << " groups, " << actions << " actions, "
              << kb.warnings.size() << " warnings\n";
    // Every key name must be known; only the camera_car line's replaced keys are expected.
    for (const auto& w : kb.warnings) {
        std::cout << "  " << w << "\n";
        EXPECT_NE(w.find("replaced by"), std::string::npos) << w;
    }
    const auto* car = kb.find("driver", "camera_car");
    ASSERT_NE(car, nullptr);
    ASSERT_EQ(car->combos.size(), 1u);
    EXPECT_EQ(trainz::input::describe(car->combos[0]), "ctrl+alt+shift+Z");
    EXPECT_GT(actions, 0u);
}

TEST_F(RealInstall, LooseImagesDecode) {
    // Every .tga / .bmp / .jpg / .jpeg decodes (all of them: the 1,276 in testdata take
    // well under a second). Four disc files are damaged and known to be refused (see
    // docs/formats/image.md); they are reported but not counted as failures.
    static const char* const kKnownDamaged[] = {
        "hash-da/kuid 2000 28000/nightwindows.tga",            // Builtin 4.ja: 32 bpp header, 24 bpp of data
        "hash-92/kuid2 86661 9210691 1/log_equip_tn.jpg",      // Builtin 3.ja: cut off inside the scan
        "hash-c8/kuid2 243555 10040 2/screenshot.jpg",         // Builtin 4.ja: as above
        "hash-ea/kuid2 243555 10010 2/screenshot.jpg",         // Builtin 4.ja: as above
    };
    std::map<std::string, std::size_t> kinds;
    std::map<std::string, std::size_t> descriptions;
    std::size_t decoded = 0;
    std::size_t failures = 0;
    std::size_t knownDamaged = 0;
    std::size_t jpegSkipped = 0;
    std::size_t withWarnings = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        for (const auto& e : a.entries()) {
            const ImageKind kind = imageKindFromName(e.name);
            if (kind == ImageKind::Unknown || !a.isLookupWinner(e)) {
                continue;
            }
            ++kinds[imageKindName(kind)];
            if (kind == ImageKind::Jpeg && !jpegSupported()) {
                ++jpegSkipped;
                continue;
            }
            try {
                const auto img = decodeImage(kind, a.readLimited(e, std::size_t{128} << 20));
                EXPECT_EQ(img.rgba.size(), std::size_t{img.width} * img.height * 4) << e.name;
                ++decoded;
                ++descriptions[img.description];
                if (!img.warnings.empty()) {
                    ++withWarnings;
                    std::cout << "  image warning: " << e.name << ": " << img.warnings.front() << "\n";
                }
            } catch (const ContentError& err) {
                const bool known = std::find_if(std::begin(kKnownDamaged), std::end(kKnownDamaged), [&](const char* n) {
                                       return e.name == n;
                                   }) != std::end(kKnownDamaged);
                (known ? knownDamaged : failures) += 1;
                std::cout << "  image " << (known ? "refused (known damaged)" : "problem") << ": " << e.name << ": "
                          << err.what() << "\n";
            }
        }
    }
    std::cout << "  images found:";
    for (const auto& [k, v] : kinds) {
        std::cout << " " << k << " " << v;
    }
    std::cout << "\n  images decoded: " << decoded << ", with warnings: " << withWarnings
              << ", known damaged refused: " << knownDamaged << ", JPEG skipped (no libjpeg): " << jpegSkipped << "\n";
    for (const auto& [d, n] : descriptions) {
        std::cout << "    " << n << "  " << d << "\n";
    }
    EXPECT_EQ(failures, 0u);
}

TEST_F(RealInstall, TextureDescriptorsResolve) {
    // Every .texture.txt parses without warnings, and its Primary/Alpha images exist
    // next to it in the same archive, decode, and combine.
    std::size_t descriptors = 0;
    std::size_t combined = 0;
    std::size_t skippedJpeg = 0;
    std::size_t problems = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        for (const auto& e : a.entries()) {
            if (e.name.size() < 12 || e.name.compare(e.name.size() - 12, 12, ".texture.txt") != 0 ||
                !a.isLookupWinner(e)) {
                continue;
            }
            ++descriptors;
            try {
                const auto d = parseTextureDescriptor(a.readLimited(e, 64 * 1024));
                for (const auto& w : d.warnings) {
                    ++problems;
                    std::cout << "  " << e.name << ": " << w << "\n";
                }
                if (d.primary.empty()) {
                    continue;
                }
                auto load = [&](const std::string& n) -> std::optional<RgbaImage> {
                    const std::string full = resolveDescriptorPath(e.name, n);
                    const JaEntry* img = a.find(full);
                    if (img == nullptr) {
                        throw ContentError("missing image " + full);
                    }
                    const ImageKind kind = imageKindFromName(full);
                    if (kind == ImageKind::Jpeg && !jpegSupported()) {
                        return std::nullopt;
                    }
                    return decodeImage(kind, a.readLimited(*img, std::size_t{256} << 20));
                };
                const auto primary = load(d.primary[0]);
                std::optional<RgbaImage> alpha;
                const bool same = !d.alpha.empty() && asciiLower(d.alpha[0]) == asciiLower(d.primary[0]);
                if (!d.alpha.empty() && !same) {
                    alpha = load(d.alpha[0]);
                }
                if (!primary || (!d.alpha.empty() && !same && !alpha)) {
                    ++skippedJpeg;
                    continue;
                }
                (void)composeDescriptorImage(*primary, d.alpha.empty() ? nullptr : (same ? &*primary : &*alpha), same);
                ++combined;
            } catch (const ContentError& err) {
                ++problems;
                std::cout << "  " << e.name << ": " << err.what() << "\n";
            }
        }
    }
    std::cout << "  texture descriptors: " << descriptors << ", combined: " << combined
              << ", skipped (no libjpeg): " << skippedJpeg << ", problems: " << problems << "\n";
    EXPECT_EQ(problems, 0u);
}

TEST_F(RealInstall, RouteObjectFilesRead) {
    // mapfile.obs / mapfile.trk (and profile.* copies): every one reads to its exact end.
    std::size_t files = 0;
    std::size_t objects = 0;
    std::size_t refusedOld = 0;
    std::size_t failures = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        for (const auto& e : a.entries()) {
            const bool obs = e.name.size() > 4 && e.name.compare(e.name.size() - 4, 4, ".obs") == 0;
            const bool trk = e.name.size() > 4 && e.name.compare(e.name.size() - 4, 4, ".trk") == 0;
            if (!(obs || trk) || !a.isLookupWinner(e)) {
                continue;
            }
            try {
                const auto f = parseRouteObjectFile(a.readLimited(e, std::size_t{256} << 20));
                ++files;
                for (const auto& c : f.containers) {
                    objects += c.items.size();
                }
            } catch (const ContentError& err) {
                const std::string why = err.what();
                if (why.find("version 2") != std::string::npos) {
                    ++refusedOld;  // the old robescenario route; not handled yet
                } else {
                    ++failures;
                    std::cout << "  " << e.name << ": " << why << "\n";
                }
            }
        }
    }
    std::cout << "  route object files: " << files << ", objects: " << objects
              << ", old container v2 refused: " << refusedOld << "\n";
    EXPECT_EQ(failures, 0u);
}

TEST_F(RealInstall, TrackSitsOnTheTerrain) {
    // Every stretch joins existing vertices, and track vertices lie on the ground:
    // checks the observed vertex layout and the terrain grid layout together.
    std::size_t routes = 0, vertices = 0, onSection = 0, within1m = 0, badStretches = 0, pieces = 0, links = 0,
                badLinks = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        for (const auto& e : a.entries()) {
            if (e.name.size() < 12 || e.name.compare(e.name.size() - 12, 12, "/mapfile.gnd") != 0 || !a.isLookupWinner(e)) {
                continue;
            }
            const std::string folder = e.name.substr(0, e.name.size() - 12);
            const JaEntry* te = a.find(folder + "/mapfile.trk");
            if (te == nullptr) {
                continue;
            }
            GroundFile g;
            TrackLayout t;
            try {
                g = parseGround(a.read(e));
                t = readTrackLayout(parseRouteObjectFile(a.read(*te)));
            } catch (const ContentError&) {
                continue;  // the old robescenario route
            }
            ++routes;
            pieces += t.stretches.size();
            for (const auto& [i, v] : t.vertices) {
                links += v.links.size();
            }
            for (const auto& w : t.warnings) {
                if (w.find("missing") != std::string::npos) {
                    ++badStretches;
                } else if (w.find("links to track piece") != std::string::npos) {
                    ++badLinks;
                }
            }
            std::map<std::pair<long, long>, const GroundSection*> byPos;
            for (const auto& s : g.sections) {
                byPos[{s.x, s.y}] = &s;
            }
            std::map<const GroundSection*, GroundSectionData> decoded;
            for (const auto& [index, v] : t.vertices) {
                ++vertices;
                const auto it = byPos.find({v.boardX, v.boardY});
                if (it == byPos.end() || !(v.x >= 0 && v.x < kBaseboardSize && v.y >= 0 && v.y < kBaseboardSize)) {
                    continue;
                }
                auto d = decoded.find(it->second);
                if (d == decoded.end()) {
                    d = decoded.emplace(it->second, parseGroundSection(*it->second, g.version)).first;
                }
                const std::size_t side = groundVertexSide(it->second->tag);
                const std::size_t cells = side - 4;
                const double spacing = kBaseboardSize / static_cast<double>(cells);
                const auto col = static_cast<std::size_t>(std::lround(v.x / spacing));
                const auto row = static_cast<std::size_t>(std::lround(v.y / spacing));
                ++onSection;
                if (std::fabs(d->second.vertices[row * side + col].height - v.z) < 1.0F) {
                    ++within1m;
                }
            }
        }
    }
    if (routes == 0) {
        GTEST_SKIP() << "no route with mapfile.gnd and mapfile.trk";
    }
    std::cout << "  routes " << routes << ", track vertices " << vertices << ", on a section " << onSection
              << ", within 1 m of the ground " << within1m << "; track pieces " << pieces << " (ends missing "
              << badStretches << "), vertex links " << links << " (bad " << badLinks << ")\n";
    // Allow a little damage: a few links on the disc point at pieces that aren't in the file.
    EXPECT_LE(badStretches * 1000, pieces);
    EXPECT_LE(badLinks * 1000, links);
    EXPECT_GT(onSection * 100, vertices * 95);  // 99.5% on the disc
    EXPECT_GT(within1m * 100, onSection * 50);  // 66% on the disc (nearest vertex here); rows flipped: 39%
}

TEST_F(RealInstall, PlacedObjectsHavePositions) {
    // The shared base block (board, metres, height) decodes for nearly every placed object.
    std::size_t objects = 0, placed = 0;
    for (const auto& path : archives) {
        const auto a = JaArchive::open(path);
        for (const auto& e : a.entries()) {
            if (e.name.size() < 12 || e.name.compare(e.name.size() - 12, 12, "/mapfile.obs") != 0 || !a.isLookupWinner(e)) {
                continue;
            }
            RouteObjectFile f;
            try {
                f = parseRouteObjectFile(a.read(e));
            } catch (const ContentError&) {
                continue;  // the old robescenario route
            }
            for (const auto& c : f.containers) {
                for (const auto& item : c.items) {
                    if (!placedObjectHeaderSize(item.tag)) {
                        continue;
                    }
                    ++objects;
                    placed += decodePlacedObject(item).has_value();
                }
            }
        }
    }
    if (objects == 0) {
        GTEST_SKIP() << "no mapfile.obs";
    }
    std::cout << "  placed objects " << objects << ", with a position " << placed << "\n";
    EXPECT_GE(placed * 1000, objects * 999);  // 99.99% on the disc
}
