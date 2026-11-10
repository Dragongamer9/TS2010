#include "JaTestBuilder.h"

#include "content/AssetIndex.h"
#include "content/JaArchive.h"
#include "content/Kuid.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace trainz::content;
using namespace trainz::test;
namespace fs = std::filesystem;

namespace {

std::string cfgText(const std::string& kuid, const std::string& kind, const std::string& name,
                    const std::string& extra = "") {
    return "\xEF\xBB\xBF\r\nkuid  " + kuid + "\r\nkind  \"" + kind + "\"\r\nusername  \"" + name + "\"\r\n" + extra;
}

std::string folderFor(const Kuid& k) { return kuidFolderKey(k); }

bool hasWarningContaining(const AssetIndex& idx, const std::string& text) {
    return std::any_of(idx.warnings().begin(), idx.warnings().end(),
                       [&](const std::string& w) { return w.find(text) != std::string::npos; });
}

}  // namespace

TEST(SelectAssetConfigs, SkipsSubPartsAndPrefersChump) {
    std::vector<std::string> notes;
    const auto chosen = selectAssetConfigs(
        {
            "hash-c0/kuid 1 2/config.txt",
            "hash-c0/kuid 1 2/sub/config.txt",
            "hash-c0/kuid 1 2/sub/deeper/config.chump",
            "a/config.txt",
            "a/config.chump",
            "b/readme.txt",
            "c/CONFIG.TXT",
        },
        &notes);
    ASSERT_EQ(chosen.size(), 3u);
    EXPECT_EQ(chosen[0], (std::pair<std::string, std::string>{"a", "config.chump"}));
    EXPECT_EQ(chosen[1], (std::pair<std::string, std::string>{"c", "CONFIG.TXT"}));
    EXPECT_EQ(chosen[2], (std::pair<std::string, std::string>{"hash-c0/kuid 1 2", "config.txt"}));
    EXPECT_EQ(notes.size(), 1u);
}

TEST(SelectAssetConfigs, ConfigAtTheTopMakesEverythingElseASubPart) {
    const auto chosen = selectAssetConfigs({"config.txt", "x/config.txt"});
    ASSERT_EQ(chosen.size(), 1u);
    EXPECT_EQ(chosen[0].first, "");
}

class AssetIndexTest : public ::testing::Test {
protected:
    TempDir dir;

    fs::path makeBuiltinArchive(const fs::path& where) {
        const Kuid shed{447264, 1073, std::nullopt};
        const Kuid carV2{5, 6, 2};
        const Kuid carV3{5, 6, 3};
        const Kuid carPlain{5, 6, std::nullopt};
        JaTestBuilder b;
        b.addCfilBpe(folderFor(shed) + "/config.txt",
                     bytesOf(cfgText("<kuid:447264:1073>", "scenery", "Shed",
                                     "obsolete-table\r\n{\r\n  0  <kuid:447264:1000>\r\n}\r\n")));
        b.addPlain(folderFor(shed) + "/lever/config.txt", bytesOf(cfgText("<kuid:447264:1074>", "x", "sub part")));
        b.addPlain(folderFor(carPlain) + "/config.txt", bytesOf(cfgText("<kuid:5:6>", "traincar", "Car v1")));
        b.addPlain(folderFor(carV3) + "/config.txt", bytesOf(cfgText("<kuid2:5:6:3>", "traincar", "Car v3")));
        b.addPlain(folderFor(carV2) + "/config.txt", bytesOf(cfgText("<kuid2:5:6:2>", "traincar", "Car v2")));
        b.addPlain("hash-00/kuid 9 9/config.txt", bytesOf(cfgText("<kuid:9:10>", "scenery", "Mislabelled")));
        b.addPlain("broken/config.txt", bytesOf("kuid <kuid:1:1>\r\nkind \"never closed\r\n"));
        b.addPlain("nokuid/config.txt", bytesOf("kind \"scenery\"\r\n"));
        return dir.write(where.string(), b.build());
    }
};

