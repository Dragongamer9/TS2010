#include "JaTestBuilder.h"

#include "content/ContentError.h"
#include "content/JaArchive.h"
#include "content/Kuid.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

#include <sys/stat.h>

using namespace trainz::content;
using namespace trainz::test;

namespace {

Bytes readStream(const JaArchive& a, const JaEntry& e, std::size_t chunk) {
    auto s = a.openStream(e);
    Bytes out;
    Bytes buf(chunk);
    for (std::size_t n; (n = s.read(buf)) != 0;) {
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
    }
    return out;
}

Bytes fileBytes(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

class ArchiveTest : public ::testing::Test {
protected:
    TempDir dir;
    Bytes plain = bytesOf("\xEF\xBB\xBF\r\nkuid  <kuid:447264:1011>\r\n");
    Bytes big = patternBytes(70000, 7);
    Bytes medium = patternBytes(20000, 8);

    std::filesystem::path makeStandard() {
        JaTestBuilder b;
        b.addPlain("Achievements/Rule/config.txt", plain)
            .addCfilBpe("Scenery/Shed/Shed.im", big)
            .addCfilRaw("Scenery/Shed/shed.texture", medium)
            .addPlain("hash-C0/kuid 447264 1073/profile.obs", bytesOf("obs"))
            .addPlain("hash-C0/kuid2 90361 120204 2/doors.kin", bytesOf("kin2"))
            .addPlain("hash-C0/kuid2 447264 1073 3/config.chump", bytesOf("v3"));
        return dir.write("Base.ja", b.build());
    }
};

TEST_F(ArchiveTest, OpensAndIndexes) {
    const auto a = JaArchive::open(makeStandard());
    EXPECT_EQ(a.info().version, 0x102u);
    EXPECT_EQ(a.info().fileCount, 6u);
    EXPECT_EQ(a.info().infoField5, 42203u);
    ASSERT_EQ(a.entries().size(), 6u);
    EXPECT_TRUE(a.warnings().empty());
    // Stored names are lowercased, like the game's _strlwr.
    EXPECT_EQ(a.entries()[0].name, "achievements/rule/config.txt");
    EXPECT_EQ(a.entries()[1].originalSize, big.size());
    EXPECT_TRUE(a.entries()[1].flaggedCompressed());
}

TEST_F(ArchiveTest, FindsLikeTheGame) {
    const auto a = JaArchive::open(makeStandard());
    ASSERT_NE(a.find("achievements/rule/config.txt"), nullptr);
    EXPECT_NE(a.find("ACHIEVEMENTS/Rule/Config.TXT"), nullptr);
    EXPECT_NE(a.find("Achievements\\Rule\\config.txt"), nullptr);
    EXPECT_NE(a.find("Achievements//Rule/./x/../config.txt"), nullptr);
    // A leading separator stays a leading '/', which no stored name has.
    EXPECT_EQ(a.find("/Achievements/Rule/config.txt"), nullptr);
    EXPECT_EQ(a.find("Achievements/Rule"), nullptr);
    EXPECT_EQ(a.find(std::string(300, 'a')), nullptr);
}

TEST_F(ArchiveTest, ReadsPlainAndCompressed) {
    const auto a = JaArchive::open(makeStandard());
    EXPECT_EQ(a.read(*a.find("Achievements/Rule/config.txt")), plain);
    EXPECT_EQ(a.read(*a.find("Scenery/Shed/Shed.im")), big);
    EXPECT_EQ(a.read(*a.find("Scenery/Shed/shed.texture")), medium);
}

TEST_F(ArchiveTest, StreamMatchesWholeRead) {
    const auto a = JaArchive::open(makeStandard());
    for (const auto& e : a.entries()) {
        const Bytes whole = a.read(e);
        for (std::size_t chunk : {1u, 7u, 4096u, 1u << 20}) {
            EXPECT_EQ(readStream(a, e, chunk), whole) << e.name << " chunk " << chunk;
        }
    }
}

TEST_F(ArchiveTest, ListsFoldersAndKuids) {
    const auto a = JaArchive::open(makeStandard());
    EXPECT_EQ(a.listFolder("Scenery/Shed").size(), 2u);
    EXPECT_EQ(a.listFolder("scenery").size(), 2u);
    EXPECT_EQ(a.listFolder("").size(), 6u);
    EXPECT_TRUE(a.listFolder("Scenery/Sh").empty());

    const auto plainKuid = a.findKuid(*parseKuid("<kuid:447264:1073>"));
    ASSERT_EQ(plainKuid.size(), 1u);
    EXPECT_EQ(plainKuid[0]->name, "hash-c0/kuid 447264 1073/profile.obs");

    EXPECT_EQ(a.findKuid(*parseKuid("<kuid:447264:1073>"), true).size(), 2u);
    EXPECT_EQ(a.findKuid(*parseKuid("<kuid2:90361:120204:2>")).size(), 1u);
    EXPECT_TRUE(a.findKuid(*parseKuid("<kuid2:90361:120204:1>")).empty());
}

TEST_F(ArchiveTest, FirstDuplicateWins) {
    JaTestBuilder b;
    b.addPlain("dup.txt", bytesOf("first")).addPlain("DUP.txt", bytesOf("second"));
    const auto a = JaArchive::open(dir.write("dup.ja", b.build()));
    ASSERT_EQ(a.entries().size(), 2u);
    EXPECT_EQ(a.read(*a.find("dup.txt")), bytesOf("first"));
    EXPECT_FALSE(a.warnings().empty());
}

TEST_F(ArchiveTest, SkipsRowsTheGameSkips) {
    JaTestBuilder b;
    b.addPlain("", bytesOf("empty name")).addPlain(std::string(259, 'n'), bytesOf("too long")).addPlain("ok.txt",
                                                                                                         bytesOf("ok"));
    const auto a = JaArchive::open(dir.write("skip.ja", b.build()));
    ASSERT_EQ(a.entries().size(), 1u);
    EXPECT_EQ(a.entries()[0].name, "ok.txt");
    EXPECT_EQ(a.warnings().size(), 2u);
}

TEST_F(ArchiveTest, Version101Archive) {
    JaTestBuilder b(0x101);
    const Bytes data = patternBytes(30000, 9);
    b.addCfilRaw("old/file.bin", data).addCfilBpe("old/packed.bin", data);
    const auto a = JaArchive::open(dir.write("old.ja", b.build()));
    EXPECT_EQ(a.info().version, 0x101u);
    EXPECT_EQ(a.read(*a.find("old/file.bin")), data);
    EXPECT_EQ(readStream(a, *a.find("old/packed.bin"), 333), data);
}

TEST_F(ArchiveTest, NeverChangesTheArchive) {
    const auto path = makeStandard();
    const Bytes before = fileBytes(path);
    struct stat st1 {};
    ASSERT_EQ(::stat(path.c_str(), &st1), 0);
    {
        const auto a = JaArchive::open(path);
        for (const auto& e : a.entries()) {
            (void)a.read(e);
            (void)readStream(a, e, 4096);
        }
    }
    struct stat st2 {};
    ASSERT_EQ(::stat(path.c_str(), &st2), 0);
    EXPECT_EQ(fileBytes(path), before);
    EXPECT_EQ(st1.st_mtim.tv_sec, st2.st_mtim.tv_sec);
    EXPECT_EQ(st1.st_mtim.tv_nsec, st2.st_mtim.tv_nsec);
}

TEST_F(ArchiveTest, RejectsBadHeaders) {
    const Bytes good = JaTestBuilder().addPlain("a.txt", bytesOf("a")).build();
    auto expectBad = [&](Bytes b, const char* what) {
        EXPECT_THROW(JaArchive::open(dir.write("bad.ja", b)), ContentError) << what;
    };
    expectBad(Bytes{}, "empty file");
    expectBad(Bytes(good.begin(), good.begin() + 40), "short header");
    {
        Bytes b = good;
        b[0] = 'X';
        expectBad(b, "no ARCH");
    }
    {
        Bytes b = good;
        b[4] = 'X';
        expectBad(b, "no INFO");
    }
    {
        Bytes b = good;
        setU32(b, 8, 31);
        expectBad(b, "INFO size");
    }
    {
        Bytes b = good;
        setU32(b, 12, 0x41524331);
        expectBad(b, "ARC0 marker");
    }
    {
        Bytes b = good;
        setU32(b, 16, 0x103);
        expectBad(b, "version");
    }
    {
        Bytes b = good;
        setU32(b, 20, 1000000);
        expectBad(b, "file count too big");
    }
    {
        Bytes b = good;
        setU32(b, 24, static_cast<std::uint32_t>(b.size() + 10));
        expectBad(b, "index offset");
    }
    {
        Bytes b = good;
        setU32(b, 24, 44);
        expectBad(b, "index tag");
    }
    {
        Bytes b = good;
        const std::size_t nameAt = b.size() - 296 + 8;
        for (std::size_t i = 0; i < 260; ++i) {
            b[nameAt + i] = 'x';
        }
        expectBad(b, "unterminated name");
    }
    EXPECT_THROW(JaArchive::open(dir.path() / "missing.ja"), ContentError);
    EXPECT_THROW(JaArchive::open(dir.path()), ContentError);
}

TEST_F(ArchiveTest, DataPastTheEndIsReportedNotRead) {
    Bytes b = JaTestBuilder().addPlain("a.txt", bytesOf("abc")).build();
    const std::size_t rowAt = b.size() - 296;
    setU32(b, rowAt + 4, 1000000);  // stored size far past the end
    const auto a = JaArchive::open(dir.write("past.ja", b));
    ASSERT_EQ(a.entries().size(), 1u);
    EXPECT_FALSE(a.warnings().empty());
    EXPECT_THROW(a.read(a.entries()[0]), ContentError);
    EXPECT_THROW(a.openStream(a.entries()[0]), ContentError);
}
