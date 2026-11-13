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
// --drive (with a session and --install): drive one of the session's trains on its route,
// drawn with its own models: W/S throttle (or one notch up/down when the train has enginespec
// throttle-power curves), Space brake, R reverser (when stopped), J switch the next junction in
// front (junctions start as the route stores them), C chase or free camera, Esc quit. The
// title shows the next signal ahead and its aspect (the port's simple block rules). --train N picks the train (1 = the first; default: the first one that stands on track).
// Physics: mass and notch curves from the install when present; resistance/brakes are presets.
// Exit codes: 0 done, 1 error, 2 bad arguments, 3 no route/session matches NAME, 4 several do.
//
// --frames N: draw N frames (turning a little each time; in drive mode at full throttle) and
// quit, for testing; with SDL_VIDEODRIVER=offscreen no window appears. --png saves the last
// frame under ~/.local/share/trainz-native/routes/. --camera EX EY EZ TX TY TZ sets the start view
// (world metres); --signals lists the signals with their places and aspects.
// Keys: W/S or Up/Down forward/back, A/D sideways, Left/Right turn, PgUp/PgDn look up/down,
// Q/E down/up, Shift = 5x faster, R = back to the start view, Esc = quit.
// Mouse: hold the right button and drag to look; the wheel moves forward/back.
// Gamepad: left stick moves, right stick looks, triggers down/up, A (south button) = faster,
// Start = back to the start view.
// Read-only: it opens the archives for reading and writes nothing.

