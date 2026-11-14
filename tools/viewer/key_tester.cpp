// key-tester: open a small window and print which keyboard.txt actions each key press
// triggers. Only reads the keyboard file.
//
//   key-tester KEYBOARD.txt [GROUP[,GROUP...]]     (default groups: driver,gameplay)
#include "content/ReadOnlyFile.h"
#include "input/ActionMap.h"
#include "input/SdlKeys.h"

#include <SDL.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace trainz::input;

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: key-tester KEYBOARD.txt [GROUP[,GROUP...]]\n";
        return 2;
    }
    KeyBindings kb;
    try {
        const auto f = trainz::content::ReadOnlyFile::open(argv[1]);
        if (f.size() > (1u << 20)) {
            std::cerr << "keyboard file is over 1 MiB\n";
            return 1;
        }
        std::vector<std::uint8_t> data(static_cast<std::size_t>(f.size()));
        f.readExact(0, data);
        kb = parseKeyBindings(data);
    } catch (const std::exception& e) {
        std::cerr << "key-tester: " << e.what() << "\n";
        return 1;
    }
    std::vector<std::string> groups;
    std::stringstream gs(argc == 3 ? argv[2] : "driver,gameplay");
    for (std::string g; std::getline(gs, g, ',');) {
        groups.push_back(g);
    }
    const ActionMap map(kb);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("key-tester (Esc twice to quit)", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, 480, 120, 0);
    if (win == nullptr) {
        std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    std::cout << "Press keys in the window. Esc twice in a row quits.\n";
    bool running = true;
    bool lastWasEsc = false;
    while (running) {
        SDL_Event ev;
        if (SDL_WaitEvent(&ev) == 0) {
            break;
        }
        if (ev.type == SDL_QUIT) {
            running = false;
        } else if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0) {
            const auto combo = comboFromKey(ev.key.keysym.scancode, ev.key.keysym.mod);
            if (!combo) {
                std::cout << "(key " << SDL_GetScancodeName(ev.key.keysym.scancode) << " has no game name)\n";
                continue;
            }
            std::cout << describe(*combo) << " ->";
            const auto hits = map.actionsFor(groups, *combo);
            for (const auto& h : hits) {
                std::cout << " " << h.group << "/" << h.action;
            }
            std::cout << (hits.empty() ? " (no action)\n" : "\n");
            const bool esc = combo->key == "Esc";
            running = !(esc && lastWasEsc);
            lastWasEsc = esc;
        }
    }
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
