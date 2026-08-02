#pragma once

#include "Window.h"
#include "graphics/FontAtlas.h"
#include "graphics/Renderer.h"

#include <memory>
#include <vector>

struct UiContext {
  Window window;
  Renderer renderer;
  // Default UI font (also registered with the renderer).
  FontAtlas font_atlas;
  // Extra atlases created via ui_create_font (owned).
  std::vector<std::unique_ptr<FontAtlas>> fonts;

  UiContext(const wchar_t* title, const ScreenSettings* screenSettings,
            const wchar_t* iconPath)
      : window(title, screenSettings, iconPath) {}
};
