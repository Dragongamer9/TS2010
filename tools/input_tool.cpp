// input-tool: show what a keyboard.txt binds. Only reads the file.
//
//   input-tool list    KEYBOARD.txt [GROUP]
//   input-tool shared KEYBOARD.txt
//   input-tool press   KEYBOARD.txt GROUP[,GROUP...] "ctrl s"
#include "content/ContentError.h"
#include "content/ReadOnlyFile.h"
#include "input/ActionMap.h"
#include "input/KeyBindings.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace trainz::input;

namespace {

KeyBindings load(const char* path) {
    const auto f = trainz::content::ReadOnlyFile::open(path);
    if (f.size() > (1u << 20)) {
        throw trainz::content::ContentError("keyboard file is over 1 MiB; refusing to read it");
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(f.size()));
    f.readExact(0, data);
    return parseKeyBindings(data);
}

int usage() {
    std::cerr << "usage:\n  input-tool list    KEYBOARD.txt [GROUP]\n  input-tool shared KEYBOARD.txt\n"
                 "  input-tool press   KEYBOARD.txt GROUP[,GROUP...] \"ctrl s\"\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        return usage();
    }
    const std::string cmd = argv[1];
    try {
        const auto kb = load(argv[2]);
        for (const auto& w : kb.warnings) {
            std::cerr << "warning: " << w << "\n";
        }
        if (cmd == "list" && (argc == 3 || argc == 4)) {
            for (const auto& [group, actions] : kb.groups) {
                if (argc == 4 && group != argv[3]) {
                    continue;
                }
                std::cout << group << "\n";
                for (const auto& a : actions) {
                    std::cout << "  " << a.name;
                    if (a.combos.empty()) {
                        std::cout << "  (none)";
                    }
                    for (std::size_t i = 0; i < a.combos.size(); ++i) {
                        std::cout << (i == 0 ? "  " : ", ") << describe(a.combos[i]);
                    }
                    std::cout << "\n";
                }
            }
            return 0;
        }
        if (cmd == "shared" && argc == 3) {
            const auto shared = ActionMap(kb).sharedCombos();
            for (const auto& c : shared) {
                std::cout << c.group << "  " << c.combo << ":";
                for (const auto& a : c.actions) {
                    std::cout << " " << a;
                }
                std::cout << "\n";
            }
            std::cout << shared.size() << " combos bound to more than one action in the same group\n";
            return 0;
        }
        if (cmd == "press" && argc == 5) {
            std::vector<std::string> groups;
            std::stringstream gs(argv[3]);
            for (std::string g; std::getline(gs, g, ',');) {
                groups.push_back(g);
            }
            // Read the key the same way as a keyboard.txt value.
            std::vector<std::string> notes;
            const auto combos = parseComboList(argv[4], "key", notes);
            for (const auto& n : notes) {
                std::cerr << "warning: " << n << "\n";
            }
            if (combos.size() != 1) {
                std::cerr << "could not read the key \"" << argv[4] << "\"\n";
                return 2;
            }
            const auto hits = ActionMap(kb).actionsFor(groups, combos[0]);
            for (const auto& h : hits) {
                std::cout << h.group << "/" << h.action << "\n";
            }
            if (hits.empty()) {
                std::cout << "(no action)\n";
            }
            return 0;
        }
        return usage();
    } catch (const std::exception& e) {
        std::cerr << "input-tool: " << e.what() << "\n";
        return 1;
    }
}
