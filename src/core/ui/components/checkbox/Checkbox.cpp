#include "Checkbox.h"

#include <algorithm>

namespace {

constexpr size_t kMaxMarkParts = 8;
constexpr size_t kMarkBase = 5;
constexpr size_t kLabelSlot = kMarkBase + kMaxMarkParts;
constexpr size_t kBufCount = kLabelSlot + 1;

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

}  // namespace

Checkbox::Checkbox() {
  const CheckboxStylePreset& p = ui_get_style_presets().checkbox;
  box_size = p.box_size;
  gap = p.gap;
  height = p.height;
  label_color = p.label_color;
  box_border_color = p.box_border_color;
  box_background_color = p.box_background_color;
  box_background_hovered = p.box_background_hovered;
  check_color = p.check_color;
  check_kind = p.check_kind;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(kBufCount);
}

Checkbox::~Checkbox() = default;

CheckMarkShape Checkbox::generate_square_check() {
  CheckMarkShape shape;
  // Larger inset square — smaller gap to the border.
  shape.parts.push_back({0.16f, 0.16f, 0.68f, 0.68f});
  return shape;
}

CheckMarkShape Checkbox::generate_checkmark() {
  // Approximate ✓ with stepped axis-aligned bars.
  CheckMarkShape shape;
  shape.parts.push_back({0.18f, 0.48f, 0.18f, 0.16f});
  shape.parts.push_back({0.28f, 0.58f, 0.16f, 0.16f});
  shape.parts.push_back({0.38f, 0.48f, 0.14f, 0.16f});
  shape.parts.push_back({0.48f, 0.34f, 0.14f, 0.18f});
  shape.parts.push_back({0.58f, 0.22f, 0.14f, 0.18f});
  shape.parts.push_back({0.68f, 0.14f, 0.14f, 0.16f});
  return shape;
}

void Checkbox::set_custom_check_shape(const CheckMarkShape& shape) {
  custom_check = shape;
  check_kind = CheckMarkKind::Custom;
}

const CheckMarkShape& Checkbox::ActiveShape() const {
  static const CheckMarkShape kSquare = generate_square_check();
  static const CheckMarkShape kCheck = generate_checkmark();
  switch (check_kind) {
    case CheckMarkKind::Checkmark:
      return kCheck;
    case CheckMarkKind::Custom:
      return custom_check;
    case CheckMarkKind::Square:
    default:
      return kSquare;
  }
}

void Checkbox::EnsureBuffer() {
  if (draw_command_buffer_.size() < kBufCount) {
    draw_command_buffer_.resize(kBufCount);
  }
}

void Checkbox::bind_data(bool* data) {
  bound_ = data;
  if (bound_) {
    checked = *bound_;
  }
}

void Checkbox::WriteBound() {
  if (bound_) {
    *bound_ = checked;
  }
}

void Checkbox::get_layout_size(float& out_w, float& out_h) const {
  out_w = box_size + (label.empty() ? 0.f : gap + 120.f);
  out_h = height;
}

void Checkbox::handle_messages(const MouseEvents& mouse,
                               const KeyboardEvents&) {
  if (disabled) {
    state = ComponentState::Base;
    return;
  }

  float layout_w = 0.f;
  float layout_h = 0.f;
  get_layout_size(layout_w, layout_h);
  const bool hovered = Hit(mouse.x, mouse.y, x, y, layout_w, layout_h);

  if (mouse.left_pressed && hovered) {
    checked = !checked;
    WriteBound();
    enqueue_event(on_click);
  }

  if (hovered && mouse.left_down) {
    state = ComponentState::Active;
  } else if (hovered) {
    state = ComponentState::Hovered;
  } else {
    state = ComponentState::Base;
  }
}

void Checkbox::build_draw_buffer() {
  EnsureBuffer();

  const float box_y = y + (height - box_size) * 0.5f;
  const Color bg = (state == ComponentState::Hovered ||
                    state == ComponentState::Active)
                       ? box_background_hovered
                       : box_background_color;

  DrawCommand& bg_cmd = draw_command_buffer_[0];
  bg_cmd.type = DrawCommandType::Rect;
  bg_cmd.x = x;
  bg_cmd.y = box_y;
  bg_cmd.width = box_size;
  bg_cmd.height = box_size;
  bg_cmd.color = bg;
  bg_cmd.layer = layer;
  bg_cmd.text.clear();
  bg_cmd.wrap = false;

  const float bt = 1.5f;
  auto write_border = [&](size_t idx, float sx, float sy, float sw, float sh) {
    DrawCommand& c = draw_command_buffer_[idx];
    c.type = DrawCommandType::Rect;
    c.x = sx;
    c.y = sy;
    c.width = sw;
    c.height = sh;
    c.color = box_border_color;
    c.layer = layer;
    c.text.clear();
    c.wrap = false;
  };
  write_border(1, x, box_y, bt, box_size);
  write_border(2, x + box_size - bt, box_y, bt, box_size);
  write_border(3, x, box_y, box_size, bt);
  write_border(4, x, box_y + box_size - bt, box_size, bt);

  for (size_t i = 0; i < kMaxMarkParts; ++i) {
    ClearCmd(draw_command_buffer_[kMarkBase + i]);
  }

  if (checked) {
    const CheckMarkShape& shape = ActiveShape();
    const size_t count = std::min(shape.parts.size(), kMaxMarkParts);
    for (size_t i = 0; i < count; ++i) {
      const CheckMarkPart& p = shape.parts[i];
      DrawCommand& mark = draw_command_buffer_[kMarkBase + i];
      mark.type = DrawCommandType::Rect;
      mark.x = x + p.x * box_size;
      mark.y = box_y + p.y * box_size;
      mark.width = p.w * box_size;
      mark.height = p.h * box_size;
      mark.color = check_color;
      mark.layer = layer;
      mark.text.clear();
      mark.wrap = false;
    }
  }

  DrawCommand& label_cmd = draw_command_buffer_[kLabelSlot];
  label_cmd.type = DrawCommandType::Text;
  label_cmd.x = x + box_size + gap;
  label_cmd.y = y;
  label_cmd.width = 400.f;
  label_cmd.height = height;
  label_cmd.color = label_color;
  label_cmd.layer = layer;
  label_cmd.text = label;
  label_cmd.wrap = false;
  label_cmd.text_align = TextAlign::LeftMiddle;
  apply_font(label_cmd);
}
