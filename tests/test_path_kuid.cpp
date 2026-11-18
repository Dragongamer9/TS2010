#include "common/DataDirs.h"
#include "content/ArchivePath.h"
#include "content/Kuid.h"

#include "JaTestBuilder.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

using namespace trainz::content;

TEST(ArchivePath, LowercaseIsAsciiOnly) {
    EXPECT_EQ(asciiLower("Scenery/SHED.IM"), "scenery/shed.im");
    EXPECT_EQ(asciiLower("\xC4X"), "\xC4x");  // non-ASCII bytes are left alone, like _strlwr in the C locale
}

TEST(ArchivePath, Canonicalize) {
    EXPECT_EQ(canonicalizePath("a\\b/c"), "a/b/c");
    EXPECT_EQ(canonicalizePath("a//b///c/"), "a/b/c");
    EXPECT_EQ(canonicalizePath("a/./b"), "a/b");
    EXPECT_EQ(canonicalizePath("a/x/../b"), "a/b");
    EXPECT_EQ(canonicalizePath("../a"), "a");
    EXPECT_EQ(canonicalizePath("/a/b"), "/a/b");
    EXPECT_EQ(canonicalizePath("\\a"), "/a");
    EXPECT_EQ(canonicalizePath(""), "");
}

TEST(ArchivePath, LookupKey) {
    EXPECT_EQ(makeLookupKey("Achievements\\Rule\\Config.txt"), "achievements/rule/config.txt");
    EXPECT_TRUE(makeLookupKey(std::string(258, 'a')).has_value());
    EXPECT_FALSE(makeLookupKey(std::string(259, 'a')).has_value());
}

TEST(ArchivePath, JetNameHash) {
    EXPECT_EQ(jetNameHash(""), 0u);
    EXPECT_EQ(jetNameHash("a"), 97u);
    EXPECT_EQ(jetNameHash("ab"), (97u << 3) ^ 98u);
    EXPECT_EQ(jetNameHash("\xFF"), 0xFFFFFFFFu);  // sign-extended byte (movsx at 0x10015114)
}

TEST(Kuid, Parse) {
    EXPECT_EQ(parseKuid("<kuid:447264:1011>"), (Kuid{447264, 1011, std::nullopt}));
    EXPECT_EQ(parseKuid("<KUID2:90361:120204:2>"), (Kuid{90361, 120204, 2}));
    EXPECT_EQ(parseKuid("kuid -3 100"), (Kuid{-3, 100, std::nullopt}));
    EXPECT_EQ(parseKuid("kuid2 82412 3020903 1"), (Kuid{82412, 3020903, 1}));
    EXPECT_EQ(parseKuid("  <kuid:1:2>  "), (Kuid{1, 2, std::nullopt}));
    EXPECT_FALSE(parseKuid("<kuid:1>"));
    EXPECT_FALSE(parseKuid("<kuid:1:2"));
    EXPECT_FALSE(parseKuid("<kuid:a:2>"));
    EXPECT_FALSE(parseKuid("<kuid2:1:2>"));
    EXPECT_FALSE(parseKuid("<kuid:99999999999:2>"));
    EXPECT_FALSE(parseKuid(""));
}

TEST(Kuid, Format) {
    EXPECT_EQ(formatKuid({447264, 1011, std::nullopt}), "<kuid:447264:1011>");
    EXPECT_EQ(formatKuid({90361, 120204, 2}), "<kuid2:90361:120204:2>");
}

TEST(Kuid, FolderHashMatchesBuiltinSamples) {
    // Folder names seen in Builtin\8.ja.
    EXPECT_EQ(kuidFolderKey({447264, 1073, std::nullopt}), "hash-c0/kuid 447264 1073");
    EXPECT_EQ(kuidFolderKey({90361, 120204, 2}), "hash-c0/kuid2 90361 120204 2");
    EXPECT_EQ(kuidFolderHash({0, 0, std::nullopt}), 0);
    EXPECT_EQ(kuidFolderHash({-1, 0, std::nullopt}), 0);  // 4 x 0xFF xor to 0
    EXPECT_EQ(kuidFolderHash({0x01, 0x10, std::nullopt}), 0x11);
}

namespace {

class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        if (const char* old = std::getenv(name)) {
            old_ = old;
            hadOld_ = true;
        }
        if (value) {
            ::setenv(name, value, 1);
        } else {
            ::unsetenv(name);
        }
    }
    ~ScopedEnv() {
        if (hadOld_) {
            ::setenv(name_, old_.c_str(), 1);
        } else {
            ::unsetenv(name_);
        }
    }

private:
    const char* name_;
    std::string old_;
    bool hadOld_ = false;
};

}  // namespace

TEST(DataDirs, PortDataDir) {
    ScopedEnv xdg("XDG_DATA_HOME", nullptr);
    ScopedEnv home("HOME", "/home/dad");
    EXPECT_EQ(trainz::common::portDataDir(), std::filesystem::path("/home/dad/.local/share/trainz-native"));
    ScopedEnv xdg2("XDG_DATA_HOME", "/data");
    EXPECT_EQ(trainz::common::portDataDir(), std::filesystem::path("/data/trainz-native"));
}

TEST(DataDirs, IsInside) {
    using trainz::common::isInsideOrSame;
    EXPECT_TRUE(isInsideOrSame("/games/trainz", "/games/trainz"));
    EXPECT_TRUE(isInsideOrSame("/games/trainz/out/x", "/games/trainz"));
    EXPECT_TRUE(isInsideOrSame("/games/trainz/a/../b", "/games/trainz"));
    EXPECT_FALSE(isInsideOrSame("/games/trainz2", "/games/trainz"));
    EXPECT_FALSE(isInsideOrSame("/games/trainz/../other", "/games/trainz"));
    EXPECT_FALSE(isInsideOrSame("/home/dad/.local/share/trainz-native", "/games/trainz"));
}

TEST(DataDirs, ProtectsTheInstall) {
    trainz::test::TempDir dir;
    std::filesystem::create_directories(dir.path() / "Builtin");
    std::ofstream(dir.path() / "Builtin" / "8.ja") << "x";
    ScopedEnv data("TRAINZ_DATA_DIR", nullptr);
    const auto guards = trainz::common::protectedDirsFor(dir.path() / "Builtin" / "8.ja");
    ASSERT_FALSE(guards.empty());
    EXPECT_TRUE(trainz::common::isInsideOrSame(dir.path() / "Builtin", guards[0]));
    EXPECT_TRUE(trainz::common::isInsideOrSame(dir.path(), guards[0]));
}
