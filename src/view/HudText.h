// A tiny built-in 5x7 pixel font for a heads-up line drawn into a rendered picture (speed,
// notch, brake, next signal), so screenshots show what the window title shows. Letters are
// drawn as capitals; characters without a glyph are drawn as a small box.
#pragma once

#include "view/SceneRender.h"

#include <cstdint>
#include <string>
#include <vector>

namespace trainz::view {

// Draws `text` into width x height RGBA at (x, y) (top left), each font pixel `scale` x `scale`
// screen pixels, in `colour` on a darkened band. Clipped to the picture.
void drawHudText(std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t height, int x, int y,
                 const std::string& text, int scale = 2, Rgb colour = {255, 255, 255});

// Width in screen pixels of `text` drawn at `scale`.
int hudTextWidth(const std::string& text, int scale = 2);

}  // namespace trainz::view
