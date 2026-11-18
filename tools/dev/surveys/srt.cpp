#include <algorithm>
#include "content/JaArchive.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <filesystem>
using namespace trainz::content;
int main(int argc, char** argv) {
    std::map<std::string, int> magic, atlas; int sane = 0, total = 0; double hmin = 1e9, hmax = 0;
    for (int i = 1; i < argc; ++i) { JaArchive a = JaArchive::open(argv[i]);
        for (const auto& e : a.entries()) { if (!e.name.ends_with(".srt")) continue; ++total;
            auto d = a.readLimited(e, 64 << 20); magic[std::string(reinterpret_cast<const char*>(d.data()), std::min<std::size_t>(10, d.size()))]++;
            const std::string dir = e.name.substr(0, e.name.find_last_of('/') + 1);
            atlas[a.find(dir + "billboardatlas_diffuse.texture") ? "atlas beside it" : "no atlas beside it"]++;
            if (d.size() >= 60) { float b[6]; std::memcpy(b, d.data() + 36, 24); bool ok = true; for (float f : b) ok = ok && std::isfinite(f);
                ok = ok && b[3] > b[0] && b[4] > b[1] && b[5] > b[2]; if (ok) { ++sane; hmin = std::min<double>(hmin, b[5]); hmax = std::max<double>(hmax, b[5]); } } } }
    std::printf("%d .srt; header box sane in %d; top z %.1f..%.1f\n", total, sane, hmin, hmax);
    for (auto& [k, v] : magic) std::printf("  magic '%s': %d\n", k.c_str(), v);
    for (auto& [k, v] : atlas) std::printf("  %s: %d\n", k.c_str(), v);
}