#include "common/DataDirs.h"
#include "common/PngWriter.h"
#include "common/SafeOutput.h"
#include "common/StrictNumber.h"
#include "content/AssetIndex.h"
#include "content/ContentError.h"
#include "content/GroundFile.h"
#include "content/JaArchive.h"
#include "input/SdlGamepad.h"
#include "game/Drive.h"
#include "game/RouteDrive.h"
#include "view/FlyCamera.h"
#include "view/RouteCatalog.h"
#include "view/RouteLoader.h"
#include "view/RouteScene.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>
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
    bool drive = false, png = false, listSignals = false;
    std::optional<std::array<double, 6>> startCamera;  // --camera EX EY EZ TX TY TZ
    long trainNumber = 0;  // 0: the first that stands on track
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
        } else if (opt == "--png") {
            png = true;
        } else if (opt == "--drive") {
            drive = true;
        } else if (opt == "--train" && i + 1 < argc) {
            const auto n = strictNumber(argv[++i]);
            if (!n || *n < 1 || *n > 100000 || *n != static_cast<double>(static_cast<long>(*n))) {
                std::cerr << "--train needs a whole number, 1 or more\n";
                return 2;
            }
            trainNumber = static_cast<long>(*n);
        } else if (opt == "--signals") {
            listSignals = true;
        } else if (opt == "--camera" && i + 6 < argc) {
            std::array<double, 6> c{};
            for (auto& v : c) {
                const auto n = strictNumber(argv[++i]);
                if (!n || std::fabs(*n) > 1e8) {
                    std::cerr << "--camera needs six numbers: eye x y z, then target x y z (world metres)\n";
                    return 2;
                }
                v = *n;
            }
            startCamera = c;
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
        // Drive mode: the train, its place on the track and its own vehicle models.
        std::optional<trainz::sim::TrackNetwork> net;
        std::optional<trainz::sim::SignalSystem> signals;  // drive mode: the route's signals (uses *net)
        std::vector<trainz::sim::Occupied> standing;       // drive mode: the other trains, standing still
        double trainLength = 0;
        double signalClock = 1e9;  // seconds since the aspects were last worked out
        trainz::game::Drive driving;
        std::vector<trainz::game::RouteVehicle> vehicles;
        if (drive) {
            if (!extras.trains || !extras.track || !meshes) {
                std::cerr << "--drive needs a session (--session) with its route's track, and --install DIR\n";
                return 2;
            }
            net.emplace(*extras.track);
            const auto& trains = *extras.trains;
            std::optional<trainz::sim::TrackPosition> where;
            std::size_t chosen = 0;
            for (std::size_t t = 0; t < trains.size() && !where; ++t) {
                if (trainNumber == 0 || static_cast<long>(t + 1) == trainNumber) {
                    where = trainz::game::trainStart(*net, trains[t]);
                    chosen = t;
                }
            }
            if (!where) {
                std::cerr << (trainNumber > 0 ? "that train does not stand on track (or there is no such train)"
                                              : "no train of the session stands on track")
                          << " (" << trains.size() << " trains)\n";
                return 1;
            }
            std::vector<trainz::sim::VehicleSpec> specs;
            std::size_t ownMass = 0, powered = 0;
            for (const auto& v : trains[chosen].vehicles) {
                // Its own physics data: the traincar's mass and its enginespec's curves.
                std::optional<double> mass;
                std::optional<trainz::content::EngineSpec> engine;
                if (const auto cfg = meshes->config(v.kuid)) {
                    if (const auto* m = cfg->child("mass"); m != nullptr && (!m->floats.empty() || !m->ints.empty())) {
                        mass = m->floats.empty() ? m->ints[0] : m->floats[0];
                    }
                    if (const auto* es = cfg->child("enginespec")) {
                        if (const auto ek = trainz::view::configKuid(*es)) {
                            if (const auto ecfg = meshes->config(*ek)) {
                                engine = trainz::content::parseEngineSpec(*ecfg);
                            }
                        }
                    }
                }
                ownMass += mass ? 1 : 0;
                specs.push_back(trainz::game::ownVehicleSpec(mass, engine ? &*engine : nullptr));
                powered += specs.back().driven ? 1 : 0;
                // Drawn only with a model the port can read; its mass, brakes and pull count anyway.
                const auto* body = meshes->get(v.kuid);
                if (body == nullptr) {
                    continue;
                }
                const trainz::view::PreparedMesh* bog[2] = {nullptr, nullptr};
                for (int k = 0; k < 2; ++k) {
                    if (const auto b = body->bogeyFor(k == 0 ? "a.bog0" : "a.bog1")) {
                        bog[k] = meshes->get(b->kuid);
                    }
                }
                vehicles.push_back(trainz::game::routeVehicle(*body, bog[0], bog[1]));
            }
            if (vehicles.empty()) {
                std::cerr << "none of that train's vehicles has a model the port can read\n";
                return 1;
            }
            driving.lead = *where;
            if (powered == 0) {  // no enginespec curves read: the first vehicle pulls as the preset locomotive
                const auto keepMass = specs.front().massKg;
                specs.front() = trainz::sim::electricLocomotive();
                specs.front().massKg = keepMass;
            }
            driving.spec = trainz::sim::makeTrain(specs);
            const auto p = net->point(*where);
            if (!atX && p) {
                atX = p->x, atY = p->y;
            }
            std::printf("driving train %zu of %zu: %zu vehicles, %zu with models, %zu with their own mass, %zu powered by their "
                        "enginespec (%.0f t; resistance and brakes are presets)\n",
                        chosen + 1, trains.size(), specs.size(), vehicles.size(), ownMass, powered, driving.spec.massKg / 1000);
            standing = trainz::game::standingTrains(*net, trains, static_cast<long>(chosen));
            for (const auto& v : vehicles) {
                trainLength += v.length + 0.5;
            }
        }
        // Signals (both modes, with track): turned to their track, placed on the network, and
        // their lamps drawn for the port's aspects. Without --drive every session train stands.
        std::map<std::string, trainz::content::SignalLook> looks;  // by formatted KUID
        if (extras.track) {
            if (!net) {
                net.emplace(*extras.track);
            }
            const std::size_t turned = trainz::game::orientSignals(*net, extras.objects);
            signals.emplace(*net, trainz::game::routeSignals(*net, extras.objects));
            if (!drive && extras.trains) {
                standing = trainz::game::standingTrains(*net, *extras.trains, -1);
            }
            if (meshes) {
                for (const auto& sg : signals->signals()) {
                    const auto& o = extras.objects[static_cast<std::size_t>(sg.object)];
                    const std::string key = trainz::content::formatKuid(o.kuid);
                    if (looks.count(key) == 0) {
                        const auto cfg = meshes->config(o.kuid);
                        looks[key] = cfg ? trainz::content::parseSignalLook(*cfg) : trainz::content::SignalLook{};
                    }
                }
            }
            std::printf("%zu signals on the track (%zu turned to it), %zu points taken by standing trains\n",
                        signals->signals().size(), turned, standing.size());
        }
        trainz::view::RouteSceneInput in;
        in.ground = &ground;
        in.track = extras.track ? &*extras.track : nullptr;
        in.objects = &extras.objects;
        in.trains = extras.trains && !drive ? &*extras.trains : nullptr;  // in drive mode the train is drawn each frame
        in.meshes = meshes ? &*meshes : nullptr;
        in.groundTextures = groundTextures ? &*groundTextures : nullptr;
        std::printf("building the scene (radius %.0f m)...\n", radius);
        const auto view = trainz::view::buildRouteView(in, radius, atX, atY);
        std::printf("%zu triangles, %zu sections, %zu objects as models, %zu editor-only objects left out\n",
                    view.scene.triangles.size(), view.sections, view.objectsAsMeshes, view.surveyorOnlySkipped);

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
        trainz::view::Camera startView = view.camera;
        if (startCamera) {
            const auto& c = *startCamera;
            startView.eye = {c[0], c[1], c[2]};
            startView.target = {c[3], c[4], c[5]};
        }
        if (listSignals && signals) {
            signals->update(standing);
            for (std::size_t i = 0; i < signals->signals().size(); ++i) {
                const auto& sg = signals->signals()[i];
                const auto& o = extras.objects[static_cast<std::size_t>(sg.object)];
                const auto at = net->point(sg.at);
                std::printf("signal %zu %s: object at %.1f %.1f %.1f, track %.1f %.1f, %s%s%s\n", i, trainz::content::formatKuid(o.kuid).c_str(),
                            o.worldX(), o.worldY(), static_cast<double>(o.z), at ? at->x : 0.0, at ? at->y : 0.0,
                            trainz::sim::aspectName(signals->aspect(i)), sg.name.empty() ? "" : " - ", sg.name.c_str());
            }
        }
        const auto start = trainz::view::FlyCamera::lookingAlong(startView);
        trainz::view::Camera lastCamera = startView;
        const bool testRun = frames > 0;
        auto cam = start;
        bool dirty = true, running = true, chase = drive;
        Uint32 last = SDL_GetTicks();
        trainz::input::GamepadSlot pads;  // the first gamepad SDL knows, if any
        pads.openIfNone();
        std::vector<std::uint8_t> rgba;  // the last frame (saved by --png)
        while (running) {
            trainz::view::FlyControls ctl;
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
                    running = false;
                } else if (drive && ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_r) {
                    if (std::fabs(driving.speed) < 0.1) {
                        driving.controls.reverser = driving.controls.reverser > 0 ? -1 : 1;
                    }
                } else if (drive && !driving.spec.notchCurvesN.empty() && ev.type == SDL_KEYDOWN &&
                           (ev.key.keysym.sym == SDLK_w || ev.key.keysym.sym == SDLK_UP) &&
                           (ev.key.repeat == 0)) {
                    const int maxN = static_cast<int>(driving.spec.notchCurvesN.size()) - 1;
                    if (driving.controls.notch < maxN) {
                        ++driving.controls.notch;
                    }
                } else if (drive && !driving.spec.notchCurvesN.empty() && ev.type == SDL_KEYDOWN &&
                           (ev.key.keysym.sym == SDLK_s || ev.key.keysym.sym == SDLK_DOWN) &&
                           (ev.key.repeat == 0)) {
                    if (driving.controls.notch > 0) {
                        --driving.controls.notch;
                    }
                } else if (drive && ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_j && ev.key.repeat == 0) {
                    if (const auto j = net->junctionAhead(driving.lead)) {  // the next junction in front
                        net->toggle(*j);
                    }
                } else if (drive && ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_c) {
                    chase = !chase;
                    cam = trainz::view::FlyCamera::lookingAlong(trainz::game::chaseCamera(*net, driving.lead));
                } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_r) {
                    cam = start, dirty = true;
                } else if (ev.type == SDL_WINDOWEVENT) {
                    dirty = true;
                } else if (ev.type == SDL_MOUSEMOTION && (ev.motion.state & SDL_BUTTON_RMASK) != 0) {
                    ctl.mouseDx += ev.motion.xrel, ctl.mouseDy += ev.motion.yrel;
                } else if (ev.type == SDL_MOUSEWHEEL) {
                    ctl.wheel += ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
                } else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                    pads.openIfNone();
                } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                    pads.removed(ev.cdevice.which);  // another one already plugged in takes over
                } else if (ev.type == SDL_CONTROLLERBUTTONDOWN && ev.cbutton.button == SDL_CONTROLLER_BUTTON_START &&
                           pads.isOurs(ev.cbutton.which)) {
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
            if (SDL_GameController* pad = pads.get()) {  // sticks add to the keys (applyFlyControls clamps the sums)
                using trainz::view::stickAxis;
                ctl.forward -= stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
                ctl.right += stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
                ctl.turnLeft -= stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
                ctl.lookUp -= stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));
                ctl.up += stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) -
                         stickAxis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
                ctl.fast = ctl.fast || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) != 0;
            }
            if (drive) {
                if (frames > 0) {  // test mode: full throttle / top notch, half a second per frame
                    driving.controls.brake = 0;
                    if (!driving.spec.notchCurvesN.empty()) {
                        driving.controls.notch = static_cast<int>(driving.spec.notchCurvesN.size()) - 1;
                    } else {
                        driving.controls.throttle = 1;
                    }
                } else if (chase) {
                    if (driving.spec.notchCurvesN.empty()) {  // smooth throttle when no notch curves
                        driving.controls.throttle =
                            std::clamp(driving.controls.throttle + ctl.forward * 0.5 * dt, 0.0, 1.0);
                    }
                    driving.controls.brake = k[SDL_SCANCODE_SPACE] ? 1.0 : 0.0;
                }
                driving.step(*net, frames > 0 ? 0.5 : dt);
                signalClock += frames > 0 ? 0.5 : dt;
                if (signals && signalClock >= 0.5) {  // aspects twice a second
                    auto taken = standing;
                    const auto own = trainz::game::trainOccupies(*net, driving.lead, trainLength);
                    taken.insert(taken.end(), own.begin(), own.end());
                    signals->update(taken);
                    signalClock = 0;
                }
                dirty = true;
                if (!chase && trainz::view::applyFlyControls(cam, ctl, dt)) {
                    dirty = true;
                }
            } else {
                if (frames > 0) {  // test mode: turn a little every frame
                    cam.turn(0.05, 0);
                    dirty = true;
                }
                if (trainz::view::applyFlyControls(cam, ctl, dt)) {
                    dirty = true;
                }
            }
            if (dirty) {
                const Uint32 t0 = SDL_GetTicks();
                const trainz::view::Camera camera = drive && chase ? trainz::game::chaseCamera(*net, driving.lead) : cam.camera();
                lastCamera = camera;
                trainz::view::Scene scene = view.scene;
                if (drive) {
                    trainz::game::addRouteTrain(scene, *net, driving.lead, vehicles);
                }
                if (signals && meshes) {
                    if (!drive && signalClock >= 1e8) {  // standing trains only: once
                        signals->update(standing);
                        signalClock = 0;
                    }
                    for (std::size_t i = 0; i < signals->signals().size(); ++i) {
                        const auto& o = extras.objects[static_cast<std::size_t>(signals->signals()[i].object)];
                        const double dx = o.worldX() - camera.eye.x, dy = o.worldY() - camera.eye.y, dz = o.z - camera.eye.z;
                        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                        const auto* m = dist < radius ? meshes->get(o.kuid) : nullptr;
                        const auto look = looks.find(trainz::content::formatKuid(o.kuid));
                        if (m == nullptr || look == looks.end()) {
                            continue;
                        }
                        const int r = static_cast<int>(std::clamp(80.0 / std::max(dist, 1.0), 1.0, 6.0));
                        for (const auto& lamp :
                             trainz::game::signalLamps(o, *m, look->second, static_cast<int>(signals->aspect(i)))) {
                            scene.dots.push_back({{lamp.x, lamp.y, lamp.z}, {lamp.rgb[0], lamp.rgb[1], lamp.rgb[2]}, r});
                        }
                    }
                }
                rgba = trainz::view::renderScene(scene, camera, kW, kH);
                SDL_UpdateTexture(tex, nullptr, rgba.data(), kW * 4);
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, nullptr, nullptr);
                SDL_RenderPresent(ren);
                char title[400];
                char junction[160] = "";
                if (drive && signals) {
                    if (const auto sg = signals->nextSignal(driving.lead)) {
                        const auto& name = signals->signals()[sg->index].name;
                        std::snprintf(junction, sizeof junction, "  signal%s%s in %.0f m: %s", name.empty() ? "" : " ",
                                      name.c_str(), sg->distance, trainz::sim::aspectName(signals->aspect(sg->index)));
                    }
                }
                if (drive) {
                    if (const auto j = net->junctionAhead(driving.lead)) {
                        const auto sw = net->switches.find(j->vertex);
                        const auto way = sw == net->switches.end() ? j->ways.end() : std::find(j->ways.begin(), j->ways.end(), sw->second);
                        if (way != j->ways.end()) {
                            const std::size_t used = std::strlen(junction);
                            std::snprintf(junction + used, sizeof junction - used, "  junction in %.0f m: way %d of %zu", j->distance,
                                          static_cast<int>(way - j->ways.begin()) + 1, j->ways.size());
                        } else {
                            const std::size_t used = std::strlen(junction);
                            std::snprintf(junction + used, sizeof junction - used, "  junction in %.0f m", j->distance);
                        }
                    }
                }
                if (drive) {
                    if (!driving.spec.notchCurvesN.empty()) {
                        const int maxN = static_cast<int>(driving.spec.notchCurvesN.size()) - 1;
                        std::snprintf(title, sizeof title,
                                      "route-viewer  %.0f km/h  notch %d of %d  brake %s  %s%s%s  (%u ms per frame)",
                                      driving.speed * 3.6, driving.controls.notch, maxN,
                                      driving.controls.brake > 0 ? "on" : "off",
                                      driving.controls.reverser > 0 ? "forward" : "reverse",
                                      driving.atBuffer ? "  (buffer!)" : "", junction, static_cast<unsigned>(SDL_GetTicks() - t0));
                    } else {
                        std::snprintf(title, sizeof title,
                                      "route-viewer  %.0f km/h  throttle %.0f%%  brake %s  %s%s%s  (%u ms per frame)",
                                      driving.speed * 3.6, driving.controls.throttle * 100,
                                      driving.controls.brake > 0 ? "on" : "off",
                                      driving.controls.reverser > 0 ? "forward" : "reverse",
                                      driving.atBuffer ? "  (buffer!)" : "", junction, static_cast<unsigned>(SDL_GetTicks() - t0));
                    }
                } else {
                    std::snprintf(title, sizeof title, "route-viewer  x %.0f  y %.0f  z %.0f  (%u ms per frame)", cam.position.x,
                                  cam.position.y, cam.position.z, static_cast<unsigned>(SDL_GetTicks() - t0));
                }
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
        pads.close();
        if (testRun) {  // for reproducing a test frame with --camera
            std::printf("last camera: --camera %.2f %.2f %.2f %.2f %.2f %.2f\n", lastCamera.eye.x, lastCamera.eye.y, lastCamera.eye.z,
                        lastCamera.target.x, lastCamera.target.y, lastCamera.target.z);
        }
        if (png && !rgba.empty()) {  // the last frame, under ~/.local/share/trainz-native/routes/
            const auto bytes = trainz::common::encodePngRgba(kW, kH, rgba);
            const auto out = trainz::common::SafeOutputDir::open(trainz::common::portDataDir() / "routes",
                                                                 trainz::common::protectedDirsFor(archivePath));
            const std::string file = "route-viewer-" + std::to_string(static_cast<long>(::time(nullptr))) + ".png";
            out.createFile(file, [&](int fd) {
                std::size_t done = 0;
                while (done < bytes.size()) {
                    const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
                    if (n <= 0) {
                        if (n < 0 && errno == EINTR) {
                            continue;
                        }
                        throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
                    }
                    done += static_cast<std::size_t>(n);
                }
            });
            std::printf("wrote %s\n", (out.path() / file).c_str());
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