TEST_F(AssetIndexTest, IndexesAnArchive) {
    const auto a = JaArchive::open(makeBuiltinArchive("8.ja"));
    AssetIndex idx;
    idx.addArchive(a);
    // shed, car plain/v2/v3, mislabelled = 5; sub part, broken, nokuid are not assets
    ASSERT_EQ(idx.records().size(), 5u);

    const auto shed = idx.find(*parseKuid("<kuid:447264:1073>"));
    ASSERT_EQ(shed.size(), 1u);
    EXPECT_EQ(shed[0]->kind, "scenery");
    EXPECT_EQ(shed[0]->username, "Shed");
    EXPECT_EQ(shed[0]->where.container, AssetLocation::Container::Archive);
    EXPECT_EQ(shed[0]->where.configName, "config.txt");
    EXPECT_EQ(shed[0]->where.folder, folderFor({447264, 1073, std::nullopt}));
    ASSERT_EQ(shed[0]->obsoletes.size(), 1u);
    EXPECT_EQ(idx.obsoletedBy(*parseKuid("<kuid:447264:1000>")).size(), 1u);

    EXPECT_TRUE(idx.find(*parseKuid("<kuid:447264:1074>")).empty());  // sub part

    const auto versions = idx.allVersions(5, 6);
    ASSERT_EQ(versions.size(), 3u);
    EXPECT_FALSE(versions[0]->kuid.version.has_value());
    EXPECT_EQ(versions[1]->kuid.version, 2);
    EXPECT_EQ(versions[2]->kuid.version, 3);
    EXPECT_EQ(idx.find(*parseKuid("<kuid2:5:6:3>")).size(), 1u);
    EXPECT_EQ(idx.find(*parseKuid("<kuid2:5:6:0>")).size(), 1u);  // version 0 = plain

    EXPECT_TRUE(hasWarningContaining(idx, "folder says <kuid:9:9>"));
    EXPECT_EQ(idx.find(*parseKuid("<kuid:9:10>")).size(), 1u);  // the config's KUID wins
    EXPECT_TRUE(hasWarningContaining(idx, "broken/config.txt"));
    EXPECT_TRUE(hasWarningContaining(idx, "no readable top-level kuid"));
}

TEST_F(AssetIndexTest, BrokenParentDoesNotHideRealAssets) {
    // A broken config (or one without a KUID) high up must not swallow the
    // valid assets below it; only a valid asset's folder makes sub-parts.
    JaTestBuilder b;
    b.addPlain("pack/config.txt", bytesOf("kind \"never closed\r\n"));
    b.addPlain("pack/a/config.txt", bytesOf(cfgText("<kuid:7:1>", "scenery", "A")));
    b.addPlain("pack2/config.txt", bytesOf("kind \"scenery\"\r\n"));  // no kuid
    b.addPlain("pack2/b/config.txt", bytesOf(cfgText("<kuid:7:2>", "scenery", "B")));
    b.addPlain("good/config.txt", bytesOf(cfgText("<kuid:7:3>", "scenery", "C")));
    b.addPlain("good/part/config.txt", bytesOf(cfgText("<kuid:7:4>", "x", "sub")));
    const auto a = JaArchive::open(dir.write("p.ja", b.build()));
    AssetIndex idx;
    idx.addArchive(a);
    EXPECT_EQ(idx.find(*parseKuid("<kuid:7:1>")).size(), 1u);
    EXPECT_EQ(idx.find(*parseKuid("<kuid:7:2>")).size(), 1u);
    EXPECT_EQ(idx.find(*parseKuid("<kuid:7:3>")).size(), 1u);
    EXPECT_TRUE(idx.find(*parseKuid("<kuid:7:4>")).empty());
    EXPECT_TRUE(hasWarningContaining(idx, "pack/config.txt"));
    EXPECT_TRUE(hasWarningContaining(idx, "pack2/config.txt"));
}

