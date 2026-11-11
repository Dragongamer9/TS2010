// route-viewer: fly around a route in a window, drawn by the port's own software renderer
// (src/view/SceneRender) from the decoded route, session and assets - the same scene as
// `route-tool view`. A viewer for checking the decoders, not the game.
//
//   route-viewer --install DIR --list                     the routes and their sessions, by name
//   route-viewer --install DIR --route NAME [--session NAME] [--at X Y] [--radius M] [--frames N]
//   route-viewer ARCHIVE.ja ROUTE_FOLDER [--session ARCHIVE.ja SESSION_FOLDER] [--install DIR]
//                [--at X Y] [--radius M] [--frames N]
//
// NAME is a route's or session's name (or part of it, if only one matches) or its KUID.
// Exit codes: 0 done, 1 error, 2 bad arguments, 3 no route/session matches NAME, 4 several do.
//
// --frames N: draw N frames (turning a little each time) and quit, for testing; with
// SDL_VIDEODRIVER=offscreen no window appears.
// Keys: W/S or Up/Down forward/back, A/D sideways, Left/Right turn, PgUp/PgDn look up/down,
// Q/E down/up, Shift = 5x faster, R = back to the start view, Esc = quit.
// Mouse: hold the right button and drag to look; the wheel moves forward/back.
// Gamepad: left stick moves, right stick looks, triggers down/up, A (south button) = faster,
// Start = back to the start view.
// Read-only: it opens the archives for reading and writes nothing.

#include "common/StrictNumber.h"
#include "content/AssetIndex.h"
#include "content/ContentError.h"
#include "content/GroundFile.h"
#include "content/JaArchive.h"
#include "view/FlyCamera.h"
#include "view/RouteCatalog.h"
#include "view/RouteLoader.h"
#include "view/RouteScene.h"

#include <SDL.h>

#include <cstdio>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace trainz::content;
using trainz::common::strictNumber;

