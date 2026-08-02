#include "ProgressBar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

ProgressBar::ProgressBar() {
  const ProgressBarStylePreset& p = ui_get_style_presets().progress_bar;
  width = p.width;
  height = p.height;
  track_color = p.track_color;
  fill_color = p.fill_color;
  text_color = p.text_color;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(3);
}

ProgressBar::~ProgressBar() = default;

void ProgressBar::bind_data(float* data) {
  bound_ = data;
  if (bound_) {
    value = *bound_;
  }
}

void ProgressBar::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  out_h = height;
}

void ProgressBar::build_draw_buffer() {
  if (bound_) {
    value = *bound_;
  }
  if (draw_command_buffer_.size() < 3) {
    draw_command_buffer_.resize(3);
  }

  float lo = min_value;
  float hi = max_value;
  if (hi < lo) {
    std::swap(lo, hi);
  }
  const float span = std::max(0.0001f, hi - lo);
  const float t = std::clamp((value - lo) / span, 0.f, 1.f);

  DrawCommand& track = draw_command_buffer_[0];
  track.type = DrawCommandType::Rect;
  track.x = x;
  track.y = y;
  track.width = width;
  track.height = height;
  track.color = track_color;
  track.layer = layer;
  track.text.clear();

  DrawCommand& fill = draw_command_buffer_[1];
  fill.type = DrawCommandType::Rect;
  fill.x = x;
  fill.y = y;
  fill.width = width * t;
  fill.height = height;
  fill.color = fill_color;
  fill.layer = layer;
  fill.text.clear();

  DrawCommand& label = draw_command_buffer_[2];
  if (show_percent) {
    wchar_t buf[16];
    std::swprintf(buf, 16, L"%.0f%%", t * 100.f);
    label.type = DrawCommandType::Text;
    label.x = x;
    label.y = y;
    label.width = width;
    label.height = height;
    label.color = text_color;
    label.layer = layer;
    label.text = buf;
    label.wrap = false;
    label.text_align = TextAlign::Center;
    apply_font(label);
  } else {
    label.type = DrawCommandType::Text;
    label.text.clear();
    label.width = 0.f;
    apply_font(label);
  }
}
