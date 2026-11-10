// model-viewer: show a Trainz mesh with its textures in a window (OpenGL 2.1 + SDL2).
//
//   model-viewer ARCHIVE.ja MESH_NAME [--kin ANIM_NAME]
//
//   mouse drag   turn the model        mouse wheel   zoom
//   space        play / pause the animation (with --kin)
//   W            wireframe on / off    Esc or Q      quit
//
// Everything is read-only: the archive is only opened for reading, and the
// viewer writes no files. Textures named by the materials (type 1, colour map)
// are looked up in the mesh's own folder of the same archive.
//
// Uses the oldest OpenGL that does the job (2.1, client-side vertex arrays,
// fixed-function lighting) so it runs on Mesa with any GPU of the last 15 years.
#include "content/AnimationFile.h"
#include "content/ContentError.h"
#include "content/JaArchive.h"
#include "content/MeshFile.h"
#include "content/TextureFile.h"
#include "view/MeshPreview.h"

#include <SDL.h>
#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace trainz::content;

namespace {

constexpr std::size_t kMaxFile = std::size_t{256} << 20;

struct GpuPart {
    std::vector<float> positions;  // x, y, z
    std::vector<float> normals;
    std::vector<float> texcoords;  // u, v
    std::vector<std::uint16_t> indices;
    GLuint texture = 0;
    bool alpha = false;
};

// Uploads every stored mip level, decoded to RGBA. DirectX-style texture
// coordinates (v = 0 at the top) match the first stored row being t = 0.
GLuint uploadTexture(const TextureFile& tex, bool& hasAlpha) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    for (std::size_t level = 0; level < tex.mips.size(); ++level) {
        const auto rgba = decodeToRgba8(tex.mips[level], tex.info.format);
        glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_RGBA, static_cast<GLsizei>(tex.mips[level].width),
                     static_cast<GLsizei>(tex.mips[level].height), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(tex.mips.size()) - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    hasAlpha = tex.info.format == PixelFormat::ARGB8888 || tex.info.format == PixelFormat::DXT1 ||
               tex.info.format == PixelFormat::DXT3 || tex.info.format == PixelFormat::DXT5;
    return id;
}

void fillArrays(const MeshFile& mesh, std::vector<GpuPart>& parts) {
    for (std::size_t i = 0; i < mesh.parts.size() && i < parts.size(); ++i) {
        const auto& g = mesh.parts[i].geometry;
        auto& p = parts[i];
        p.positions.clear();
        p.normals.clear();
        p.texcoords.clear();
        for (std::size_t v = 0; v < g.vertices.size(); ++v) {
            const auto& vx = g.vertices[v];
            p.positions.insert(p.positions.end(), {vx.x, vx.y, vx.z});
            p.texcoords.insert(p.texcoords.end(), {vx.u, vx.v});
            const auto& n = v < g.vertexNormals.size() ? g.vertexNormals[v] : std::array<float, 3>{0, 0, 1};
            p.normals.insert(p.normals.end(), {n[0], n[1], n[2]});
        }
        p.indices = g.indices;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 5) {
        std::cerr << "usage: model-viewer ARCHIVE.ja MESH_NAME [--kin ANIM_NAME]\n";
        return 2;
    }
    std::optional<JaArchive> archive;
    MeshFile original;
    std::optional<AnimationFile> anim;
    std::map<std::string, TextureFile> textures;
    std::string title;
    try {
        archive.emplace(JaArchive::open(argv[1]));
        const JaEntry* e = archive->find(argv[2]);
        if (e == nullptr) {
            std::cerr << "'" << argv[2] << "' is not in " << argv[1] << "\n";
            return 1;
        }
        original = parseMeshOrRepair(archive->readLimited(*e, kMaxFile));
        title = "model-viewer - " + e->name;
        const std::string folder = e->name.find('/') == std::string::npos ? "" : e->name.substr(0, e->name.rfind('/') + 1);
        for (const auto& part : original.parts) {
            for (const auto& t : part.material.textures) {
                if (t.type != 1 || t.fileName.empty() || textures.count(t.fileName)) {
                    continue;
                }
                if (const JaEntry* te = archive->find(folder + t.fileName)) {
                    try {
                        textures.emplace(t.fileName, parseTexture(archive->readLimited(*te, kMaxFile)));
                    } catch (const ContentError& err) {
                        std::cerr << "texture " << t.fileName << ": " << err.what() << "\n";
                    }
                } else {
                    std::cerr << "texture " << t.fileName << " is not in this folder of the archive\n";
                }
            }
        }
        if (argc == 5) {
            if (std::string(argv[3]) != "--kin") {
                std::cerr << "usage: model-viewer ARCHIVE.ja MESH_NAME [--kin ANIM_NAME]\n";
                return 2;
            }
            const JaEntry* ke = archive->find(argv[4]);
            if (ke == nullptr) {
                std::cerr << "'" << argv[4] << "' is not in " << argv[1] << "\n";
                return 1;
            }
            anim = parseAnimation(archive->readLimited(*ke, kMaxFile));
        }
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }

    // Size of the model, for the camera.
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& part : original.parts) {
        for (const auto& v : part.geometry.vertices) {
            const float c[3] = {v.x, v.y, v.z};
            for (int k = 0; k < 3; ++k) {
                if (std::isfinite(c[k])) {
                    lo[k] = std::min(lo[k], c[k]);
                    hi[k] = std::max(hi[k], c[k]);
                }
            }
        }
    }
    float centre[3], radius = 1;
    for (int k = 0; k < 3; ++k) {
        centre[k] = lo[k] <= hi[k] ? (lo[k] + hi[k]) / 2 : 0;
        if (lo[k] <= hi[k]) {
            radius = std::max(radius, (hi[k] - lo[k]) / 2);
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::cerr << "cannot open a window: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        std::cerr << "cannot create an OpenGL 2.1 context: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    std::map<std::string, std::pair<GLuint, bool>> gpuTextures;
    for (const auto& [name, tex] : textures) {
        bool alpha = false;
        try {
            const GLuint id = uploadTexture(tex, alpha);
            gpuTextures.emplace(name, std::make_pair(id, alpha));
        } catch (const ContentError& err) {
            std::cerr << "texture " << name << ": " << err.what() << "\n";
        }
    }
    std::vector<GpuPart> parts(original.parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        for (const auto& t : original.parts[i].material.textures) {
            if (t.type == 1) {
                if (auto it = gpuTextures.find(t.fileName); it != gpuTextures.end()) {
                    parts[i].texture = it->second.first;
                    parts[i].alpha = it->second.second;
                }
                break;
            }
        }
    }
    fillArrays(original, parts);

    float yaw = 30, pitch = 20, distance = radius * 3.0f;
    bool dragging = false, playing = anim.has_value(), wireframe = false, running = true;
    std::size_t lastFrame = static_cast<std::size_t>(-1);
    const Uint32 startTicks = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                const SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE || k == SDLK_q) {
                    running = false;
                } else if (k == SDLK_SPACE) {
                    playing = !playing;
                } else if (k == SDLK_w) {
                    wireframe = !wireframe;
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                dragging = true;
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                dragging = false;
            } else if (ev.type == SDL_MOUSEMOTION && dragging) {
                yaw += static_cast<float>(ev.motion.xrel) * 0.4f;
                pitch = std::clamp(pitch + static_cast<float>(ev.motion.yrel) * 0.4f, -89.0f, 89.0f);
            } else if (ev.type == SDL_MOUSEWHEEL) {
                distance = std::clamp(distance * (ev.wheel.y > 0 ? 0.9f : 1.1f), radius * 0.2f, radius * 50.0f);
            }
        }

