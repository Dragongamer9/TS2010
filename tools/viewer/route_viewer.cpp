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
// throttle-power curves; below notch 0, S steps into its dynamic-brake notches), train brake
// handle A application / Z lap / Q release / Pause or Space emergency, R reverser (when stopped), J switch the next junction in
// front (junctions start as the route stores them), C chase or free camera, Esc quit. The
// title shows the next signal ahead and its aspect (the port's simple block rules). The session's
// other trains stand where they are; with --ai the port's simple AI drives them (it stops for
// signals at stop, trains ahead and line ends). T (or --watch N) makes the chase camera follow
// another train; T again moves on, back to your own. Running into a train below 7 km/h couples
// it to yours (faster is a collision: you stop); U (when stopped) leaves your rearmost car behind.
// Sound: your train's own engine sounds follow the notch, H sounds its horn, B rings its bell
// (TS2010's keys); wheel clacks, points, flange squeal on tight curves and the brake hiss play too;
// --record-sound writes what was played to a .wav next to the screenshots. --mute plays nothing;
// test runs (--frames) are silent unless --sound is given. --train N picks the train (1 = the first; default: the first one that stands on track).
// Physics: mass and notch curves from the install when present; resistance/brakes are presets.
// Exit codes: 0 done, 1 error, 2 bad arguments, 3 no route/session matches NAME, 4 several do.
//
// --frames N: draw N frames (turning a little each time; in drive mode at full throttle) and
// quit, for testing; with SDL_VIDEODRIVER=offscreen no window appears. --png saves the last
// frame under ~/.local/share/trainz-native/routes/. --camera EX EY EZ TX TY TZ sets the start view
// (world metres); --signals lists the signals with their places and aspects. --keys
// "FRAME:KEY,..." presses keys at those frames of a test run (then the run does not force full
// throttle), e.g. --keys "1:w,1:w,40:s,40:a" for screenshots of driving and braking.
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
#include "game/TrainSound.h"
#include "view/FlyCamera.h"
#include "view/HudText.h"
#include "view/RouteCatalog.h"
#include "view/RouteLoader.h"
#include "view/RouteScene.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <future>
#include <chrono>
#include <cstdio>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
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
    bool drive = false, png = false, listSignals = false, aiTrains = false, recordSound = false, mute = false, sound = false;
    long watch = 0;  // --watch N / key T: the chase camera follows other train N (0 = your own)
    std::optional<std::array<double, 6>> startCamera;  // --camera EX EY EZ TX TY TZ
    std::multimap<long, SDL_Keycode> keyScript;        // --keys "FRAME:KEY,...": key presses in a test run
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
        } else if (opt == "--keys" && i + 1 < argc) {
            const std::string keyList = argv[++i];
            std::size_t pos = 0;
            while (pos < keyList.size()) {
                const std::size_t end = std::min(keyList.find(',', pos), keyList.size());
                const std::string item = keyList.substr(pos, end - pos);
                const std::size_t colon = item.find(':');
                const std::string frameText = colon == std::string::npos ? std::string() : item.substr(0, colon);
                const double frame = colon == std::string::npos ? -1.0 : strictNumber(frameText.c_str()).value_or(-1.0);
                const std::string name = colon == std::string::npos ? std::string() : item.substr(colon + 1);
                SDL_Keycode key = SDLK_UNKNOWN;
                if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0])) != 0) {
                    key = static_cast<SDL_Keycode>(std::tolower(static_cast<unsigned char>(name[0])));
                } else if (name == "space") {
                    key = SDLK_SPACE;
                } else if (name == "pause") {
                    key = SDLK_PAUSE;
                }
                if (!(frame >= 1 && frame <= 100000) || frame != std::floor(frame) || key == SDLK_UNKNOWN) {
                    std::cerr << "--keys needs FRAME:KEY items separated by commas (KEY: a letter, a digit, space or pause), not '"
                              << item << "'\n";
                    return 2;
                }
                keyScript.emplace(static_cast<long>(frame), key);
                pos = end + 1;
            }
        } else if (opt == "--watch" && i + 1 < argc) {
            const auto n = strictNumber(argv[++i]);
            if (!n || *n < 0 || *n > 10000 || *n != std::floor(*n)) {
                std::cerr << "--watch needs a whole number (0 = your own train, 1.. = the other trains in order)\n";
                return 2;
            }
            watch = static_cast<long>(*n);
        } else if (opt == "--mute") {
            mute = true;
        } else if (opt == "--sound") {
            sound = true;
        } else if (opt == "--record-sound") {
            recordSound = true;
        } else if (opt == "--ai") {
            aiTrains = true;
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
        double signalClock = 1e9;  // seconds since the aspects were last worked out
        trainz::game::RouteTrain me;            // drive mode: your train
        trainz::game::Drive& driving = me.drive;
        std::string notice;                     // a short message on screen (coupled, collision)
        double noticeClock = 0;
        trainz::game::BrakeHandle brakeHandle;
        trainz::game::TrainSound trainSound;   // drive mode: your train's engine and horn
        std::vector<std::int16_t> recorded;    // --record-sound: everything mixed
        double hornHeldFor = 1e9;              // seconds the horn has sounded this time
        bool hornKey = false;
        bool bellOn = false;
        trainz::game::RunningSounds runningSounds;  // wheel clacks, points, brake hiss
        std::vector<trainz::game::RouteTrain> otherTrains;  // drive mode: the session's other trains
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
            auto mine = trainz::game::makeRouteTrain(*net, trains, chosen, *meshes);
            if (!mine) {
                std::cerr << "none of that train's vehicles has a model the port can read\n";
                return 1;
            }
            me = std::move(*mine);
            // Its sounds: the first car whose config names an enginesound (and its hornsound).
            for (const auto& v : trains[chosen].vehicles) {
                const auto cfg = meshes->config(v.kuid);
                const auto* es = cfg ? cfg->child("enginesound") : nullptr;
                if (es == nullptr) {
                    continue;
                }
                const auto* hs = cfg->child("hornsound");
                trainz::game::loadTrainSounds(trainSound, trainz::view::configKuid(*es),
                                              hs != nullptr ? trainz::view::configKuid(*hs) : std::nullopt,
                                              [&](const trainz::content::Kuid& k, const std::string& name) { return meshes->file(k, name); });
                if (trainSound.hasEngine()) {
                    break;
                }
            }
            // The game's general running sounds (wheels, points, brake hiss) from the install's Data.ja.
            std::optional<trainz::content::JaArchive> dataJa;
            for (const char* name : {"Data.ja", "data.ja"}) {
                const std::filesystem::path p = std::filesystem::path(installDir) / name;
                std::error_code ec;
                if (!dataJa && std::filesystem::is_regular_file(p, ec)) {
                    try {
                        dataJa.emplace(trainz::content::JaArchive::open(p));
                    } catch (const std::exception&) {
                    }
                }
            }
            if (dataJa) {
                trainz::game::loadRunningSounds(trainSound, [&](const std::string& name) -> std::optional<std::vector<std::uint8_t>> {
                    const auto* e = dataJa->find(name);
                    if (e == nullptr) {
                        return std::nullopt;
                    }
                    return dataJa->readLimited(*e, std::size_t{8} << 20);
                });
            }
            std::printf("sounds: engine %s, horn %s, running sounds %s\n", trainSound.hasEngine() ? "from its enginesound" : "none",
                        trainSound.hasHorn() ? "from its hornsound" : "none",
                        trainSound.hasEffect("sounds/track/wheel_clang1.wav") ? "from Data.ja" : "none");
            const auto p = net->point(driving.lead);
            if (!atX && p) {
                atX = p->x, atY = p->y;
            }
            std::printf("driving train %zu of %zu: %zu vehicles, %zu with models, %zu with their own mass, %zu powered by their "
                        "enginespec (%.0f t; resistance and brakes are presets)\n",
                        chosen + 1, trains.size(), me.vehicleCount, me.vehicles.size(), me.withOwnMass, me.powered,
                        driving.spec.massKg / 1000);
            // The session's other trains: drawn where they stand, and with --ai driven by the port's simple driver.
            for (std::size_t t = 0; t < trains.size(); ++t) {
                if (t != chosen) {
                    if (auto other = trainz::game::makeRouteTrain(*net, trains, t, *meshes)) {
                        otherTrains.push_back(std::move(*other));
                    }
                }
            }
            std::printf("%zu other trains on the track%s\n", otherTrains.size(), aiTrains ? ", driven by the port's AI" : ", standing");
            if (const auto near = trainz::game::findContact(*net, me, otherTrains, 3000)) {
                std::printf("nearest train: %.0f m from your %s (train %zu's %s)\n", near->gap, near->atOurFront ? "front" : "rear",
                            otherTrains[near->other].index + 1, near->theirFront ? "front" : "rear");
            }
            // Trains that could not be built (no readable model, not on track) still stand in the way.
            std::vector<trainz::content::Train> unbuilt;
            for (std::size_t t = 0; t < trains.size(); ++t) {
                const bool built = t == chosen || std::any_of(otherTrains.begin(), otherTrains.end(),
                                                              [t](const trainz::game::RouteTrain& o) { return o.index == t; });
                if (!built) {
                    unbuilt.push_back(trains[t]);
                }
            }
            standing = trainz::game::standingTrains(*net, unbuilt, -1);
            if (!unbuilt.empty()) {
                std::printf("trains not built (no readable model, or not on the track):");
                for (std::size_t t = 0; t < trains.size(); ++t) {
                    const bool built = t == chosen || std::any_of(otherTrains.begin(), otherTrains.end(),
                                                                  [t](const trainz::game::RouteTrain& o) { return o.index == t; });
                    if (!built) {
                        std::printf(" %zu", t + 1);
                    }
                }
                std::printf("\n");
            }
        }
        // Signals (both modes, with track): turned to their track, placed on the network, and
        // their lamps drawn for the port's aspects. Without --drive every session train stands.
        std::map<std::string, trainz::content::SignalLook> looks;  // by formatted KUID
        std::map<std::string, trainz::sim::TrackPosition> marks;   // trackmarks and triggers by name (for orders)
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
            // The session's driver orders (--ai): each driver's commands, worked through by runOrders.
            if (drive && aiTrains && extras.trains) {
                marks = trainz::game::routeTrackmarks(*net, extras.sessionObjects);  // the session's own first
                for (const auto& [name, at] : trainz::game::routeTrackmarks(*net, extras.objects)) {
                    marks.emplace(name, at);
                }
                std::map<std::string, std::string> kinds, names;  // by formatted command KUID
                std::size_t given = 0, followed = 0, all = 0;
                for (auto order : trainz::game::driverOrders(extras.rules)) {
                    auto train = std::find_if(otherTrains.begin(), otherTrains.end(), [&](const trainz::game::RouteTrain& t) {
                        const auto& vs = (*extras.trains)[t.index].vehicles;
                        return std::any_of(vs.begin(), vs.end(), [&](const trainz::content::Vehicle& v) { return v.name == order.vehicle; });
                    });
                    if (train == otherTrains.end() || order.commands.empty()) {
                        continue;
                    }
                    for (auto& c : order.commands) {
                        const std::string key = trainz::content::formatKuid(c.command);
                        if (kinds.count(key) == 0) {
                            const auto cfg = meshes->config(c.command);
                            const auto* u = cfg ? cfg->child("username") : nullptr;
                            names[key] = u != nullptr ? u->text : key;
                            kinds[key] = trainz::game::commandKind(u != nullptr ? u->text : std::string());
                        }
                        c.kind = kinds[key];
                        c.name = names[key];
                        ++all;
                        followed += c.kind.empty() ? 0 : 1;
                    }
                    train->orders = std::move(order.commands);
                    ++given;
                }
                std::printf("session orders: %zu trackmarks and triggers, %zu AI trains with orders, %zu of their %zu orders followed\n",
                            marks.size(), given, followed, all);
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
        auto view = trainz::view::buildRouteView(in, radius, atX, atY);
        std::future<trainz::view::RouteView> nextView;  // drive mode: the next scene, built in the background
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
        // Sound: an SDL audio queue when a device opens (none in offscreen test runs is fine).
        SDL_AudioDeviceID audio = 0;
        // Test runs (--frames) stay silent unless --sound; --mute silences every run.
        if (drive && trainSound.hasEngine() && !mute && (frames <= 0 || sound) && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
            SDL_AudioSpec want{}, have{};
            want.freq = trainz::game::TrainSound::kRate;
            want.format = AUDIO_S16SYS;
            want.channels = 1;
            want.samples = 1024;
            audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
            if (audio != 0) {
                SDL_PauseAudioDevice(audio, 0);
            }
        }
        trainz::view::Camera lastCamera = startView;
        const bool testRun = frames > 0;
        auto cam = start;
        bool dirty = true, running = true, chase = drive;
        Uint32 last = SDL_GetTicks();
        trainz::input::GamepadSlot pads;  // the first gamepad SDL knows, if any
        pads.openIfNone();
        std::vector<std::uint8_t> rgba;  // the last frame (saved by --png)
        long frameNumber = 0;
        unsigned slowestFrame = 0;  // ms, a whole loop pass (scene rebuilds included)
        while (running) {
            trainz::view::FlyControls ctl;
            SDL_Event ev;
            ++frameNumber;
            const Uint32 loopStart = SDL_GetTicks();
            for (auto [it, stop] = keyScript.equal_range(frameNumber); it != stop; ++it) {  // --keys
                SDL_Event press{};
                press.type = SDL_KEYDOWN;
                press.key.keysym.sym = it->second;
                SDL_PushEvent(&press);
                press.type = SDL_KEYUP;  // a tap: down, then up
                SDL_PushEvent(&press);
            }
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
                    if (driving.controls.dynamicNotch > 0) {
                        --driving.controls.dynamicNotch;  // back up out of the dynamic brake first
                    } else if (driving.controls.notch < maxN) {
                        ++driving.controls.notch;
                    }
                } else if (drive && !driving.spec.notchCurvesN.empty() && ev.type == SDL_KEYDOWN &&
                           (ev.key.keysym.sym == SDLK_s || ev.key.keysym.sym == SDLK_DOWN) &&
                           (ev.key.repeat == 0)) {
                    const int maxD = static_cast<int>(driving.spec.dynamicBrakeCurvesN.size()) - 1;
                    if (driving.controls.notch > 0) {
                        --driving.controls.notch;
                    } else if (driving.controls.dynamicNotch < maxD) {
                        ++driving.controls.dynamicNotch;  // below idle: the dynamic brake's notches
                    }
                } else if (drive && chase && ev.type == SDL_KEYDOWN &&
                           (ev.key.keysym.sym == SDLK_a || ev.key.keysym.sym == SDLK_z || ev.key.keysym.sym == SDLK_q ||
                            ev.key.keysym.sym == SDLK_SPACE || ev.key.keysym.sym == SDLK_PAUSE)) {
                    // The train brake handle, on TS2010's keys: A application, Z lap, Q release, Pause emergency
                    // (Space as well).
                    using P = trainz::game::BrakeHandle::Position;
                    const auto key = ev.key.keysym.sym;
                    brakeHandle.position = key == SDLK_a ? P::Application : key == SDLK_z ? P::Lap : key == SDLK_q ? P::Release : P::Emergency;
                } else if (drive && driving.spec.notchCurvesN.empty() && ev.type == SDL_KEYDOWN && ev.key.repeat == 0 &&
                           (ev.key.keysym.sym == SDLK_w || ev.key.keysym.sym == SDLK_s)) {
                    // A train without notches: each press moves the smooth throttle by 10% (holding moves it too).
                    driving.controls.throttle =
                        std::clamp(driving.controls.throttle + (ev.key.keysym.sym == SDLK_w ? 0.1 : -0.1), 0.0, 1.0);
                } else if (drive && ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_b && ev.key.repeat == 0) {
                    bellOn = !bellOn;  // TS2010's bell key (keyboard.txt train_bell)
                    trainSound.setBell(bellOn);
                } else if (drive && (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.sym == SDLK_h) {
                    hornKey = ev.type == SDL_KEYDOWN;  // TS2010's horn key (keyboard.txt train_horn)
                    if (hornKey && ev.key.repeat == 0) {
                        hornHeldFor = 0;
                    }
                } else if (drive && ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_u && ev.key.repeat == 0) {
                    if (std::fabs(driving.speed) < 0.1) {  // uncouple the rearmost car (when stopped)
                        if (auto rest = trainz::game::uncoupleRear(*net, me)) {
                            otherTrains.push_back(std::move(*rest));
                            notice = "uncoupled the rear car (" + std::to_string(me.vehicleCount) + " left)";
                        } else {
                            notice = "nothing to uncouple";
                        }
                    } else {
                        notice = "stop to uncouple";
                    }
                    std::printf("%s\n", notice.c_str());
                    noticeClock = 6;
                } else if (drive && ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_t && ev.key.repeat == 0) {
                    watch = otherTrains.empty() ? 0 : (watch + 1) % static_cast<long>(otherTrains.size() + 1);
                    chase = true;
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
                if (frames > 0 && keyScript.empty()) {  // test mode: full throttle / top notch, half a second per frame
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
                }
                const double pipeBefore = me.airBrake.pipe;  // for the brake hiss
                if (frames <= 0 || !keyScript.empty()) {  // the air brake, with the train's own pressures
                    driving.controls.brake = me.airBrake.step(brakeHandle.position, frames > 0 ? 0.5 : dt, me.cars.size());
                }
                // Touching another train: looked for before the step, over the distance it would move,
                // so a fast train cannot jump past the contact. Slow (under 7 km/h) couples; faster is
                // a collision that stops you at the contact.
                const double stepSeconds = frames > 0 ? 0.5 : dt;
                const auto junctionBefore = net->junctionAhead(driving.lead, 200);
                const double speedBefore = driving.speed;
                const double reach = std::fabs(driving.speed) * stepSeconds + 0.6;
                const auto contact = trainz::game::findContact(*net, me, otherTrains, reach, driving.speed >= 0);
                if (contact && ((driving.speed > 0.05 && contact->atOurFront) || (driving.speed < -0.05 && !contact->atOurFront))) {
                    auto& other = otherTrains[contact->other];
                    const double hitKmh = std::fabs(driving.speed) * 3.6;
                    net->advance(driving.lead, (contact->atOurFront ? 1.0 : -1.0) * contact->gap);  // up to the buffers
                    if (std::fabs(driving.speed) < 2.0) {
                        const std::size_t joined = other.vehicleCount;
                        trainz::game::coupleTrains(*net, me, other, *contact);
                        otherTrains.erase(otherTrains.begin() + static_cast<std::ptrdiff_t>(contact->other));
                        if (watch > static_cast<long>(otherTrains.size())) {
                            watch = 0;
                        }
                        notice = "coupled " + std::to_string(joined) + " cars at your " + (contact->atOurFront ? "front" : "rear") +
                                 " at " + std::to_string(static_cast<int>(std::lround(hitKmh))) + " km/h (now " +
                                 std::to_string(me.vehicleCount) + ")";
                    } else {
                        notice = "collision at " + std::to_string(static_cast<int>(std::lround(hitKmh))) + " km/h!";
                        driving.speed = 0;
                    }
                    std::printf("%s\n", notice.c_str());
                    noticeClock = 6;
                } else {
                    driving.step(*net, stepSeconds);
                }
                noticeClock -= frames > 0 ? 0.5 : dt;
                // Keep the world around the train without a pause: past 30% of the radius from the
                // scene's centre the next scene is built in the background (AssetMeshes is locked
                // for it) and swapped in when ready; only past 75% does it wait for it.
                const auto& followed = watch > 0 && watch <= static_cast<long>(otherTrains.size())
                                           ? otherTrains[static_cast<std::size_t>(watch - 1)].drive.lead
                                           : driving.lead;  // the train the camera watches
                if (nextView.valid() && nextView.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    view = nextView.get();
                    std::printf("scene swapped in around %.0f, %.0f (built in the background)\n", view.centreX, view.centreY);
                }
                if (const auto p = net->point(followed)) {
                    const double off = std::hypot(p->x - view.centreX, p->y - view.centreY);
                    if (off > radius * 0.3 && !nextView.valid()) {
                        nextView = std::async(std::launch::async, [inCopy = in, radius, x = p->x, y = p->y] {
                            return trainz::view::buildRouteView(inCopy, radius, x, y);
                        });
                    }
                    if (off > radius * 0.75) {  // the edge is near: wait for it (or build it now)
                        const Uint32 r0 = SDL_GetTicks();
                        view = nextView.valid() ? nextView.get() : trainz::view::buildRouteView(in, radius, p->x, p->y);
                        std::printf("scene rebuilt around the train at %.0f, %.0f (waited %u ms)\n", view.centreX, view.centreY,
                                    static_cast<unsigned>(SDL_GetTicks() - r0));
                    }
                }
                signalClock += frames > 0 ? 0.5 : dt;
                if (trainSound.hasEngine()) {  // the engine follows the notch (or the throttle), the horn sounds 1.5 s at least
                    const double seconds = frames > 0 ? 0.5 : dt;
                    const double moved = 0.5 * (speedBefore + driving.speed) * seconds;
                    const bool overPoints = junctionBefore && std::fabs(moved) >= junctionBefore->distance && speedBefore > 0;
                    // The curve under the train: the heading change over the next 10 m.
                    double curveRadius = std::numeric_limits<double>::infinity();
                    if (const auto here = net->point(driving.lead)) {
                        trainz::sim::TrackPosition ahead = driving.lead;
                        const auto m = net->advance(ahead, driving.speed >= 0 ? 10.0 : -10.0);
                        const auto there = net->point(ahead);
                        const double turn = there ? std::fabs(std::remainder(there->heading - here->heading, 2 * std::numbers::pi)) : 0.0;
                        if (turn > 1e-6 && std::fabs(m.moved) > 5) {
                            curveRadius = std::fabs(m.moved) / turn;
                        }
                    }
                    runningSounds.update(trainSound, driving.speed, moved, seconds, overPoints, me.airBrake.pipe < pipeBefore - 1e-9,
                                         curveRadius);
                    trainSound.setNotch(driving.spec.notchCurvesN.empty() ? static_cast<int>(std::lround(driving.controls.throttle * 8))
                                                                         : driving.controls.notch);
                    hornHeldFor += seconds;
                    trainSound.setHorn(hornKey || hornHeldFor < 1.5);
                    const std::size_t want = static_cast<std::size_t>(seconds * trainz::game::TrainSound::kRate);
                    std::vector<float> mixed(want, 0.0f);
                    trainSound.mix(mixed.data(), mixed.size());
                    std::vector<std::int16_t> pcm(want);
                    for (std::size_t i = 0; i < want; ++i) {
                        pcm[i] = static_cast<std::int16_t>(std::lround(std::clamp(mixed[i], -1.0f, 1.0f) * 32767.0f));
                    }
                    if (audio != 0 && SDL_GetQueuedAudioSize(audio) < static_cast<Uint32>(trainz::game::TrainSound::kRate)) {
                        SDL_QueueAudio(audio, pcm.data(), static_cast<Uint32>(pcm.size() * sizeof(std::int16_t)));
                    }
                    if (recordSound) {
                        recorded.insert(recorded.end(), pcm.begin(), pcm.end());
                    }
                }
                // The other trains: each takes the points along its length; with --ai each is driven.
                std::vector<std::vector<trainz::sim::Occupied>> takenBy(otherTrains.size() + 2);
                takenBy[0] = trainz::game::trainOccupies(*net, me);
                takenBy.back() = standing;  // trains that could not be built
                for (std::size_t t = 0; t < otherTrains.size(); ++t) {
                    takenBy[t + 1] = trainz::game::trainOccupies(*net, otherTrains[t]);
                }
                if (aiTrains) {
                    const trainz::game::AiDriver ai;
                    for (std::size_t t = 0; t < otherTrains.size(); ++t) {
                        if (otherTrains[t].parked) {
                            continue;  // cars you left behind stand
                        }
                        std::vector<trainz::sim::Occupied> others;
                        for (std::size_t o = 0; o < takenBy.size(); ++o) {
                            if (o != t + 1) {
                                others.insert(others.end(), takenBy[o].begin(), takenBy[o].end());
                            }
                        }
                        if (!otherTrains[t].orders.empty()) {  // its session orders first
                            const auto trainFront = [&](const std::string& vehicle) -> std::optional<trainz::sim::TrackPosition> {
                                const auto has = [&](const trainz::content::Train& tr) {
                                    return std::any_of(tr.vehicles.begin(), tr.vehicles.end(),
                                                       [&](const trainz::content::Vehicle& v) { return v.name == vehicle; });
                                };
                                if (has((*extras.trains)[me.index])) {
                                    return trainz::game::trainFrontEnd(*net, me);
                                }
                                for (const auto& o : otherTrains) {
                                    if (has((*extras.trains)[o.index])) {
                                        return trainz::game::trainFrontEnd(*net, o);
                                    }
                                }
                                return std::nullopt;
                            };
                            const auto trainEnd = [&](const std::string& vehicle,
                                                      const trainz::sim::TrackPosition& from) -> std::optional<trainz::sim::TrackPosition> {
                                for (std::size_t o = 0; o < otherTrains.size(); ++o) {
                                    const auto& vs = (*extras.trains)[otherTrains[o].index].vehicles;
                                    if (o == t || otherTrains[o].cars.empty() ||
                                        std::none_of(vs.begin(), vs.end(), [&](const trainz::content::Vehicle& v) { return v.name == vehicle; })) {
                                        continue;
                                    }
                                    // The nearer of its two buffer ends, by the way along the track.
                                    std::optional<trainz::sim::TrackPosition> best;
                                    double bestD = 1e18;
                                    for (const auto& end : {trainz::game::trainFrontEnd(*net, otherTrains[o]), trainz::game::trainRearEnd(*net, otherTrains[o])}) {
                                        if (const auto r = net->route(from, end); r && r->distance < bestD) {
                                            bestD = r->distance, best = end;
                                        }
                                    }
                                    return best;
                                }
                                return std::nullopt;
                            };
                            trainz::game::runOrders(otherTrains[t], *net, marks, trainFront, frames > 0 ? 0.5 : dt, trainEnd);
                        }
                        ai.control(otherTrains[t], *net, signals ? &*signals : nullptr, others);
                        otherTrains[t].drive.step(*net, frames > 0 ? 0.5 : dt);
                    }
                }
                // Couple orders: an AI train touching the train it is to couple to joins it.
                for (std::size_t t = 0; t < otherTrains.size(); ++t) {
                    auto& ai = otherTrains[t];
                    if (ai.coupleWith.empty()) {
                        continue;
                    }
                    const auto touch = trainz::game::findContact(*net, ai, otherTrains, 0.6, ai.drive.speed >= 0);
                    if (!touch || touch->other == t) {
                        continue;
                    }
                    auto& other = otherTrains[touch->other];
                    const auto& vs = (*extras.trains)[other.index].vehicles;
                    if (std::none_of(vs.begin(), vs.end(), [&](const trainz::content::Vehicle& v) { return v.name == ai.coupleWith; })) {
                        continue;
                    }
                    std::printf("AI train %zu coupled to train %zu ('%s')\n", ai.index + 1, other.index + 1, ai.coupleWith.c_str());
                    trainz::game::coupleTrains(*net, ai, other, *touch);
                    trainz::game::finishCouple(ai);
                    otherTrains.erase(otherTrains.begin() + static_cast<std::ptrdiff_t>(touch->other));
                    if (watch > static_cast<long>(otherTrains.size())) {
                        watch = 0;
                    }
                    break;  // indexes changed: the rest next step
                }
                if (signals && signalClock >= 0.5) {  // aspects twice a second
                    std::vector<trainz::sim::Occupied> taken;
                    for (const auto& part : takenBy) {
                        taken.insert(taken.end(), part.begin(), part.end());
                    }
                    signals->update(taken);
                    // A train held at a red signal that only it holds red (it overran into the
                    // block) would wait forever: release it, and say so.
                    for (std::size_t t = 0; t < otherTrains.size(); ++t) {
                        if (!otherTrains[t].heldRedSignal) {
                            continue;
                        }
                        std::vector<trainz::sim::Occupied> others;
                        for (std::size_t o = 0; o < takenBy.size(); ++o) {
                            if (o != t + 1) {
                                others.insert(others.end(), takenBy[o].begin(), takenBy[o].end());
                            }
                        }
                        if (trainz::game::releaseIfOnlySelfHeld(otherTrains[t], *signals, others)) {
                            std::printf("train %zu overran a red signal into its block; released\n", otherTrains[t].index + 1);
                        }
                    }
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
                if (watch > static_cast<long>(otherTrains.size())) {
                    watch = 0;
                }
                const trainz::sim::TrackPosition& watched =
                    watch > 0 ? otherTrains[static_cast<std::size_t>(watch - 1)].drive.lead : driving.lead;
                const trainz::view::Camera camera = drive && chase ? trainz::game::chaseCamera(*net, watched) : cam.camera();
                lastCamera = camera;
                trainz::view::Scene scene = view.scene;
                if (drive) {
                    trainz::game::addRouteTrain(scene, *net, driving.lead, me.vehicles);
                    for (const auto& t : otherTrains) {  // the session's other trains, within the view radius
                        if (const auto p = net->point(t.drive.lead);
                            p && std::hypot(p->x - camera.eye.x, p->y - camera.eye.y) < radius + t.length) {
                            trainz::game::addRouteTrain(scene, *net, t.drive.lead, t.vehicles);
                        }
                    }
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
                        char handle[48];
                        if (driving.controls.dynamicNotch > 0) {
                            std::snprintf(handle, sizeof handle, "dynamic brake %d of %d", driving.controls.dynamicNotch,
                                          static_cast<int>(driving.spec.dynamicBrakeCurvesN.size()) - 1);
                        } else {
                            std::snprintf(handle, sizeof handle, "notch %d of %d", driving.controls.notch, maxN);
                        }
                        std::snprintf(title, sizeof title,
                                      "route-viewer  %.0f km/h  %s  brake %.0f%% (%s, pipe ~%.0f psi)  %s%s%s  (%u ms per frame)",
                                      driving.speed * 3.6, handle, driving.controls.brake * 100, brakeHandle.name(),
                                      trainz::content::approximatePsi(me.airBrake.pipe),
                                      driving.controls.reverser > 0 ? "forward" : "reverse",
                                      driving.atBuffer ? "  (buffer!)" : "", junction, static_cast<unsigned>(SDL_GetTicks() - t0));
                    } else {
                        std::snprintf(title, sizeof title,
                                      "route-viewer  %.0f km/h  throttle %.0f%%  brake %.0f%% (%s, pipe ~%.0f psi)  %s%s%s  (%u ms per frame)",
                                      driving.speed * 3.6, driving.controls.throttle * 100, driving.controls.brake * 100,
                                      brakeHandle.name(), trainz::content::approximatePsi(me.airBrake.pipe),
                                      driving.controls.reverser > 0 ? "forward" : "reverse",
                                      driving.atBuffer ? "  (buffer!)" : "", junction, static_cast<unsigned>(SDL_GetTicks() - t0));
                    }
                } else {
                    std::snprintf(title, sizeof title, "route-viewer  x %.0f  y %.0f  z %.0f  (%u ms per frame)", cam.position.x,
                                  cam.position.y, cam.position.z, static_cast<unsigned>(SDL_GetTicks() - t0));
                }
                if (drive) {  // the same line drawn into the picture, so screenshots show it
                    std::string hud = title;
                    if (hud.rfind("route-viewer  ", 0) == 0) {
                        hud = hud.substr(14);
                    }
                    if (const auto ms = hud.rfind("  ("); ms != std::string::npos && hud.find("ms per frame", ms) != std::string::npos) {
                        hud = hud.substr(0, ms);
                    }
                    const auto split = hud.find("  signal");
                    trainz::view::drawHudText(rgba, kW, kH, 8, 8, hud.substr(0, split), 2);
                    if (split != std::string::npos) {
                        trainz::view::drawHudText(rgba, kW, kH, 8, 30, hud.substr(split + 2), 2, {255, 235, 150});
                    }
                    if (noticeClock > 0 && !notice.empty()) {
                        trainz::view::drawHudText(rgba, kW, kH, 8, kH - 30, notice, 2, {255, 200, 120});
                    }
                    if (watch > 0 && watch <= static_cast<long>(otherTrains.size())) {
                        const auto& w = otherTrains[static_cast<std::size_t>(watch - 1)];
                        char line[200];
                        std::snprintf(line, sizeof line, "watching train %zu (%s): %.0f km/h, brake %.0f%%", w.index + 1,
                                      aiTrains ? "AI" : "standing", std::fabs(w.drive.speed) * 3.6, w.drive.controls.brake * 100);
                        trainz::view::drawHudText(rgba, kW, kH, 8, 52, line, 2, {150, 220, 255});
                        if (!w.orderNote.empty()) {
                            trainz::view::drawHudText(rgba, kW, kH, 8, 74, "order: " + w.orderNote, 2, {150, 220, 255});
                        }
                    }
                }
                SDL_UpdateTexture(tex, nullptr, rgba.data(), kW * 4);
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, nullptr, nullptr);
                SDL_RenderPresent(ren);
                SDL_SetWindowTitle(win, title);
                dirty = false;
                slowestFrame = std::max(slowestFrame, static_cast<unsigned>(SDL_GetTicks() - loopStart));
                if (frames > 0 && --frames == 0) {
                    std::printf("%s\n", title);
                    running = false;
                }
            } else {
                SDL_Delay(15);
            }
        }
        pads.close();
        if (testRun && aiTrains && !otherTrains.empty()) {  // what the AI trains are doing at the end
            std::map<std::string, int> states;
            double furthest = 0;
            for (const auto& t : otherTrains) {
                const auto here = net->point(t.drive.lead);
                const auto origin = trainz::game::trainStart(*net, (*extras.trains)[t.index]);
                const auto from = origin ? net->point(*origin) : std::nullopt;
                if (here && from) {
                    furthest = std::max(furthest, std::hypot(here->x - from->x, here->y - from->y));
                }
                if (std::fabs(t.drive.speed) > 0.1) {
                    ++states["moving"];
                } else {
                    std::vector<trainz::sim::Occupied> none;
                    const auto why = trainz::game::AiDriver{}.stopAhead(t, *net, signals ? &*signals : nullptr, none, 60);
                    ++states[std::string("stopped") + (why ? std::string(" (") + why->why + " ahead)" : std::string())];
                }
            }
            std::printf("AI trains:");
            for (const auto& [k, v] : states) {
                std::printf(" %d %s;", v, k.c_str());
            }
            std::printf(" furthest from its start %.0f m\n", furthest);
            for (const auto& t : otherTrains) {
                if (!t.orders.empty()) {
                    std::printf("  train %zu orders: %zu of %zu done, now: %s\n", t.index + 1, t.orderIndex, t.orders.size(),
                                t.orderNote.c_str());
                }
                if (t.destination) {
                    // Why it stands: the AI's own stop check, with every other train's real place.
                    std::vector<trainz::sim::Occupied> others = trainz::game::trainOccupies(*net, me);
                    for (const auto& o : otherTrains) {
                        if (&o != &t) {
                            const auto pts = trainz::game::trainOccupies(*net, o);
                            others.insert(others.end(), pts.begin(), pts.end());
                        }
                    }
                    others.insert(others.end(), standing.begin(), standing.end());
                    if (const auto why = trainz::game::AiDriver{}.stopAhead(t, *net, signals ? &*signals : nullptr, others)) {
                        std::printf("  train %zu: nearest stop %s in %.0f m%s\n", t.index + 1, why->why, why->distance,
                                    t.heldRedSignal ? " (held at a red signal it reached)" : "");
                    }
                    if (t.heldRedSignal && signals) {  // would that signal clear without this train's own points?
                        std::vector<trainz::sim::Occupied> withoutMe = trainz::game::trainOccupies(*net, me);
                        for (const auto& o : otherTrains) {
                            if (&o != &t) {
                                const auto pts = trainz::game::trainOccupies(*net, o);
                                withoutMe.insert(withoutMe.end(), pts.begin(), pts.end());
                            }
                        }
                        withoutMe.insert(withoutMe.end(), standing.begin(), standing.end());
                        auto probe = *signals;
                        probe.update(withoutMe);
                        std::printf("  train %zu: its held signal is %s now, %s without the train itself\n", t.index + 1,
                                    trainz::sim::aspectName(signals->aspect(*t.heldRedSignal)),
                                    trainz::sim::aspectName(probe.aspect(*t.heldRedSignal)));
                    }
                    const auto r = net->route(t.drive.lead, *t.destination);
                    std::printf("  train %zu -> '%s': %s, %.0f km/h\n", t.index + 1, t.destinationName.c_str(),
                                r ? (r->distance < 30 ? "there" : (std::to_string(static_cast<int>(r->distance)) + " m to go").c_str()) : "passed it",
                                std::fabs(t.drive.speed) * 3.6);
                }
            }
        }
        if (testRun) {
            std::printf("slowest frame: %u ms\n", slowestFrame);
        }
        if (testRun) {  // for reproducing a test frame with --camera
            std::printf("last camera: --camera %.2f %.2f %.2f %.2f %.2f %.2f\n", lastCamera.eye.x, lastCamera.eye.y, lastCamera.eye.z,
                        lastCamera.target.x, lastCamera.target.y, lastCamera.target.z);
        }
        if (audio != 0) {
            SDL_CloseAudioDevice(audio);
        }
        if (recordSound && !recorded.empty()) {  // what was mixed, under ~/.local/share/trainz-native/routes/
            trainz::content::PcmSound out;
            out.channels = 1;
            out.sampleRate = trainz::game::TrainSound::kRate;
            out.samples = std::move(recorded);
            const auto bytes = trainz::content::encodePcmWav(out);
            const auto dir = trainz::common::SafeOutputDir::open(trainz::common::portDataDir() / "routes",
                                                                 trainz::common::protectedDirsFor(archivePath));
            const std::string file = "route-viewer-" + std::to_string(static_cast<long>(::time(nullptr))) + ".wav";
            dir.createFile(file, [&](int fd) {
                std::size_t done = 0;
                while (done < bytes.size()) {
                    const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
                    if (n <= 0) {
                        if (n < 0 && errno == EINTR) {
                            continue;
                        }
                        throw std::runtime_error("cannot write the sound file");
                    }
                    done += static_cast<std::size_t>(n);
                }
            });
            std::printf("wrote %s (%.1f s of sound)\n", (dir.path() / file).c_str(),
                        static_cast<double>(out.samples.size()) / trainz::game::TrainSound::kRate);
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
