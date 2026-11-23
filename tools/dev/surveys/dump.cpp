#include "content/ByteOrder.h"
#include "content/JaArchive.h"
#include "content/SessionFile.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace trainz::content;
int main(int argc, char** argv) {
    JaArchive a = JaArchive::open(argv[1]);
    auto p = parseSessionProfile(a.read(*a.find(std::string(argv[2]) + "/profile.dat")));
    const int want = std::atoi(argv[3]);
    for (const auto& blk : p.blocks) if (blk.container) for (const auto& it : blk.container->items) {
        if (it.tag != "trCt" || it.index != want) continue;
        const auto& d = it.payload;
        std::printf("size %zu count %u piece %d\n", d.size(), loadU32LE(d.data()), static_cast<int>(loadU32LE(d.data() + 8)));
        if (argc > 5) { const std::size_t from = std::atoi(argv[4]), to = std::atoi(argv[5]);
            for (std::size_t q = from; q < to && q < d.size(); q += 16) { std::printf("  %5zu:", q); for (std::size_t m = q; m < q + 16 && m < d.size(); ++m) std::printf(" %02x", d[m]);
                std::printf("  "); for (std::size_t m = q; m < q + 16 && m < d.size(); ++m) std::putchar(d[m] >= 32 && d[m] < 127 ? d[m] : '.'); std::printf("\n"); } return 0; }
        for (std::size_t k = 8; k + 13 <= d.size(); ++k) {
            static const std::uint8_t z5[5] = {0,0,0,0,0}; static const std::uint8_t three[7] = {0,0,0,3,0,0,0};
            if (std::memcmp(d.data()+k, z5, 5) == 0 && d[k+5] < 16 && std::memcmp(d.data()+k+6, three, 7) == 0) {
                const std::size_t s = k - 8; const std::uint8_t* r = d.data() + s + 13;
                float x, y; std::memcpy(&x, r + 16, 4); std::memcpy(&y, r + 20, 4);
                const std::uint32_t len = s + 13 + 44 <= d.size() ? loadU32LE(r + 40) : 0;
                std::string name; if (len > 0 && len < 64 && s + 13 + 44 + len <= d.size()) name.assign(reinterpret_cast<const char*>(r + 44), len - 1);
                std::printf("  at %6zu ver %u kuid %u/%d board %d,%d x %.1f y %.1f name[%u] '%s'\n", s, d[k+5], loadU32LE(d.data()+s), static_cast<int>(loadU32LE(d.data()+s+4)),
                    static_cast<std::int16_t>(loadU16LE(r+12)), static_cast<std::int16_t>(loadU16LE(r+14)), x, y, len, name.c_str());
            }
        }
    }
}
