// How the game turns a file name into an archive lookup key.
//
// Based on KernelStandard_7c.dll (see docs/formats/ja.md):
//   - names are limited to 259 characters (0x10023790 / 0x100237e0: length >= 0x103 is rejected)
//   - names are lowercased with MSVC _strlwr in the "C" locale, i.e. ASCII A-Z only
//     (stored names: 0x100260e9; lookups: 0x100253c4, 0x10026c68)
//   - lookups are canonicalized by 0x10023a20: split on '\' and '/', drop empty
//     parts and ".", ".." removes the previous part, join with '/', and a
//     leading separator becomes a leading '/'
//   - the bucket hash is 0x100150f0: h = (h << 3) ^ (signed char)c
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace trainz::content {

inline constexpr std::size_t kMaxArchiveNameLength = 258;  // longest name the game accepts (< 0x103)

// ASCII-only lowercase, like _strlwr in the "C" locale.
std::string asciiLower(std::string_view s);

// The full lookup key the game builds from a requested name: length check,
// lowercase, then canonicalize. Returns nullopt for names the game rejects (too long).
std::optional<std::string> makeLookupKey(std::string_view requested);

// Just the canonicalization step (0x10023a20), without lowercasing.
std::string canonicalizePath(std::string_view path);

// The engine's string hash used for the archive's name table (0x100150f0).
std::uint32_t jetNameHash(std::string_view s);

}  // namespace trainz::content
