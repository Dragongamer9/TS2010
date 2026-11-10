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
};

// Parses "<kuid:U:C>", "<kuid2:U:C:V>" (case-insensitive, angle brackets
// optional) and the folder forms "kuid U C" / "kuid2 U C V".
std::optional<Kuid> parseKuid(std::string_view text);

// "<kuid:U:C>" or "<kuid2:U:C:V>".
std::string formatKuid(const Kuid& k);

// The XX in "hash-XX".
std::uint8_t kuidFolderHash(const Kuid& k);

// "hash-xx/kuid u c" or "hash-xx/kuid2 u c v" in the lowercase form the
// archive index uses (no trailing slash).
std::string kuidFolderKey(const Kuid& k);

}  // namespace trainz::content
