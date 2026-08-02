#include "../../simple_ui.h"

namespace {

UiStylePresets g_style_presets{};

}  // namespace

void ui_set_style_presets(const UiStylePresets& presets) {
  g_style_presets = presets;
}

const UiStylePresets& ui_get_style_presets() {
  return g_style_presets;
}
