#include "RadioGroup.h"

#include <algorithm>

namespace {

bool Hit(float px, float py, float x, float y, float w, float h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

void ClearCmd(DrawCommand& cmd) {
  cmd.type = DrawCommandType::Rect;
  cmd.width = 0.f;
  cmd.height = 0.f;
  cmd.color.a = 0.f;
  cmd.text.clear();
}

}  // namespace

RadioGroup::RadioGroup() {
  const RadioGroupStylePreset& p = ui_get_style_presets().radio_group;
  radio_size = p.radio_size;
  item_height = p.item_height;
  item_width = p.item_width;
  gap = p.gap;
  label_color = p.label_color;
  box_border_color = p.box_border_color;
  box_background_color = p.box_background_color;
  box_background_hovered = p.box_background_hovered;
  check_color = p.check_color;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
}

RadioGroup::~RadioGroup() = default;

void RadioGroup::bind_data(int* data) {
  bound_ = data;
  if (bound_) {
    selected = *bound_;
  }
}

void RadioGroup::WriteBound() {
  if (bound_) {
    *bound_ = selected;
  }
}

void RadioGroup::EnsureBuffer() {
  // Per option: outer + (up to 4 border/ring slots) + check + label = 7
  // Circle uses: outer ring (Circle), hole (Circle bg), check (Circle), label
  // Square uses: bg + 4 borders + check + label
  const size_t need = options.size() * 7;
  if (draw_command_buffer_.size() < need) {
    draw_command_buffer_.resize(need);
  }
}

void RadioGroup::get_layout_size(float& out_w, float& out_h) const {
  const size_t n = options.size();
  if (n == 0) {
    out_w = 0.f;
    out_h = 0.f;
    return;
  }
  if (orientation == RadioOrientation::Horizontal) {
    out_w = static_cast<float>(n) * item_width +
            static_cast<float>(n > 0 ? n - 1 : 0) * gap;
    out_h = item_height;
  } else {
    out_w = item_width;
    out_h = static_cast<float>(n) * item_height +
            static_cast<float>(n > 0 ? n - 1 : 0) * gap;
  }
}

void RadioGroup::handle_messages(const MouseEvents& mouse,
                                 const KeyboardEvents&) {
  if (disabled) {
    state = ComponentState::Base;
    hovered_index_ = -1;
    return;
  }

  hovered_index_ = -1;
  for (size_t i = 0; i < options.size(); ++i) {
    float ix = x;
    float iy = y;
    if (orientation == RadioOrientation::Horizontal) {
      ix = x + static_cast<float>(i) * (item_width + gap);
    } else {
      iy = y + static_cast<float>(i) * (item_height + gap);
    }
    if (Hit(mouse.x, mouse.y, ix, iy, item_width, item_height)) {
      hovered_index_ = static_cast<int>(i);
      break;
    }
  }

  if (hovered_index_ >= 0 && mouse.left_down) {
    state = ComponentState::Active;
  } else if (hovered_index_ >= 0) {
    state = ComponentState::Hovered;
  } else {
    state = ComponentState::Base;
  }

  if (!mouse.left_pressed || hovered_index_ < 0) {
    return;
  }

  selected = hovered_index_;
  WriteBound();
  enqueue_event(on_click);
}

void RadioGroup::build_draw_buffer() {
  EnsureBuffer();

  for (size_t i = 0; i < options.size(); ++i) {
    const size_t base = i * 7;
    float ix = x;
    float iy = y;
    if (orientation == RadioOrientation::Horizontal) {
      ix = x + static_cast<float>(i) * (item_width + gap);
    } else {
      iy = y + static_cast<float>(i) * (item_height + gap);
    }

    const float box_y = iy + (item_height - radio_size) * 0.5f;
    const bool item_hovered = static_cast<int>(i) == hovered_index_;
    const Color bg =
        item_hovered ? box_background_hovered : box_background_color;
    const bool is_selected = static_cast<int>(i) == selected;

    for (size_t s = 0; s < 7; ++s) {
      ClearCmd(draw_command_buffer_[base + s]);
    }

    if (mode == RadioMode::Circle) {
      // Outer disc = border color, inner disc = background (ring), check disc.
      DrawCommand& outer = draw_command_buffer_[base];
      outer.type = DrawCommandType::Circle;
      outer.x = ix;
      outer.y = box_y;
      outer.width = radio_size;
      outer.height = radio_size;
      outer.color = box_border_color;
      outer.layer = layer;

      const float inset = 1.5f;
      DrawCommand& inner = draw_command_buffer_[base + 1];
      inner.type = DrawCommandType::Circle;
      inner.x = ix + inset;
      inner.y = box_y + inset;
      inner.width = radio_size - inset * 2.f;
      inner.height = radio_size - inset * 2.f;
      inner.color = bg;
      inner.layer = layer;

      DrawCommand& check = draw_command_buffer_[base + 5];
      if (is_selected) {
        const float dot_inset = radio_size * 0.28f;
        check.type = DrawCommandType::Circle;
        check.x = ix + dot_inset;
        check.y = box_y + dot_inset;
        check.width = radio_size - dot_inset * 2.f;
        check.height = radio_size - dot_inset * 2.f;
        check.color = check_color;
        check.layer = layer;
      }
    } else {
      DrawCommand& bg_cmd = draw_command_buffer_[base];
      bg_cmd.type = DrawCommandType::Rect;
      bg_cmd.x = ix;
      bg_cmd.y = box_y;
      bg_cmd.width = radio_size;
      bg_cmd.height = radio_size;
      bg_cmd.color = bg;
      bg_cmd.layer = layer;

      const float bt = 1.5f;
      auto write_side = [&](size_t idx, float sx, float sy, float sw, float sh) {
        DrawCommand& c = draw_command_buffer_[base + idx];
        c.type = DrawCommandType::Rect;
        c.x = sx;
        c.y = sy;
        c.width = sw;
        c.height = sh;
        c.color = box_border_color;
        c.layer = layer;
      };
      write_side(1, ix, box_y, bt, radio_size);
      write_side(2, ix + radio_size - bt, box_y, bt, radio_size);
      write_side(3, ix, box_y, radio_size, bt);
      write_side(4, ix, box_y + radio_size - bt, radio_size, bt);

      DrawCommand& check = draw_command_buffer_[base + 5];
      if (is_selected) {
        const float inset = radio_size * 0.28f;
        check.type = DrawCommandType::Rect;
        check.x = ix + inset;
        check.y = box_y + inset;
        check.width = radio_size - inset * 2.f;
        check.height = radio_size - inset * 2.f;
        check.color = check_color;
        check.layer = layer;
      }
    }

    DrawCommand& label = draw_command_buffer_[base + 6];
    label.type = DrawCommandType::Text;
    label.x = ix + radio_size + 8.f;
    label.y = iy;
    label.width = item_width - radio_size - 8.f;
    label.height = item_height;
    label.color = label_color;
    label.layer = layer;
    label.text = options[i];
    label.wrap = false;
    label.text_align = TextAlign::LeftMiddle;
    apply_font(label);
  }
}
