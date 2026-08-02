#include "Input.h"

#include <algorithm>

namespace {

constexpr size_t kTextSlot = 5;
constexpr size_t kCaretSlot = 6;
constexpr size_t kBufCount = 7;
constexpr float kFallbackGlyph = 8.5f;

bool Hit(float px, float py, float x, float y, float w, float h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

}  // namespace

Input::Input() {
  const InputStylePreset& p = ui_get_style_presets().input;
  width = p.width;
  height = p.height;
  padding = p.padding;
  text_color = p.text_color;
  placeholder_color = p.placeholder_color;
  caret_color = p.caret_color;
  caret_blink_period = p.caret_blink_period;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(kBufCount);
}

Input::~Input() = default;

void Input::bind_data(std::wstring* data) {
  bound_ = data;
  if (bound_) {
    text = *bound_;
    caret_ = text.size();
  }
}

void Input::WriteBound() {
  if (bound_) {
    *bound_ = text;
  }
}

float Input::MeasureTextWidth(const std::wstring& s) const {
  if (ui) {
    return ui_measure_text(ui, s.c_str(), effective_font_size(), font);
  }
  return static_cast<float>(s.size()) * kFallbackGlyph;
}

size_t Input::CaretIndexAtX(float local_x) const {
  if (ui) {
    return ui_text_index_at_x(ui, text.c_str(), local_x, effective_font_size(),
                              font);
  }
  if (local_x <= 0.f) {
    return 0;
  }
  const size_t idx = static_cast<size_t>(local_x / kFallbackGlyph + 0.5f);
  return idx > text.size() ? text.size() : idx;
}

void Input::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  out_h = height;
}

void Input::update(float dt) {
  Component::update(dt);
  if (!focused) {
    caret_visible_ = false;
    return;
  }
  caret_blink_ += dt;
  const float period =
      caret_blink_period > 0.f ? caret_blink_period : 0.5f;
  if (caret_blink_ >= period) {
    caret_blink_ -= period;
    caret_visible_ = !caret_visible_;
  }
}

void Input::handle_messages(const MouseEvents& mouse,
                            const KeyboardEvents& keyboard) {
  if (disabled) {
    state = ComponentState::Base;
    focused = false;
    return;
  }

  const bool hovered = Hit(mouse.x, mouse.y, x, y, width, height);

  if (mouse.left_pressed) {
    focused = hovered;
    if (focused) {
      caret_blink_ = 0.f;
      caret_visible_ = true;
      const float local = mouse.x - x - padding;
      caret_ = CaretIndexAtX(local);
    }
  }

  if (focused) {
    state = ComponentState::Active;
  } else if (hovered) {
    state = ComponentState::Hovered;
  } else {
    state = ComponentState::Base;
  }

  if (!focused) {
    return;
  }

  bool changed = false;

  if (keyboard.pressed[VK_BACK] && caret_ > 0) {
    text.erase(caret_ - 1, 1);
    --caret_;
    changed = true;
  }
  if (keyboard.pressed[VK_DELETE] && caret_ < text.size()) {
    text.erase(caret_, 1);
    changed = true;
  }
  if (keyboard.pressed[VK_LEFT] && caret_ > 0) {
    --caret_;
    caret_blink_ = 0.f;
    caret_visible_ = true;
  }
  if (keyboard.pressed[VK_RIGHT] && caret_ < text.size()) {
    ++caret_;
    caret_blink_ = 0.f;
    caret_visible_ = true;
  }
  if (keyboard.pressed[VK_HOME]) {
    caret_ = 0;
    caret_blink_ = 0.f;
    caret_visible_ = true;
  }
  if (keyboard.pressed[VK_END]) {
    caret_ = text.size();
    caret_blink_ = 0.f;
    caret_visible_ = true;
  }

  for (int i = 0; i < keyboard.char_count; ++i) {
    const wchar_t ch = keyboard.chars[i];
    if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
      continue;
    }
    text.insert(caret_, 1, ch);
    ++caret_;
    changed = true;
  }

  if (changed) {
    caret_blink_ = 0.f;
    caret_visible_ = true;
    WriteBound();
  }
}

void Input::build_draw_buffer() {
  if (draw_command_buffer_.size() < kBufCount) {
    draw_command_buffer_.resize(kBufCount);
  }

  write_box_commands(0, width, height);

  DrawCommand& text_cmd = draw_command_buffer_[kTextSlot];
  text_cmd.type = DrawCommandType::Text;
  text_cmd.x = x + padding;
  text_cmd.y = y;
  text_cmd.width = std::max(1.f, width - padding * 2.f);
  text_cmd.height = height;
  text_cmd.layer = layer;
  text_cmd.wrap = false;
  text_cmd.text_align = TextAlign::LeftMiddle;
  if (text.empty() && !focused && !placeholder.empty()) {
    text_cmd.text = placeholder;
    text_cmd.color = placeholder_color;
  } else {
    text_cmd.text = text;
    text_cmd.color = text_color;
  }
  apply_font(text_cmd);

  DrawCommand& caret = draw_command_buffer_[kCaretSlot];
  if (focused && caret_visible_) {
    const std::wstring before = text.substr(0, caret_);
    const float caret_x = x + padding + MeasureTextWidth(before);
    const float fs = effective_font_size();
    caret.type = DrawCommandType::Rect;
    caret.x = caret_x;
    caret.y = y + (height - fs * 0.85f) * 0.5f;
    caret.width = 2.f;
    caret.height = fs * 0.85f;
    caret.color = caret_color;
    caret.layer = layer;
    caret.text.clear();
    caret.wrap = false;
  } else {
    caret.type = DrawCommandType::Rect;
    caret.width = 0.f;
    caret.height = 0.f;
    caret.color.a = 0.f;
  }
}
