#include "Toggle.h"

#include <algorithm>

namespace {

bool Hit(float px, float py, float x, float y, float w, float h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

Color Lerp(const Color& a, const Color& b, float t) {
  if (t < 0.f) {
    t = 0.f;
  } else if (t > 1.f) {
    t = 1.f;
  }
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
          a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

}  // namespace

Toggle::Toggle() {
  const ToggleStylePreset& p = ui_get_style_presets().toggle;
  width = p.width;
  height = p.height;
  gap = p.gap;
  label_color = p.label_color;
  track_off = p.track_off;
  track_on = p.track_on;
  thumb_color = p.thumb_color;
  transition_duration = p.transition_duration;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(3);
}

Toggle::~Toggle() = default;

void Toggle::bind_data(bool* data) {
  bound_ = data;
  if (bound_) {
    checked = *bound_;
    anim_t_ = checked ? 1.f : 0.f;
  }
}

void Toggle::WriteBound() {
  if (bound_) {
    *bound_ = checked;
  }
}

void Toggle::get_layout_size(float& out_w, float& out_h) const {
  out_w = width + (label.empty() ? 0.f : gap + 100.f);
  out_h = height;
}

void Toggle::handle_messages(const MouseEvents& mouse,
                             const KeyboardEvents&) {
  if (disabled) {
    state = ComponentState::Base;
    return;
  }
  float lw = 0.f;
  float lh = 0.f;
  get_layout_size(lw, lh);
  const bool hovered = Hit(mouse.x, mouse.y, x, y, lw, lh);
  if (mouse.left_pressed && !mouse.click_consumed && hovered) {
    checked = !checked;
    WriteBound();
    enqueue_event(on_click);
    mouse.click_consumed = true;
  }
  state = hovered ? (mouse.left_down ? ComponentState::Active
                                     : ComponentState::Hovered)
                  : ComponentState::Base;
}

void Toggle::update(float dt) {
  Component::update(dt);

  const float target = checked ? 1.f : 0.f;
  if (transition_duration <= 0.f) {
    anim_t_ = target;
    return;
  }

  const float speed = 1.f / transition_duration;
  if (anim_t_ < target) {
    anim_t_ = std::min(target, anim_t_ + speed * dt);
  } else if (anim_t_ > target) {
    anim_t_ = std::max(target, anim_t_ - speed * dt);
  }
}

void Toggle::build_draw_buffer() {
  if (draw_command_buffer_.size() < 3) {
    draw_command_buffer_.resize(3);
  }

  float t = anim_t_;
  if (t < 0.f) {
    t = 0.f;
  } else if (t > 1.f) {
    t = 1.f;
  }

  DrawCommand& track = draw_command_buffer_[0];
  track.type = DrawCommandType::RoundedRect;
  track.x = x;
  track.y = y;
  track.width = width;
  track.height = height;
  track.corner_radius = height * 0.5f;
  track.color = Lerp(track_off, track_on, t);
  track.layer = layer;
  track.text.clear();

  const float thumb = height - 4.f;
  const float x_off = x + 2.f;
  const float x_on = x + width - thumb - 2.f;

  DrawCommand& knob = draw_command_buffer_[1];
  knob.type = DrawCommandType::Circle;
  knob.y = y + 2.f;
  knob.width = thumb;
  knob.height = thumb;
  knob.x = x_off + (x_on - x_off) * t;
  knob.color = thumb_color;
  knob.layer = layer;
  knob.text.clear();

  DrawCommand& label_cmd = draw_command_buffer_[2];
  label_cmd.type = DrawCommandType::Text;
  label_cmd.x = x + width + gap;
  label_cmd.y = y;
  label_cmd.width = 200.f;
  label_cmd.height = height;
  label_cmd.color = label_color;
  label_cmd.layer = layer;
  label_cmd.text = label;
  label_cmd.wrap = false;
  label_cmd.text_align = TextAlign::LeftMiddle;
  apply_font(label_cmd);
}