int main(int argc, char** argv) {
    const bool byName = argc >= 2 && std::string(argv[1]).rfind("--", 0) == 0;
    if (argc < 3) {
        std::cerr << "usage: route-viewer --install DIR --list\n"
                     "       route-viewer --install DIR --route NAME [--session NAME] [--at X Y] [--radius M]\n"
                     "       route-viewer ARCHIVE.ja ROUTE_FOLDER [--session ARCHIVE.ja SESSION_FOLDER] [--install DIR]\n"
                     "                    [--at X Y] [--radius M]\n"
                     "keys: W/S or Up/Down forward/back, A/D sideways, Left/Right turn, PgUp/PgDn look up/down,\n"
                     "      Q/E down/up, Shift 5x faster, R start view, Esc quit\n"
                     "mouse: right button + drag to look, wheel forward/back; gamepad: sticks, triggers, A faster\n";
        return 2;
    }
    std::string sessionArchive, sessionFolder, installDir, routeQuery, sessionQuery;
    bool list = false;
    std::optional<double> atX, atY;
    double radius = 600;
    long frames = -1;  // -1: until Esc
    for (int i = byName ? 1 : 3; i < argc; ++i) {
        const std::string opt = argv[i];
        if (opt == "--session" && !byName && i + 2 < argc) {
            sessionArchive = argv[++i];
            sessionFolder = argv[++i];
        } else if (opt == "--session" && byName && i + 1 < argc) {
            sessionQuery = argv[++i];
        } else if (opt == "--route" && byName && i + 1 < argc) {
            routeQuery = argv[++i];
        } else if (opt == "--list" && byName) {
            list = true;
        } else if (opt == "--install" && i + 1 < argc) {
            installDir = argv[++i];
        } else if (opt == "--at" && i + 2 < argc) {
            atX = strictNumber(argv[i + 1]), atY = strictNumber(argv[i + 2]);
            i += 2;
            if (!atX || !atY) {
                std::cerr << "--at needs two numbers (world metres)\n";
                return 2;
            }
        } else if (opt == "--frames" && i + 1 < argc) {
            const auto n = strictNumber(argv[++i]);
            if (!n || *n < 1 || *n > 100000 || *n != static_cast<double>(static_cast<long>(*n))) {
                std::cerr << "--frames needs a whole number, 1..100000\n";
                return 2;
            }
            frames = static_cast<long>(*n);
        } else if (opt == "--radius" && i + 1 < argc) {
            const auto r = strictNumber(argv[++i]);
            if (!r || !(*r >= 50 && *r <= 5000)) {
                std::cerr << "--radius needs a number of metres, 50..5000\n";
                return 2;
            }
            radius = *r;
        } else {
            std::cerr << "unexpected argument " << opt << "\n";
            return 2;
        }
    }
    if (byName && installDir.empty()) {
        std::cerr << "--list, --route and --session NAME need --install DIR (the TS2010 folder)\n";
        return 2;
    }
    if (byName && !list && routeQuery.empty() && sessionQuery.empty()) {
        std::cerr << "give --list, --route NAME or --session NAME\n";
        return 2;
    }
    try {
        std::optional<AssetIndex> index;
        std::optional<trainz::view::AssetMeshes> meshes;
        std::optional<trainz::view::GroundTextures> groundTextures;
        if (!installDir.empty()) {
            std::printf("indexing the assets in %s...\n", installDir.c_str());
            index = AssetIndex::scanInstall(installDir);
            meshes.emplace(*index);
            groundTextures.emplace(*index);
        }
        std::string archivePath = byName ? std::string() : std::string(argv[1]);
        std::string routeFolder = byName ? std::string() : std::string(argv[2]);
        if (byName) {
            const auto routes = trainz::view::listRoutes(*index);
            if (list) {
                for (const auto& r : routes) {
                    std::printf("%s  %s\n", r.route.name.c_str(), formatKuid(r.route.kuid).c_str());
                    for (const auto& s : r.sessions) {
                        std::printf("    session: %s  %s\n", s.name.c_str(), formatKuid(s.kuid).c_str());
                    }
                }
                std::printf("%zu routes\n", routes.size());
                return 0;
            }
            std::vector<const trainz::view::CatalogEntry*> routeEntries, sessionEntries;
            std::map<const trainz::view::CatalogEntry*, const trainz::view::CatalogRoute*> routeOfSession;
            for (const auto& r : routes) {
                routeEntries.push_back(&r.route);
                for (const auto& s : r.sessions) {
                    sessionEntries.push_back(&s);
                    routeOfSession[&s] = &r;
                }
            }
            std::vector<std::string> candidates;
            auto complain = [&](const char* what, const std::string& q) {
                std::cerr << "no single " << what << " matches '" << q << "'";
                if (!candidates.empty()) {
                    std::cerr << "; it could be:\n";
                    for (const auto& c : candidates) {
                        std::cerr << "  " << c << "\n";
                    }
                    return 4;
                }
                std::cerr << " (see --list)\n";
                return 3;
            };
            const trainz::view::CatalogRoute* route = nullptr;
            if (!routeQuery.empty()) {
                const auto* e = trainz::view::findEntry(routeEntries, routeQuery, candidates);
                if (e == nullptr) {
                    return complain("route", routeQuery);
                }
                for (const auto& r : routes) {
                    route = &r.route == e ? &r : route;
                }
            }
            if (!sessionQuery.empty()) {
                std::vector<const trainz::view::CatalogEntry*> pool;
                for (const auto* s : sessionEntries) {
                    if (route == nullptr || routeOfSession[s] == route) {
                        pool.push_back(s);  // only the chosen route's sessions
                    }
                }
                const auto* s = trainz::view::findEntry(pool, sessionQuery, candidates);
                if (s == nullptr) {
                    return complain("session", sessionQuery);
                }
                route = routeOfSession[s];
                sessionArchive = s->where.source.string();
                sessionFolder = s->where.folder;
                std::printf("session: %s\n", s->name.c_str());
            }
            archivePath = route->route.where.source.string();
            routeFolder = route->route.where.folder;
            std::printf("route: %s\n", route->route.name.c_str());
        }
        const auto archive = JaArchive::open(archivePath);
        const JaEntry* ge = archive.find(routeFolder + "/mapfile.gnd");
        if (ge == nullptr) {
            std::cerr << "'" << routeFolder << "/mapfile.gnd' is not in " << archivePath << "\n";
            return 1;
        }
        std::printf("reading the route...\n");
        const GroundFile ground = parseGround(archive.readLimited(*ge, std::size_t{1} << 30));
        auto extras = trainz::view::loadRouteExtras(archive, routeFolder, sessionArchive, sessionFolder);
        trainz::view::RouteSceneInput in;
        in.ground = &ground;
        in.track = extras.track ? &*extras.track : nullptr;
        in.objects = &extras.objects;
        in.trains = extras.trains ? &*extras.trains : nullptr;
        in.meshes = meshes ? &*meshes : nullptr;
        in.groundTextures = groundTextures ? &*groundTextures : nullptr;
        std::printf("building the scene (radius %.0f m)...\n", radius);
        const auto view = trainz::view::buildRouteView(in, radius, atX, atY);
        std::printf("%zu triangles, %zu sections, %zu objects as models\n", view.scene.triangles.size(), view.sections,
                    view.objectsAsMeshes);

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0 && SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            return 1;
        }
        constexpr int kW = 960, kH = 540;
        SDL_Window* win = SDL_CreateWindow("route-viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kW, kH,
                                           SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        SDL_Renderer* ren = win != nullptr ? SDL_CreateRenderer(win, -1, 0) : nullptr;
        SDL_Texture* tex = ren != nullptr ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kW, kH)
                                          : nullptr;
        if (tex == nullptr) {
            std::cerr << "could not open a window: " << SDL_GetError() << "\n";
            SDL_Quit();
            return 1;
        }
        const auto start = trainz::view::FlyCamera::lookingAlong(view.camera);
        auto cam = start;
        bool dirty = true, running = true;
        Uint32 last = SDL_GetTicks();
        SDL_GameController* pad = nullptr;  // the first gamepad SDL knows, if any
        auto openPad = [&]() {
            for (int j = 0; pad == nullptr && j < SDL_NumJoysticks(); ++j) {
                if (SDL_IsGameController(j)) {
                    pad = SDL_GameControllerOpen(j);
                }
            }
        };
        openPad();
        while (running) {
            trainz::view::FlyControls ctl;
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
                    running = false;
                } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_r) {
                    cam = start, dirty = true;
                } else if (ev.type == SDL_WINDOWEVENT) {
                    dirty = true;
                } else if (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_RMASK) != 0) {
                    ctl.mouseDx += ev.motion.xrel, ctl.mouseDy += ev.motion.yrel;
                } else if (ev.type == SDL_MOUSEWHEEL) {
                    ctl.wheel += ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
                } else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                    openPad();
                } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED && pad != nullptr &&
                           ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                    SDL_GameControllerClose(pad);
                    pad = nullptr;
                    openPad();  // another one already plugged in takes over
                } else if (ev.type == SDL_CONTROLLERBUTTONDOWN && ev.cbutton.button == SDL_CONTROLLER_BUTTON_START &&
                           pad != nullptr && ev.cbutton.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                    cam = start, dirty = true;  // only the gamepad in use resets the view
                }
            }
            const Uint32 now = SDL_GetTicks();
            const double dt = std::min(0.5, (now - last) / 1000.0);
            last = now;
            const Uint8* k = SDL_GetKeyboardState(nullptr);
            ctl.fast = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
            ctl.forward = (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP] ? 1 : 0) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN] ? 1 : 0);
            ctl.right = (k[SDL_SCANCODE_D] ? 1 : 0) - (k[SDL_SCANCODE_A] ? 1 : 0);
            ctl.up = (k[SDL_SCANCODE_E] ? 1 : 0) - (k[SDL_SCANCODE_Q] ? 1 : 0);
            ctl.turnLeft = (k[SDL_SCANCODE_LEFT] ? 1 : 0) - (k[SDL_SCANCODE_RIGHT] ? 1 : 0);
            ctl.lookUp = (k[SDL_SCANCODE_PAGEUP] ? 1 : 0) - (k[SDL_SCANCODE_PAGEDOWN] ? 1 : 0);
            if (pad != nullptr) {  // sticks add to the keys (applyFlyControls clamps the sums)
                using trainz::view::stickAxis;
                ctl.forward -= stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
                ctl.right += stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
                ctl.turnLeft -= stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
                ctl.lookUp -= stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));
                ctl.up += stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) -
                         stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
                ctl.fast = ctl.fast || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) != 0;
            }
            if (frames > 0) {  // test mode: turn a little every frame
                cam.turn(0.05, 0);
                dirty = true;
            }
            if (trainz::view::applyFlyControls(cam, ctl, dt)) {
                dirty = true;
            }
            if (dirty) {
                const Uint32 t0 = SDL_GetTicks();
                const auto rgba = trainz::view::renderScene(view.scene, cam.camera(), kW, kH);
                SDL_UpdateTexture(tex, nullptr, rgba.data(), kW * 4);
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, nullptr, nullptr);
                SDL_RenderPresent(ren);
                char title[160];
                std::snprintf(title, sizeof title, "route-viewer  x %.0f  y %.0f  z %.0f  (%u ms per frame)", cam.position.x,
                              cam.position.y, cam.position.z, static_cast<unsigned>(SDL_GetTicks() - t0));
                SDL_SetWindowTitle(win, title);
                dirty = false;
                if (frames > 0 && --frames == 0) {
                    std::printf("%s\n", title);
                    running = false;
                }
            } else {
                SDL_Delay(15);
            }
        }
        if (pad != nullptr) {
            SDL_GameControllerClose(pad);
        }
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
