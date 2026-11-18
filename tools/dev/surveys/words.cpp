#include "content/JaArchive.h"
#include "content/SessionFile.h"
#include "content/ContentError.h"
#include <cstdio>
#include <map>
using namespace trainz::content;
int main(int argc, char** argv) {
    std::map<std::string, long> n; long refused = 0, oldTr = 0, newTr = 0;
    for (int i = 1; i < argc; ++i) { JaArchive a = JaArchive::open(argv[i]);
        for (const auto& e : a.entries()) { if (!e.name.ends_with("/profile.dat")) continue;
            SessionProfile p; try { p = parseSessionProfile(a.read(e)); } catch (const ContentError&) { continue; }
            for (const auto& blk : p.blocks) if (blk.container) for (const auto& it : blk.container->items) { if (it.tag != "trCt") continue;
                auto t = decodeTrain(it); if (!t) { ++refused; continue; } (t->oldLayout ? oldTr : newTr)++;
                for (const auto& v : t->vehicles) n[std::string(t->oldLayout ? "old" : "v5") + " word " + std::to_string(v.wordAfterKuid)]++; } } }
    std::printf("trains: v5 %ld, old %ld, refused %ld\n", newTr, oldTr, refused);
    for (auto& [k, v] : n) std::printf("  %s: %ld vehicles\n", k.c_str(), v);
}
