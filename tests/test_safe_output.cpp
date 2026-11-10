// SafeOutputDir must never let a write land outside the port data folder or
// inside a protected (game) folder, whatever symlinks are lying around.
#include "JaTestBuilder.h"

#include "common/SafeOutput.h"
#include "content/JaArchive.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;
using trainz::common::SafeOutputDir;
using namespace trainz::test;

namespace {

class SafeOutputTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (const char* old = std::getenv("XDG_DATA_HOME")) {
            oldXdg = old;
            hadXdg = true;
        }
        data = tmp.path() / "data";
        game = tmp.path() / "TS2010";
        fs::create_directories(data);
        fs::create_directories(game / "Builtin");
        std::ofstream(game / "Base.ja") << "game file";
        ::setenv("XDG_DATA_HOME", data.c_str(), 1);
        port = data / "trainz-native";
    }
    void TearDown() override {
        if (hadXdg) {
            ::setenv("XDG_DATA_HOME", oldXdg.c_str(), 1);
        } else {
            ::unsetenv("XDG_DATA_HOME");
        }
    }

    static void writeText(int fd, const std::string& s) {
        if (::write(fd, s.data(), s.size()) != static_cast<ssize_t>(s.size())) {
            throw std::runtime_error("short write");
        }
    }

    std::size_t gameFileCount() const {
        std::size_t n = 0;
        for (const auto& e : fs::recursive_directory_iterator(game)) {
            (void)e;
            ++n;
        }
        return n;
    }

    TempDir tmp;
    fs::path data, game, port;
    std::string oldXdg;
    bool hadXdg = false;
    std::vector<fs::path> guards() const { return {game}; }
};

}  // namespace

TEST_F(SafeOutputTest, WritesInsideThePortFolder) {
    const auto out = SafeOutputDir::open(port / "extracted" / "Base", guards());
    out.createFile("a/b/c.txt", [](int fd) { writeText(fd, "hello"); });
    std::ifstream in(port / "extracted" / "Base" / "a" / "b" / "c.txt");
    std::string s;
    in >> s;
    EXPECT_EQ(s, "hello");
}

TEST_F(SafeOutputTest, RefusesOutsideThePortFolder) {
    EXPECT_THROW(SafeOutputDir::open(tmp.path() / "elsewhere", guards()), std::runtime_error);
    EXPECT_THROW(SafeOutputDir::open(port / ".." / "sibling", guards()), std::runtime_error);
    EXPECT_THROW(SafeOutputDir::open(game, guards()), std::runtime_error);
    EXPECT_FALSE(fs::exists(tmp.path() / "elsewhere"));
}

TEST_F(SafeOutputTest, NeverOverwrites) {
    const auto out = SafeOutputDir::open(port / "x", guards());
    out.createFile("f.txt", [](int fd) { writeText(fd, "one"); });
    EXPECT_THROW(out.createFile("f.txt", [](int fd) { writeText(fd, "two"); }), std::runtime_error);
    std::ifstream in(port / "x" / "f.txt");
    std::string s;
    in >> s;
    EXPECT_EQ(s, "one");
}

TEST_F(SafeOutputTest, SymlinkedSubfolderCannotReachTheGame) {
    const auto out = SafeOutputDir::open(port / "extracted", guards());
    fs::create_symlink(game, port / "extracted" / "evil");
    const auto before = gameFileCount();
    EXPECT_THROW(out.createFile("evil/new.txt", [](int fd) { writeText(fd, "x"); }), std::runtime_error);
    EXPECT_THROW(out.createFile("evil/Builtin/new.txt", [](int fd) { writeText(fd, "x"); }), std::runtime_error);
    EXPECT_EQ(gameFileCount(), before);
    EXPECT_FALSE(fs::exists(game / "new.txt"));
}

TEST_F(SafeOutputTest, SymlinkedOutputFolderIsRefused) {
    fs::create_directories(port);
    fs::create_symlink(game, port / "link");
    EXPECT_THROW(SafeOutputDir::open(port / "link", guards()), std::runtime_error);
    EXPECT_THROW(SafeOutputDir::open(port / "link" / "sub", guards()), std::runtime_error);
    EXPECT_FALSE(fs::exists(game / "sub"));
}

TEST_F(SafeOutputTest, SymlinkedFinalFileIsNotFollowed) {
    const auto out = SafeOutputDir::open(port / "y", guards());
    fs::create_symlink(game / "Base.ja", port / "y" / "Base.ja");
    EXPECT_THROW(out.createFile("Base.ja", [](int fd) { writeText(fd, "clobber"); }), std::runtime_error);
    std::ifstream in(game / "Base.ja");
    std::string s;
    std::getline(in, s);
    EXPECT_EQ(s, "game file");
}

TEST_F(SafeOutputTest, PortFolderPointingIntoTheGameIsRefused) {
    fs::create_symlink(game, port);
    EXPECT_THROW(SafeOutputDir::open(port / "z", guards()), std::runtime_error);
    EXPECT_FALSE(fs::exists(game / "z"));
}

TEST_F(SafeOutputTest, BadRelativePathsAreRefused) {
    const auto out = SafeOutputDir::open(port / "w", guards());
    auto noop = [](int) {};
    EXPECT_THROW(out.createFile("../escape.txt", noop), std::runtime_error);
    EXPECT_THROW(out.createFile("a/./b.txt", noop), std::runtime_error);
    EXPECT_THROW(out.createFile("/abs.txt", noop), std::runtime_error);
    EXPECT_THROW(out.createFile("", noop), std::runtime_error);
    EXPECT_FALSE(fs::exists(port / "escape.txt"));
}

TEST_F(SafeOutputTest, FailedWriteLeavesNoPartialFile) {
    const auto out = SafeOutputDir::open(port / "v", guards());
    EXPECT_THROW(out.createFile("p.bin", [](int) { throw std::runtime_error("boom"); }), std::runtime_error);
    EXPECT_FALSE(fs::exists(port / "v" / "p.bin"));
}

TEST(Duplicates, OnlyTheFirstRowWins) {
    TempDir dir;
    JaTestBuilder b;
    b.addPlain("dir/dup.txt", bytesOf("first")).addPlain("dir/other.txt", bytesOf("o")).addPlain("DIR/Dup.txt",
                                                                                                bytesOf("second"));
    const auto a = trainz::content::JaArchive::open(dir.write("d.ja", b.build()));
    const auto all = a.listFolder("dir");
    ASSERT_EQ(all.size(), 3u);
    std::size_t winners = 0;
    for (const auto* e : all) {
        if (a.isLookupWinner(*e)) {
            ++winners;
            if (e->name == "dir/dup.txt") {
                EXPECT_EQ(a.read(*e), bytesOf("first"));
            }
        }
    }
    EXPECT_EQ(winners, 2u);
    EXPECT_FALSE(a.isLookupWinner(*all[2]));
}
