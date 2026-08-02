#include "Label.h"

Label::Label() {
  const LabelStylePreset& p = ui_get_style_presets().label;
  width = p.width;
  height = p.height;
  color = p.color;
  outline = p.outline;
  text_align = p.text_align;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(1);
}

Label::~Label() = default;

void Label::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  out_h = height;
}

void Label::build_draw_buffer() {
  if (draw_command_buffer_.empty()) {
    draw_command_buffer_.resize(1);
  }
  DrawCommand& cmd = draw_command_buffer_[0];
  cmd.type = DrawCommandType::Text;
  cmd.x = x;
  cmd.y = y;
  cmd.width = width;
  cmd.height = height;
  cmd.color = color;
  cmd.outline = outline;
  cmd.layer = layer;
  cmd.text = text;
  cmd.wrap = false;
  cmd.text_align = text_align;
  apply_font(cmd);
}
