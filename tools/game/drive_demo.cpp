// drive-demo: the own game's first playable world. Everything is made by code: terrain, an oval
// with a siding, a box locomotive and wagons; no game files are read. Physics from src/sim.
// ("drive-demo" is a working name; the game's name is the owner's to pick.)
//
//   drive-demo [--frames N] [--png]
//
// Keys: W/Up more throttle, S/Down less, Space (held) brake, R reverser (when stopped),
// J switch the turnout (main line / siding), C chase / free camera, Esc quit.
// Free camera: the route-viewer keys and mouse (FlyCamera).
// --frames N: drive with full throttle for N frames (half a second each) and quit, printing
// where the train got to; with SDL_VIDEODRIVER=offscreen no window appears. --png also saves
// the last frame under ~/.local/share/trainz-native/game/.

#include "common/DataDirs.h"
#include "common/PngWriter.h"
#include "common/SafeOutput.h"
#include "common/StrictNumber.h"
#include "game/Drive.h"
#include "game/GeneratedWorld.h"
#include "view/FlyCamera.h"

#include <SDL.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

using namespace trainz;

int main(int argc, char** argv) {
    long frames = -1;
    bool png = false;
    for (int i = 1; i < argc; ++i) {
        const std::string opt = argv[i];
        if (opt == "--frames" && i + 1 < argc) {
            const auto n = common::strictNumber(argv[++i]);
            if (!n || *n < 1 || *n > 100000 || *n != static_cast<double>(static_cast<long>(*n))) {
                std::cerr << "--frames needs a whole number, 1..100000\n";
                return 2;
            }
            frames = static_cast<long>(*n);
        } else if (opt == "--png") {
            png = true;
        } else {
            std::cerr << "usage: drive-demo [--frames N] [--png]\n"
                         "keys: W/S throttle, Space brake, R reverser, J turnout, C camera, Esc quit\n";
            return 2;
        }
    }
    try {
        const game::GeneratedWorld world = game::generateWorld(1);
        const sim::TrackNetwork baseNet(world.track);
        sim::TrackNetwork net = baseNet;
        view::Scene fixed;
        game::addTerrain(fixed, world);
        game::addTrack(fixed, world);
        const view::PreparedMesh loco = game::boxLocomotive(), bogie = game::boxBogie();
        const view::PreparedMesh wagonA = game::boxWagon({60, 90, 140}), wagonB = game::boxWagon({150, 130, 60});
        const std::vector<game::ConsistVehicle> consist = {{&loco, 18}, {&wagonA, 14}, {&wagonB, 14}, {&wagonA, 14}};
        game::Drive drive;
        drive.lead = {world.startPiece, 150, +1};
        std::vector<sim::VehicleSpec> specs = {sim::electricLocomotive()};
        for (int k = 0; k < 3; ++k) {
            specs.push_back(sim::freightWagon(60));
        }
        drive.spec = sim::makeTrain(specs);

        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            return 1;
        }
        constexpr int kW = 960, kH = 540;
        SDL_Window* win = SDL_CreateWindow("drive-demo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kW, kH, SDL_WINDOW_SHOWN);
        SDL_Renderer* ren = win != nullptr ? SDL_CreateRenderer(win, -1, 0) : nullptr;
        SDL_Texture* tex = ren != nullptr ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kW, kH) : nullptr;
        if (tex == nullptr) {
            std::cerr << "could not open a window: " << SDL_GetError() << "\n";
            SDL_Quit();
            return 1;
        }
        bool chase = true, running = true, toSiding = false;
        view::FlyCamera free = view::FlyCamera::lookingAlong(game::chaseCamera(net, drive.lead));
        Uint32 last = SDL_GetTicks();
        std::vector<std::uint8_t> rgba;
        while (running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
                    running = false;
                } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_r && std::fabs(drive.speed) < 0.1) {
                    drive.controls.reverser = drive.controls.reverser > 0 ? -1 : 1;
                } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_j) {
                    toSiding = !toSiding;
                    net.switches[world.junctionVertex] = toSiding ? world.sidingPiece : world.startPiece + 1;
                } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_c) {
                    chase = !chase;
                    free = view::FlyCamera::lookingAlong(game::chaseCamera(net, drive.lead));
                }
            }
            const Uint32 now = SDL_GetTicks();
            double dt = std::min(0.25, (now - last) / 1000.0);
            last = now;
            const Uint8* k = SDL_GetKeyboardState(nullptr);
            if (frames > 0) {  // test mode: full throttle, half a second per frame
                drive.controls.throttle = 1, drive.controls.brake = 0, dt = 0.5;
            } else {
                const double up = (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) ? 1 : 0;
                const double down = (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) ? 1 : 0;
                if (chase) {
                    drive.controls.throttle = std::clamp(drive.controls.throttle + (up - down) * 0.5 * dt, 0.0, 1.0);
                }
                drive.controls.brake = k[SDL_SCANCODE_SPACE] ? 1.0 : 0.0;
            }
            drive.step(net, dt);
            view::Camera cam;
            if (chase) {
                cam = game::chaseCamera(net, drive.lead);
            } else {
                view::FlyControls ctl;
                ctl.forward = (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP] ? 1 : 0) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN] ? 1 : 0);
                ctl.right = (k[SDL_SCANCODE_D] ? 1 : 0) - (k[SDL_SCANCODE_A] ? 1 : 0);
                ctl.up = (k[SDL_SCANCODE_E] ? 1 : 0) - (k[SDL_SCANCODE_Q] ? 1 : 0);
                ctl.turnLeft = (k[SDL_SCANCODE_LEFT] ? 1 : 0) - (k[SDL_SCANCODE_RIGHT] ? 1 : 0);
                ctl.lookUp = (k[SDL_SCANCODE_PAGEUP] ? 1 : 0) - (k[SDL_SCANCODE_PAGEDOWN] ? 1 : 0);
                view::applyFlyControls(free, ctl, dt);
                cam = free.camera(55);
            }
            view::Scene scene = fixed;
            game::addConsist(scene, net, drive.lead, consist, bogie);
            rgba = view::renderScene(scene, cam, kW, kH);
            SDL_UpdateTexture(tex, nullptr, rgba.data(), kW * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            SDL_RenderPresent(ren);
            const auto p = net.point(drive.lead);
            char title[200];
            std::snprintf(title, sizeof title, "drive-demo  %.0f km/h  throttle %.0f%%  brake %s  %s  turnout: %s%s",
                          drive.speed * 3.6, drive.controls.throttle * 100, drive.controls.brake > 0 ? "on" : "off",
                          drive.controls.reverser > 0 ? "forward" : "reverse", toSiding ? "siding" : "main",
                          drive.atBuffer ? "  (buffer!)" : "");
            SDL_SetWindowTitle(win, title);
            if (frames > 0 && --frames == 0) {
                std::printf("%s\nposition x %.1f y %.1f, piece %d\n", title, p ? p->x : 0.0, p ? p->y : 0.0, drive.lead.piece);
                running = false;
            }
            if (frames < 0) {
                SDL_Delay(5);
            }
        }
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        if (png && !rgba.empty()) {
            const auto bytes = common::encodePngRgba(kW, kH, rgba);
            const auto out = common::SafeOutputDir::open(common::portDataDir() / "game", {});
            const std::string file = "drive-demo-" + std::to_string(static_cast<long>(::time(nullptr))) + ".png";
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
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
