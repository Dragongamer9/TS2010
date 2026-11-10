#include "JaTestBuilder.h"

#include "content/AssetConfig.h"
#include "content/ContentError.h"
#include "content/Kuid.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace trainz::content;
using namespace trainz::test;

namespace {

// ---- building .chump bytes (layout from docs/formats/config.md)

Bytes item(const std::string& key, std::uint8_t type, const Bytes& value) {
    Bytes b;
    const auto keyLen = static_cast<std::uint8_t>(key.size() + 1);
    putU32(b, static_cast<std::uint32_t>(1 + keyLen + 1 + value.size()));
    b.push_back(keyLen);
    b.insert(b.end(), key.begin(), key.end());
    b.push_back(0);
    b.push_back(type);
    b.insert(b.end(), value.begin(), value.end());
    return b;
}

Bytes strValue(const std::string& s, std::size_t extraNuls = 0) {
    Bytes b(s.begin(), s.end());
    b.resize(b.size() + 1 + extraNuls, 0);
    return b;
}

Bytes u32s(std::initializer_list<std::uint32_t> vs) {
    Bytes b;
    for (auto v : vs) {
        putU32(b, v);
    }
    return b;
}

Bytes floatValue(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return u32s({bits});
}

Bytes chump(const Bytes& body, std::uint32_t version = 1) {
    Bytes b = bytesOf("ACS$");
    putU32(b, version);
    putU32(b, 0);
    putU32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes b;
    for (const auto& p : parts) {
        b.insert(b.end(), p.begin(), p.end());
    }
    return b;
}

AssetConfig text(const std::string& s) { return parseConfigText(bytesOf(s)); }

}  // namespace

// ------------------------------------------------------------------ KUIDs

TEST(KuidPacking, VersionInTopSevenBits) {
    EXPECT_EQ(unpackChumpKuid(0x040160F9u, 120204), (Kuid{90361, 120204, 2}));
    EXPECT_EQ(unpackChumpKuid(0x0006D320u, 1073), (Kuid{447264, 1073, std::nullopt}));
    EXPECT_EQ(unpackChumpKuid((127u << 25) | 113998u, 9002), (Kuid{113998, 9002, 127}));
}

TEST(KuidPacking, NegativeUsers) {
    EXPECT_EQ(unpackChumpKuid(0xFFFFFFF6u, 137), (Kuid{-10, 137, std::nullopt}));
    EXPECT_EQ(unpackChumpKuid(0xFFFFFFFFu, 5), (Kuid{-1, 5, std::nullopt}));
}

TEST(KuidPacking, FolderRuleFromTadDaemon) {
    // TADDaemon.exe 0x51919a: plain iff (v == 0 && u >= 0) || (v == 127 && u < 0).
    EXPECT_EQ(unpackChumpKuid(0x01FFFFF6u, 137), (Kuid{-10, 137, 0}));  // negative user, version bits 0
    EXPECT_EQ(kuidFolderKey({-10, 137, 0}).substr(8), "kuid2 -10 137 0");
    EXPECT_EQ(kuidFolderKey({-10, 137, 127}).substr(8), "kuid -10 137");
    EXPECT_EQ(kuidFolderKey({5, 6, 127}).substr(8), "kuid2 5 6 127");
}

TEST(KuidPacking, LooseTextForms) {
    EXPECT_EQ(parseKuidLoose("<KUID2:33404:22112:02>"), (Kuid{33404, 22112, 2}));
    EXPECT_EQ(parseKuidLoose(std::string("<kuid:1:2>\0\0", 12)), (Kuid{1, 2, std::nullopt}));
    EXPECT_EQ(parseKuidLoose("37028:27013"), (Kuid{37028, 27013, std::nullopt}));
    EXPECT_FALSE(parseKuidLoose("37028"));
    EXPECT_FALSE(parseKuidLoose("scenery"));
}

TEST(KuidPacking, VersionZeroIsPlainKuid) {
    EXPECT_EQ(kuidFolderKey({2512, 3, 0}), kuidFolderKey({2512, 3, std::nullopt}));
    EXPECT_EQ(kuidFolderKey({2512, 3, 0}), "hash-da/kuid 2512 3");  // folder seen in Builtin\\8.ja
}

// ------------------------------------------------------------------ chump

