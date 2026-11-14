// KUIDs: the IDs every Trainz asset has.
//
// Text forms (strings in trainz.exe and TADDaemon.exe):
//   <kuid:USER:CONTENT>            e.g. <kuid:447264:1011>
//   <kuid2:USER:CONTENT:VERSION>
// USER may be negative.
//
// Folder inside built-in .ja archives (seen in Builtin\8.ja; lowercased by the
// archive reader):
//   hash-XX/kuid USER CONTENT/...
//   hash-XX/kuid2 USER CONTENT VERSION/...
// XX = XOR of the 8 little-endian bytes of (int32 USER, int32 CONTENT), as two
// upper-case hex digits. This rule matched all 499 asset folders checked in
// Builtin\8.ja, but the code that builds it (in TADDaemon.exe) has NOT been
// traced, so treat it as observed, not proven.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace trainz::content {

struct Kuid {
    std::int32_t user = 0;
    std::int32_t content = 0;
    std::optional<std::int32_t> version;  // set for kuid2

    bool operator==(const Kuid&) const = default;

    // True when the game treats a (user, version) pair as a plain kuid.
    // TADDaemon.exe 0x51919a..0x5191de names the folder "kuid U C" when
    // version == 0 and user >= 0, or version == 127 and user < 0; otherwise
    // "kuid2 U C V". (Seen in data too: "<KUID2:2512:3:0>" lives in "kuid 2512 3".)
    static bool isPlainVersion(std::int32_t user, std::int32_t version) {
        return (version == 0 && user >= 0) || (version == 127 && user < 0);
    }

    // Drops a version that the game treats as "no version".
    Kuid normalized() const {
        Kuid k = *this;
        if (k.version && isPlainVersion(k.user, *k.version)) {
            k.version.reset();
        }
        return k;
    }
};

// Parses "<kuid:U:C>", "<kuid2:U:C:V>" (case-insensitive, angle brackets
// optional) and the folder forms "kuid U C" / "kuid2 U C V".
std::optional<Kuid> parseKuid(std::string_view text);

// Like parseKuid, but also accepts the looser forms found inside config files:
// trailing NUL bytes or spaces, leading zeros ("<KUID2:33404:22112:02>") and a
// bare "USER:CONTENT" with no brackets ("37028:27013").
std::optional<Kuid> parseKuidLoose(std::string_view text);

// Decodes the 8-byte KUID value of a .chump file (type 13): two little-endian
// 32-bit words, the first holding USER in its low 25 bits (signed) and VERSION
// in its top 7 bits. A negative USER has all-ones top bits, which mean "no
// version". The plain/kuid2 rule is traced (see Kuid::isPlainVersion); the
// 25/7-bit split is OBSERVED: it matched all 1468 config.chump files in
// Builtin\8.ja against their folder names.
Kuid unpackChumpKuid(std::uint32_t packedUser, std::int32_t content);

// "<kuid:U:C>" or "<kuid2:U:C:V>".
std::string formatKuid(const Kuid& k);

// The XX in "hash-XX".
std::uint8_t kuidFolderHash(const Kuid& k);

// "hash-xx/kuid u c" or "hash-xx/kuid2 u c v" in the lowercase form the
// archive index uses (no trailing slash). Version 0 counts as no version.
std::string kuidFolderKey(const Kuid& k);

}  // namespace trainz::content
