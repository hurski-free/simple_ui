#include "Text.h"

Text::Text() {
  const TextStylePreset& p = ui_get_style_presets().text;
  width = p.width;
  height = p.height;
  color = p.color;
  outline = p.outline;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(1);
}

Text::~Text() = default;

void Text::build_draw_buffer() {
  if (draw_command_buffer_.empty()) {
    draw_command_buffer_.resize(1);
  }

  DrawCommand& cmd = draw_command_buffer_[0];
  if (text.empty() || width <= 0.f) {
    cmd.type = DrawCommandType::Text;
    cmd.x = x;
    cmd.y = y;
    cmd.width = 0.f;
    cmd.height = 0.f;
    cmd.color = color;
    cmd.outline = outline;
    cmd.layer = layer;
    cmd.text.clear();
    cmd.wrap = true;
    cmd.text_align = TextAlign::LeftTop;
    apply_font(cmd);
    return;
  }

  cmd.type = DrawCommandType::Text;
  cmd.x = x;
  cmd.y = y;
  cmd.width = width;
  cmd.height = 0.f;
  cmd.color = color;
  cmd.outline = outline;
  cmd.layer = layer;
  cmd.text = text;
  cmd.wrap = true;
  cmd.text_align = TextAlign::LeftTop;
  apply_font(cmd);
}

void Text::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  if (height > 0.f) {
    out_h = height;
    return;
  }
  int lines = 1;
  for (wchar_t ch : text) {
    if (ch == L'\n') {
      ++lines;
    }
  }
  out_h = static_cast<float>(lines) * (effective_font_size() * 1.2f);
}
