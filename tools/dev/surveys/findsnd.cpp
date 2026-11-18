#include "content/JaArchive.h"
#include <cstdio>
#include <string>
using namespace trainz::content;
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        JaArchive a = JaArchive::open(argv[i]);
        for (const auto& e : a.entries()) {
            const auto& n = e.name;
            if (n.size() > 4 && n.substr(n.size() - 4) == ".wav" &&
                (n.find("wheel") != std::string::npos || n.find("clack") != std::string::npos || n.find("rail") != std::string::npos ||
                 n.find("rolling") != std::string::npos || n.find("squeal") != std::string::npos || n.find("brake") != std::string::npos ||
                 n.find("joint") != std::string::npos))
                std::printf("%s: %s %u\n", argv[i], n.c_str(), e.originalSize);
        }
    }
}