        // Animation: pose a fresh copy of the mesh for the current frame (poseMesh works in place).
        if (anim && playing && !anim->frames.empty()) {
            const double seconds = (SDL_GetTicks() - startTicks) / 1000.0;
            const double fps = anim->framesPerSecond ? anim->framesPerSecond : 30;
            const auto frame = static_cast<std::size_t>(seconds * fps) % anim->frames.size();
            if (frame != lastFrame) {
                MeshFile posed = original;
                try {
                    trainz::view::poseMesh(posed, *anim, frame);
                    fillArrays(posed, parts);
                } catch (const std::exception& err) {
                    std::cerr << "animation: " << err.what() << "\n";
                    playing = false;
                }
                lastFrame = frame;
            }
        }

        int w = 1, h = 1;
        SDL_GL_GetDrawableSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.16f, 0.17f, 0.2f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        const double aspect = static_cast<double>(w) / std::max(1, h);
        const double nearZ = std::max(0.01, static_cast<double>(radius) * 0.01);
        const double farZ = static_cast<double>(radius) * 200.0;
        const double top = nearZ * std::tan(0.5 * 45.0 * 3.14159265358979 / 180.0);
        glFrustum(-top * aspect, top * aspect, -top, top, nearZ, farZ);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        const GLfloat lightDir[4] = {0.4f, 0.6f, 1.0f, 0.0f};
        glLightfv(GL_LIGHT0, GL_POSITION, lightDir);
        glTranslatef(0, 0, -distance);
        glRotatef(pitch, 1, 0, 0);
        glRotatef(yaw, 0, 1, 0);
        glRotatef(-90, 1, 0, 0);  // Jet is Z-up
        glTranslatef(-centre[0], -centre[1], -centre[2]);

        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        const GLfloat ambient[4] = {0.35f, 0.35f, 0.35f, 1};
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
        glEnable(GL_NORMALIZE);
        glEnable(GL_COLOR_MATERIAL);
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        for (const auto& p : parts) {
            if (p.indices.empty()) {
                continue;
            }
            if (p.texture) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, p.texture);
                glColor3f(1, 1, 1);
            } else {
                glDisable(GL_TEXTURE_2D);
                glColor3f(0.75f, 0.75f, 0.72f);
            }
            if (p.alpha) {
                glEnable(GL_ALPHA_TEST);
                glAlphaFunc(GL_GREATER, 0.5f);
            } else {
                glDisable(GL_ALPHA_TEST);
            }
            glVertexPointer(3, GL_FLOAT, 0, p.positions.data());
            glNormalPointer(GL_FLOAT, 0, p.normals.data());
            glTexCoordPointer(2, GL_FLOAT, 0, p.texcoords.data());
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(p.indices.size()), GL_UNSIGNED_SHORT, p.indices.data());
        }
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);

        SDL_GL_SwapWindow(window);
    }

    for (const auto& [name, t] : gpuTextures) {
        glDeleteTextures(1, &t.first);
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