TEST_F(AssetIndexTest, OversizedConfigIsReportedNotLoaded) {
    JaTestBuilder b;
    Bytes stored = cfilRaw(bytesOf("kuid <kuid:1:1>\n"));
    setU32(stored, 8, 0x7FFFFFFFu);  // CFIL claims 2 GiB
    b.addEntry({"big/config.txt", stored, 16, 0x80, 0});
    const auto a = JaArchive::open(dir.write("big.ja", b.build()));
    AssetIndex idx;
    idx.addArchive(a);
    EXPECT_TRUE(idx.records().empty());
    EXPECT_TRUE(hasWarningContaining(idx, "allowed here"));
}

TEST_F(AssetIndexTest, IndexesALooseFolder) {
    const fs::path local = dir.path() / "local";
    dir.write("local/" + folderFor({10, 20, std::nullopt}) + "/config.txt",
              bytesOf(cfgText("<kuid:10:20>", "scenery", "Loose")));
    dir.write("local/My Stuff/Thing/config.txt", bytesOf(cfgText("<kuid2:10:21:4>", "traincar", "Thing")));
    dir.write("local/My Stuff/Thing/bogey/config.txt", bytesOf(cfgText("<kuid:10:22>", "bogey", "sub")));
    AssetIndex idx;
    idx.addFolder(local);
    ASSERT_EQ(idx.records().size(), 2u);
    const auto thing = idx.find(*parseKuid("<kuid2:10:21:4>"));
    ASSERT_EQ(thing.size(), 1u);
    EXPECT_EQ(thing[0]->where.container, AssetLocation::Container::Folder);
    EXPECT_EQ(thing[0]->where.folder, "My Stuff/Thing");
    EXPECT_TRUE(idx.find(*parseKuid("<kuid:10:22>")).empty());
}

TEST_F(AssetIndexTest, ScansAnInstallCaseInsensitively) {
    const fs::path root = dir.path() / "TS2010";
    JaTestBuilder base;
    base.addPlain("Achievements/Rule/config.txt", bytesOf(cfgText("<kuid:447264:1011>", "behavior", "Rule")));
    dir.write("TS2010/Base.ja", base.build());
    makeBuiltinArchive("TS2010/builtin/8.ja");
    dir.write("TS2010/UserData/local/" + folderFor({10, 20, std::nullopt}) + "/config.txt",
              bytesOf(cfgText("<kuid:10:20>", "scenery", "Loose")));

    const AssetIndex idx = AssetIndex::scanInstall(root);
    EXPECT_EQ(idx.records().size(), 1u + 5u + 1u);
    EXPECT_EQ(idx.find(*parseKuid("<kuid:447264:1011>")).size(), 1u);
    EXPECT_EQ(idx.find(*parseKuid("<kuid:10:20>")).size(), 1u);
    EXPECT_TRUE(hasWarningContaining(idx, "Data.ja not found"));
}

TEST_F(AssetIndexTest, SameKuidInTwoPlacesIsReportedTwice) {
    JaTestBuilder b;
    b.addPlain(folderFor({10, 20, std::nullopt}) + "/config.txt", bytesOf(cfgText("<kuid:10:20>", "scenery", "Built-in")));
    const auto a = JaArchive::open(dir.write("x.ja", b.build()));
    dir.write("local/" + folderFor({10, 20, std::nullopt}) + "/config.txt",
              bytesOf(cfgText("<kuid:10:20>", "scenery", "Local copy")));
    AssetIndex idx;
    idx.addArchive(a);
    idx.addFolder(dir.path() / "local");
    const auto both = idx.find(*parseKuid("<kuid:10:20>"));
    ASSERT_EQ(both.size(), 2u);  // which one the game uses is not decided here
    EXPECT_EQ(both[0]->username, "Built-in");
    EXPECT_EQ(both[1]->username, "Local copy");
}
