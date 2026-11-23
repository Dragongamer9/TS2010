#include "content/ByteOrder.h"
#include "content/ContentError.h"
#include "content/JaArchive.h"
#include "content/SessionFile.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
using namespace trainz::content;
int main(int argc, char** argv) {
    std::map<std::string, int> why; int shown = 0, total = 0;
    std::map<std::string, int> wideVer;
    for (int i = 1; i < argc; ++i) {
        JaArchive a = JaArchive::open(argv[i]);
        for (const auto& e : a.entries()) {
            if (!e.name.ends_with("/profile.dat")) continue;
            SessionProfile p; try { p = parseSessionProfile(a.read(e)); } catch (const ContentError&) { continue; }
            for (const auto& blk : p.blocks) if (blk.container) for (const auto& it : blk.container->items) {
                if (it.tag != "trCt") continue;
                const auto& d = it.payload;
                if (d.size() >= 4 && loadU32LE(d.data()) == 0xCAFFA1DAu) continue;
                ++total;
                if (decodeTrain(it)) continue;
                std::string r;
                const std::uint32_t count = d.size() >= 8 ? loadU32LE(d.data()) : 0;
                if (d.size() < 20 || loadU32LE(d.data() + 4) != 0) r = "header: word 1 not 0 or short";
                else if (count == 0) r = "count 0";
                else if (count > d.size() / 61) r = "count too big for size";
                else {
                    // any record version 0..15 with the "u32 3" after 5 zero bytes
                    std::vector<std::pair<std::size_t,int>> st;
                    for (std::size_t k = 8; k + 13 <= d.size(); ++k) {
                        static const std::uint8_t z5[5] = {0,0,0,0,0}; static const std::uint8_t three[7] = {0,0,0,3,0,0,0};
                        if (std::memcmp(d.data()+k, z5, 5) == 0 && d[k+5] < 16 && std::memcmp(d.data()+k+6, three, 7) == 0) st.push_back({k - 8, d[k+5]});
                    }
                    std::size_t known = 0; for (auto& [s, v] : st) known += (v == 1 || v == 4 || v == 5);
                    for (auto& [s, v] : st) if (!(v == 1 || v == 4 || v == 5)) wideVer[std::to_string(v)]++;
                    char b[160];
                    if (known != count) { std::snprintf(b, sizeof b, "pattern count %s (found %zu known-version, %zu any-version, want %u)", known < count ? "short" : "long", known, st.size(), count); r = known < count ? (st.size() == count ? "pattern short; other record versions fill it" : "pattern short") : "pattern long"; if (shown < 6) { ++shown; std::printf("%s #%d: %s\n", e.name.c_str(), it.index, b); } }
                    else if (!st.empty() && st[0].first != 20) r = "first vehicle not at byte 20";
                    else r = "a vehicle check (name/position/gap)";
                }
                why[r]++;
            }
        }
    }
    std::printf("old trains %d\n", total);
    for (auto& [k, v] : why) std::printf("%5d  %s\n", v, k.c_str());
    for (auto& [k, v] : wideVer) std::printf("other record version %s: %d\n", k.c_str(), v);
}
