#include "Button.h"

namespace {

constexpr size_t kTextSlot = 5;
constexpr size_t kBufCount = 6;

}  // namespace

Button::Button() {
  const ButtonStylePreset& p = ui_get_style_presets().button;
  width = p.width;
  height = p.height;
  text_color = p.text_color;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(kBufCount);
}

Button::~Button() = default;

void Button::handle_messages(const MouseEvents& mouse,
                             const KeyboardEvents&) {
  if (disabled) {
    state = ComponentState::Base;
    press_started_here_ = false;
    return;
  }

  const bool hovered = mouse.x >= x && mouse.x <= x + width &&
                       mouse.y >= y && mouse.y <= y + height;

  if (mouse.left_pressed && hovered) {
    press_started_here_ = true;
  }

  if (mouse.left_released) {
    if (press_started_here_ && hovered) {
      enqueue_event(on_click);
    }
    press_started_here_ = false;
  }

  if (press_started_here_ && mouse.left_down) {
    state = ComponentState::Active;
  } else if (hovered) {
    state = ComponentState::Hovered;
  } else {
    state = ComponentState::Base;
  }
}

void Button::build_draw_buffer() {
  if (draw_command_buffer_.size() < kBufCount) {
    draw_command_buffer_.resize(kBufCount);
  }

  write_box_commands(0, width, height);

  DrawCommand& text_cmd = draw_command_buffer_[kTextSlot];
  if (text.empty()) {
    text_cmd.type = DrawCommandType::Text;
    text_cmd.x = x;
    text_cmd.y = y;
    text_cmd.width = 0.f;
    text_cmd.height = 0.f;
    text_cmd.color = text_color;
    text_cmd.layer = layer;
    text_cmd.text.clear();
    text_cmd.wrap = false;
    text_cmd.text_align = TextAlign::Center;
    apply_font(text_cmd);
    return;
  }

  text_cmd.type = DrawCommandType::Text;
  text_cmd.x = x;
  text_cmd.y = y;
  text_cmd.width = width;
  text_cmd.height = height;
  text_cmd.color = text_color;
  text_cmd.layer = layer;
  text_cmd.text = text;
  text_cmd.wrap = false;
  text_cmd.text_align = TextAlign::Center;
  apply_font(text_cmd);
}

void Button::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  out_h = height;
}
