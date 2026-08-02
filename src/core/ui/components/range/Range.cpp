#include "Range.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

// Fixed prefix slots, then dynamic tick mark+label pairs.
constexpr size_t kLabelSlot = 0;
constexpr size_t kTrackSlot = 1;
constexpr size_t kThumbSlot = 2;
constexpr size_t kValueBgSlot = 3;
constexpr size_t kValueBorderL = 4;
constexpr size_t kValueBorderR = 5;
constexpr size_t kValueBorderT = 6;
constexpr size_t kValueBorderB = 7;
constexpr size_t kValueTextSlot = 8;
constexpr size_t kFixedSlots = 9;

bool Hit(float px, float py, float x, float y, float w, float h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

void ClearCmd(DrawCommand& cmd) {
  cmd.type = DrawCommandType::Rect;
  cmd.x = 0.f;
  cmd.y = 0.f;
  cmd.width = 0.f;
  cmd.height = 0.f;
  cmd.color = {};
  cmd.layer = 0;
  cmd.text.clear();
  cmd.wrap = false;
  cmd.text_align = TextAlign::Center;
}

std::wstring FormatValue(float v) {
  wchar_t buf[32];
  if (std::fabs(v - std::round(v)) < 0.001f) {
    std::swprintf(buf, 32, L"%.0f", v);
  } else {
    std::swprintf(buf, 32, L"%.1f", v);
  }
  return buf;
}

}  // namespace

Range::Range() {
  const RangeStylePreset& p = ui_get_style_presets().range;
  width = p.width;
  height = p.height;
  text_color = p.text_color;
  label_width = p.label_width;
  label_gap = p.label_gap;
  value_box_width = p.value_box_width;
  value_box_gap = p.value_box_gap;
  value_box_background = p.value_box_background;
  value_box_border = p.value_box_border;
  value_text_color = p.value_text_color;
  tick_color = p.tick_color;
  tick_label_color = p.tick_label_color;
  tick_height = p.tick_height;
  tick_label_gap = p.tick_label_gap;
  tick_label_height = p.tick_label_height;
  track_color = p.track_color;
  thumb_color = p.thumb_color;
  thumb_active_color = p.thumb_active_color;
  track_thickness = p.track_thickness;
  thumb_size = p.thumb_size;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(kFixedSlots);
}

Range::~Range() = default;

void Range::bind_data(float* data) {
  bound_ = data;
  if (bound_) {
    value = *bound_;
    SetValue(value);
  }
}

void Range::WriteBound() {
  if (bound_) {
    *bound_ = value;
  }
}

void Range::SetValue(float v) {
  float lo = min_value;
  float hi = max_value;
  if (hi < lo) {
    std::swap(lo, hi);
  }
  v = std::clamp(v, lo, hi);
  if (step > 0.f) {
    v = lo + std::round((v - lo) / step) * step;
    v = std::clamp(v, lo, hi);
  }
  value = v;
}

float Range::TrackX() const {
  return text.empty() ? x : x + label_width + label_gap;
}

float Range::TrackY() const {
  return y;
}

float Range::ValueToTrackX(float v) const {
  float lo = min_value;
  float hi = max_value;
  if (hi < lo) {
    std::swap(lo, hi);
  }
  const float span = std::max(0.0001f, hi - lo);
  const float t = std::clamp((v - lo) / span, 0.f, 1.f);
  const float usable = std::max(1.f, width - thumb_size);
  return TrackX() + t * usable + thumb_size * 0.5f;
}

void Range::EnsureBuffer() {
  const size_t need = kFixedSlots + tick_labels.size() * 2;
  if (draw_command_buffer_.size() < need) {
    draw_command_buffer_.resize(need);
  }
}

void Range::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  if (!text.empty()) {
    out_w += label_width + label_gap;
  }
  if (show_value) {
    out_w += value_box_gap + value_box_width;
  }
  out_h = height;
  if (!tick_labels.empty()) {
    out_h += tick_height + tick_label_gap + tick_label_height;
  }
}

void Range::handle_messages(const MouseEvents& mouse,
                            const KeyboardEvents&) {
  if (disabled) {
    state = ComponentState::Base;
    dragging_ = false;
    return;
  }

  const float track_x = TrackX();
  const float track_y = TrackY();
  const bool hovered =
      Hit(mouse.x, mouse.y, track_x, track_y, width, height);

  auto apply_from_mouse = [&]() {
    float lo = min_value;
    float hi = max_value;
    if (hi < lo) {
      std::swap(lo, hi);
    }
    const float usable = std::max(1.f, width - thumb_size);
    const float t =
        std::clamp((mouse.x - track_x - thumb_size * 0.5f) / usable, 0.f, 1.f);
    SetValue(lo + t * (hi - lo));
    WriteBound();
  };

  if (mouse.left_released) {
    dragging_ = false;
  }

  if (dragging_ && mouse.left_down) {
    apply_from_mouse();
    state = ComponentState::Active;
    return;
  }

  if (mouse.left_pressed && !mouse.click_consumed && hovered) {
    dragging_ = true;
    apply_from_mouse();
    state = ComponentState::Active;
    mouse.click_consumed = true;
    return;
  }

  state = hovered ? ComponentState::Hovered : ComponentState::Base;
}

