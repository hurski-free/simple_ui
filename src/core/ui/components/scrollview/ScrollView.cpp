#include "ScrollView.h"

ScrollView::ScrollView() {
  const ScrollViewStylePreset& p = ui_get_style_presets().scroll_view;
  place_mode = p.place_mode;
  ui_apply_scrollbar_preset(scroll_x, p.scroll_x);
  ui_apply_scrollbar_preset(scroll_y, p.scroll_y);
}

ScrollView::~ScrollView() = default;
