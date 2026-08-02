#include "Image.h"

Image::Image() {
  const ImageStylePreset& p = ui_get_style_presets().image;
  width = p.width;
  height = p.height;
  tint = p.tint;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(1);
}

Image::~Image() = default;

void Image::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  out_h = height;
}

void Image::build_draw_buffer() {
  if (draw_command_buffer_.empty()) {
    draw_command_buffer_.resize(1);
  }
  DrawCommand& cmd = draw_command_buffer_[0];
  cmd.type = DrawCommandType::Image;
  cmd.x = x;
  cmd.y = y;
  cmd.width = width;
  cmd.height = height;
  cmd.color = tint;
  cmd.texture_id = texture_id;
  cmd.layer = layer;
  cmd.u0 = u0;
  cmd.v0 = v0;
  cmd.u1 = u1;
  cmd.v1 = v1;
  cmd.text.clear();
}