void Range::build_draw_buffer() {
  EnsureBuffer();

  for (DrawCommand& cmd : draw_command_buffer_) {
    ClearCmd(cmd);
  }

  const float track_x = TrackX();
  const float track_y = TrackY();

  float lo = min_value;
  float hi = max_value;
  if (hi < lo) {
    std::swap(lo, hi);
  }
  const float span = std::max(0.0001f, hi - lo);
  const float t = std::clamp((value - lo) / span, 0.f, 1.f);
  const float usable = std::max(1.f, width - thumb_size);
  const float thumb_x = track_x + t * usable;
  const float track_bar_y = track_y + (height - track_thickness) * 0.5f;
  const float thumb_y = track_y + (height - thumb_size) * 0.5f;

  DrawCommand& label = draw_command_buffer_[kLabelSlot];
  if (!text.empty()) {
    label.type = DrawCommandType::Text;
    label.x = x;
    label.y = track_y;
    label.width = label_width;
    label.height = height;
    label.color = text_color;
    label.layer = layer;
    label.text = text;
    label.wrap = false;
    label.text_align = TextAlign::LeftMiddle;
    apply_font(label);
  }

  DrawCommand& track = draw_command_buffer_[kTrackSlot];
  track.type = DrawCommandType::Rect;
  track.x = track_x;
  track.y = track_bar_y;
  track.width = width;
  track.height = track_thickness;
  track.color = track_color;
  track.layer = layer;

  DrawCommand& thumb = draw_command_buffer_[kThumbSlot];
  thumb.type = DrawCommandType::Rect;
  thumb.x = thumb_x;
  thumb.y = thumb_y;
  thumb.width = thumb_size;
  thumb.height = thumb_size;
  thumb.color = (dragging_ || state == ComponentState::Active)
                    ? thumb_active_color
                    : thumb_color;
  thumb.layer = layer;

  if (show_value) {
    const float box_x = track_x + width + value_box_gap;
    const float box_y = track_y;
    const float box_h = height;
    const float bt = 1.5f;

    DrawCommand& bg = draw_command_buffer_[kValueBgSlot];
    bg.type = DrawCommandType::Rect;
    bg.x = box_x;
    bg.y = box_y;
    bg.width = value_box_width;
    bg.height = box_h;
    bg.color = value_box_background;
    bg.layer = layer;

    auto write_border = [&](size_t idx, float sx, float sy, float sw, float sh) {
      DrawCommand& c = draw_command_buffer_[idx];
      c.type = DrawCommandType::Rect;
      c.x = sx;
      c.y = sy;
      c.width = sw;
      c.height = sh;
      c.color = value_box_border;
      c.layer = layer;
    };
    write_border(kValueBorderL, box_x, box_y, bt, box_h);
    write_border(kValueBorderR, box_x + value_box_width - bt, box_y, bt, box_h);
    write_border(kValueBorderT, box_x, box_y, value_box_width, bt);
    write_border(kValueBorderB, box_x, box_y + box_h - bt, value_box_width, bt);

    DrawCommand& val = draw_command_buffer_[kValueTextSlot];
    val.type = DrawCommandType::Text;
    val.x = box_x;
    val.y = box_y;
    val.width = value_box_width;
    val.height = box_h;
    val.color = value_text_color;
    val.layer = layer;
    val.text = FormatValue(value);
    val.wrap = false;
    val.text_align = TextAlign::Center;
    apply_font(val);
  }

  const float ticks_y = track_y + height;
  for (size_t i = 0; i < tick_labels.size(); ++i) {
    const RangeTickLabel& tick = tick_labels[i];
    const float cx = ValueToTrackX(tick.value);
    const size_t mark_slot = kFixedSlots + i * 2;
    const size_t text_slot = mark_slot + 1;

    DrawCommand& mark = draw_command_buffer_[mark_slot];
    mark.type = DrawCommandType::Rect;
    mark.x = cx - 1.f;
    mark.y = ticks_y;
    mark.width = 2.f;
    mark.height = tick_height;
    mark.color = tick_color;
    mark.layer = layer;

    DrawCommand& tick_text = draw_command_buffer_[text_slot];
    tick_text.type = DrawCommandType::Text;
    const float label_w = 48.f;
    tick_text.x = cx - label_w * 0.5f;
    tick_text.y = ticks_y + tick_height + tick_label_gap;
    tick_text.width = label_w;
    tick_text.height = tick_label_height;
    tick_text.color = tick_label_color;
    tick_text.layer = layer;
    tick_text.text = tick.text;
    tick_text.wrap = false;
    tick_text.text_align = TextAlign::Center;
    apply_font(tick_text);
  }
}

void Range::collect_draw(std::vector<DrawCommand*>& out, bool force_rebuild) {
  if (force_rebuild || !static_draw) {
    build_draw_buffer();
    reset_draw_origin_after_build();
  }
  emit_draw_buffer(out);
}