TEST(Chump, AllValueTypes) {
    const Bytes body = cat({
        item("kuid", 13, u32s({0x040160F9u, 120204})),
        item("kind", 3, strValue("bridge")),
        item("padded", 3, strValue("x", 2)),
        item("length", 2, floatValue(8.75f)),
        item("ints", 1, u32s({1, 2, 0xFFFFFFFFu})),
        item("thumbnail", 4, Bytes{0xFF, 0xD8, 0xFF}),
        item("license", 5, Bytes{}),
        item("thumbnails", 0, cat({item("image", 3, strValue("t.jpg")), item("width", 1, u32s({240}))})),
    });
    const auto cfg = parseChump(chump(body));
    EXPECT_EQ(cfg.format, ConfigFormat::Chump);
    EXPECT_EQ(cfg.chumpVersion, 1u);
    ASSERT_EQ(cfg.root.children.size(), 8u);
    EXPECT_EQ(cfg.assetKuid(), (Kuid{90361, 120204, 2}));
    EXPECT_EQ(cfg.root.child("KIND")->text, "bridge");
    EXPECT_EQ(cfg.root.child("padded")->text, "x");
    EXPECT_FLOAT_EQ(cfg.root.child("length")->floats.at(0), 8.75f);
    EXPECT_EQ(cfg.root.child("ints")->ints, (std::vector<std::int32_t>{1, 2, -1}));
    EXPECT_EQ(cfg.root.child("thumbnail")->type, ConfigType::Binary);
    EXPECT_EQ(cfg.root.child("license")->type, ConfigType::Null);
    const auto* g = cfg.root.child("thumbnails");
    ASSERT_EQ(g->type, ConfigType::Group);
    EXPECT_EQ(g->child("width")->ints.at(0), 240);
    EXPECT_TRUE(cfg.warnings.empty());
}

TEST(Chump, KuidStoredAsString) {
    const auto cfg = parseChump(chump(item("kuid", 3, strValue("<KUID2:2512:3:0>", 1))));
    EXPECT_EQ(cfg.assetKuid()->normalized(), (Kuid{2512, 3, std::nullopt}));
}

TEST(Chump, UpToFiveTrailingBytesAreIgnored) {
    Bytes body = item("a", 1, u32s({7}));
    body.insert(body.end(), {9, 9, 9, 9, 9});
    const auto cfg = parseChump(chump(body));
    ASSERT_EQ(cfg.root.children.size(), 1u);
    EXPECT_EQ(cfg.root.children[0].ints.at(0), 7);
}

TEST(Chump, UnknownTypesAreKeptNotFatal) {
    const auto cfg = parseChump(chump(item("odd", 9, Bytes{1, 2, 3})));
    ASSERT_EQ(cfg.root.children.size(), 1u);
    EXPECT_EQ(cfg.root.children[0].type, ConfigType::Unknown);
    EXPECT_EQ(cfg.root.children[0].bytes, (Bytes{1, 2, 3}));
    EXPECT_FALSE(cfg.warnings.empty());
}

TEST(Chump, EmptyBodyIsAllowed) {
    EXPECT_TRUE(parseChump(chump(Bytes{})).root.children.empty());
}

TEST(Chump, DamagedFilesThrow) {
    EXPECT_THROW(parseChump(bytesOf("ACS$")), ContentError);
    EXPECT_THROW(parseChump(bytesOf("NOPE____________")), ContentError);
    Bytes tooBig = chump(item("a", 1, u32s({1})));
    setU32(tooBig, 12, 1000);
    EXPECT_THROW(parseChump(tooBig), ContentError);
    Bytes itemPastEnd = chump(item("a", 1, u32s({1})));
    setU32(itemPastEnd, 16, 500);
    EXPECT_THROW(parseChump(itemPastEnd), ContentError);
    Bytes keyPastEnd = chump(item("a", 1, u32s({1})));
    keyPastEnd[20] = 200;
    EXPECT_THROW(parseChump(keyPastEnd), ContentError);
    Bytes tooSmall = chump(item("abc", 1, u32s({1})));
    setU32(tooSmall, 16, 2);  // smaller than keyLen + 2
    EXPECT_THROW(parseChump(tooSmall), ContentError);
}

TEST(Chump, DeepNestingIsRefused) {
    Bytes inner = item("x", 1, u32s({1}));
    for (int i = 0; i < 70; ++i) {
        inner = item("g", 0, inner);
    }
    EXPECT_THROW(parseChump(chump(inner)), ContentError);
}

TEST(Chump, EveryTruncationAndByteChangeIsSafe) {
    const Bytes good = chump(cat({item("kuid", 13, u32s({5, 6})), item("kind", 3, strValue("scenery")),
                                  item("g", 0, cat({item("a", 1, u32s({1, 2})), item("b", 2, floatValue(1.5f))}))}));
    auto tryParse = [](const Bytes& b) {
        try {
            (void)parseChump(b);
        } catch (const ContentError&) {
        }
    };
    for (std::size_t len = 0; len < good.size(); ++len) {
        tryParse(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len)));
    }
    for (std::size_t pos = 0; pos < good.size(); ++pos) {
        for (std::uint8_t v : {std::uint8_t{0}, std::uint8_t{0xFF}, std::uint8_t{0x7F}}) {
            Bytes b = good;
            b[pos] = v;
            tryParse(b);
        }
    }
}

