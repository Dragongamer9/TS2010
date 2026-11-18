// A signal asset's own aspects (kind "mosignal"): its `signals` container maps a state
// number to the lights it shows ("light 2" or several, "light 1 3"); `lights` names each
// light's corona image. Lights stand on the model's "a.light<N>" attachment points.
// OBSERVED on the disc's signal configs (e.g. <kuid:-12:214>: 1 -> light 2 red, 4 -> light 1
// yellow, 8 -> light 0 green), matching the Trainz wiki's KIND MOSignal page (a later page:
// state 0 absolute stop, 1 permissive stop, 4 caution, 8 clear; docs/research/public-formats.md).
#pragma once

#include "content/AssetConfig.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace trainz::content {

struct SignalLook {
    std::map<int, std::vector<int>> states;  // state number -> lit light numbers (-1 = none lit)
    std::map<int, std::string> coronas;      // light number -> corona image name
};

SignalLook parseSignalLook(const ConfigNode& root);

// Which state the asset shows for the port's three aspects (0 stop, 1 caution, 2 clear): the
// lowest listed state in 0-3, 4-7 or 8 and up; caution falls back to stop and clear to
// caution when the asset has none in its range. nullopt if it lists no states at all.
// The port's mapping, not the game's.
std::optional<int> stateForAspect(const SignalLook& look, int aspect);

// A light's colour from its corona name ("corona_red.tga" -> red): red, yellow/amber/orange,
// green, blue, lunar/white; white otherwise. A heuristic on the file name, not the image.
std::array<std::uint8_t, 3> coronaColour(const std::string& name);

}  // namespace trainz::content