// ------------------------------------------------------------- config.txt

TEST(ConfigText, TypicalAssetConfig) {
    const auto cfg = text(
        "\xEF\xBB\xBF\r\n"
        "kuid                                    <kuid:447264:1011>\r\n"
        "kind                                    \"behavior\"\r\n"
        "trainz-build                            3.1\r\n"
        "offsets                                 0,4.5,-1\r\n"
        "count                                   12\r\n"
        "type                                    Bridge\r\n"
        "script-include-table\r\n"
        "{\r\n"
        "  0                                     <kuid:447264:1000>\r\n"
        "}\r\n"
        "string-table {\r\n"
        "  complete_name                         \"Session completion\"\r\n"
        "}\r\n");
    EXPECT_EQ(cfg.format, ConfigFormat::Text);
    EXPECT_EQ(cfg.assetKuid(), (Kuid{447264, 1011, std::nullopt}));
    EXPECT_EQ(cfg.root.child("kind")->text, "behavior");
    EXPECT_EQ(cfg.root.child("trainz-build")->type, ConfigType::Floats);
    EXPECT_EQ(cfg.root.child("offsets")->floats.size(), 3u);
    EXPECT_EQ(cfg.root.child("count")->ints.at(0), 12);
    EXPECT_EQ(cfg.root.child("type")->text, "Bridge");
    EXPECT_EQ(cfg.root.child("script-include-table")->child("0")->kuid, (Kuid{447264, 1000, std::nullopt}));
    EXPECT_EQ(cfg.root.child("string-table")->child("complete_name")->text, "Session completion");
}

TEST(ConfigText, MultiLineStrings) {
    const auto cfg = text("html \"<b>one\r\ntwo</b>\"\r\nnext \"x\"\r\n");
    EXPECT_EQ(cfg.root.child("html")->text, "<b>one\r\ntwo</b>");
    EXPECT_EQ(cfg.root.child("next")->text, "x");
}

TEST(ConfigText, StringValueOnTheNextLine) {
    // Builtin\3.ja kuid -13:151000: the value of "description" starts on the next line.
    const auto cfg = text("kind \"traincar\"\r\ndescription\r\n\"Double Door Boxcar\"\r\nnext 1\r\n");
    EXPECT_EQ(cfg.root.child("description")->text, "Double Door Boxcar");
    EXPECT_EQ(cfg.root.child("next")->ints.at(0), 1);
}

TEST(ConfigText, KeyWithoutValueAndComments) {
    const auto cfg = text("// a comment\nempty\nafter 1\n");
    ASSERT_EQ(cfg.root.children.size(), 2u);
    EXPECT_EQ(cfg.root.child("empty")->type, ConfigType::Null);
    EXPECT_EQ(cfg.root.child("after")->ints.at(0), 1);
}

TEST(ConfigText, ErrorsNameTheLine) {
    try {
        (void)text("a 1\nb \"never closed\n");
        FAIL() << "expected an error";
    } catch (const ContentError& e) {
        EXPECT_NE(std::string(e.what()).find("line 2"), std::string::npos) << e.what();
    }
    EXPECT_THROW(text("g {\n a 1\n"), ContentError);
    EXPECT_THROW(text("}\n"), ContentError);
}

TEST(ConfigText, DeepNestingIsRefused) {
    std::string s;
    for (int i = 0; i < 70; ++i) {
        s += "g {\n";
    }
    EXPECT_THROW(text(s), ContentError);
}

TEST(ConfigText, EveryTruncationIsSafe) {
    const std::string good = "kuid <kuid:1:2>\nkind \"a\nb\"\ng\n{\n x 1,2\n}\n";
    for (std::size_t len = 0; len <= good.size(); ++len) {
        try {
            (void)text(good.substr(0, len));
        } catch (const ContentError&) {
        }
    }
}

TEST(AssetConfigApi, PicksFormatByMagic) {
    EXPECT_EQ(parseAssetConfig(chump(Bytes{})).format, ConfigFormat::Chump);
    EXPECT_EQ(parseAssetConfig(bytesOf("kind \"x\"\n")).format, ConfigFormat::Text);
}

TEST(AssetConfigApi, FormatRoundTripsThroughTheTextParser) {
    const auto original = text("kuid <kuid2:1:2:3>\nkind \"scenery\"\nnums 1,2\ng\n{\n f 0.5\n}\n");
    const std::string printed = formatConfigText(original.root);
    const auto again = text(printed);
    EXPECT_EQ(again.assetKuid(), original.assetKuid());
    EXPECT_EQ(again.root.child("g")->child("f")->floats.at(0), 0.5f);
    EXPECT_EQ(again.root.child("nums")->ints, (std::vector<std::int32_t>{1, 2}));
}
