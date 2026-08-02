#include "Panel.h"

namespace {

bool Hit(float px, float py, float x, float y, float w, float h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

}  // namespace

Panel::Panel() {
  const PanelStylePreset& p = ui_get_style_presets().panel;
  width = p.width;
  height = p.height;
  title_height = p.title_height;
  title_color = p.title_color;
  title_bar_color = p.title_bar_color;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(8);
}

Panel::~Panel() = default;

void Panel::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  out_h = height;
}

void Panel::handle_messages(const MouseEvents& mouse,
                            const KeyboardEvents& keyboard) {
  if (disabled) {
    return;
  }

  const bool over_title =
      Hit(mouse.x, mouse.y, x, y, width - (closable ? title_height : 0.f),
          title_height);
  const bool over_close =
      closable && Hit(mouse.x, mouse.y, x + width - title_height, y,
                      title_height, title_height);

  if (mouse.left_released) {
    dragging_ = false;
  }

  if (dragging_ && mouse.left_down && draggable) {
    x = mouse.x - drag_dx_;
    y = mouse.y - drag_dy_;
  }

  if (mouse.left_pressed && over_close) {
    if (on_close) {
      enqueue_event(on_close);
    }
    return;
  }

  if (mouse.left_pressed && over_title && draggable) {
    dragging_ = true;
    drag_dx_ = mouse.x - x;
    drag_dy_ = mouse.y - y;
  }

  state = Hit(mouse.x, mouse.y, x, y, width, height) ? ComponentState::Hovered
                                                     : ComponentState::Base;

  const float content_x = x;
  const float content_y = y + title_height;
  for (Component* child : components) {
    if (!child) {
      continue;
    }
    const float sx = child->x;
    const float sy = child->y;
    child->x = content_x + sx;
    child->y = content_y + sy;
    child->ui = ui;
    child->handle_messages(mouse, keyboard);
    child->x = sx;
    child->y = sy;
  }
}

void Panel::update(float dt) {
  Component::update(dt);
  for (Component* child : components) {
    if (child) {
      child->update(dt);
    }
  }
}

void Panel::handle_events() {
  Component::handle_events();
  for (Component* child : components) {
    if (child) {
      child->handle_events();
    }
  }
}

void Panel::build_draw_buffer() {
  if (draw_command_buffer_.size() < 8) {
    draw_command_buffer_.resize(8);
  }
  write_box_commands(0, width, height);

  DrawCommand& title_bg = draw_command_buffer_[5];
  title_bg.type = DrawCommandType::Rect;
  title_bg.x = x;
  title_bg.y = y;
  title_bg.width = width;
  title_bg.height = title_height;
  title_bg.color = title_bar_color;
  title_bg.layer = layer;

  DrawCommand& title_text = draw_command_buffer_[6];
  title_text.type = DrawCommandType::Text;
  title_text.x = x + 10.f;
  title_text.y = y;
  title_text.width = width - (closable ? title_height + 10.f : 20.f);
  title_text.height = title_height;
  title_text.color = title_color;
  title_text.layer = layer;
  title_text.text = title;
  title_text.wrap = false;
  title_text.text_align = TextAlign::LeftMiddle;
  apply_font(title_text);

  DrawCommand& close_txt = draw_command_buffer_[7];
  if (closable) {
    close_txt.type = DrawCommandType::Text;
    close_txt.x = x + width - title_height;
    close_txt.y = y;
    close_txt.width = title_height;
    close_txt.height = title_height;
    close_txt.color = title_color;
    close_txt.layer = layer;
    close_txt.text = L"x";
    close_txt.wrap = false;
    close_txt.text_align = TextAlign::Center;
    apply_font(close_txt);
  } else {
    close_txt.type = DrawCommandType::Text;
    close_txt.text.clear();
    close_txt.width = 0.f;
    apply_font(close_txt);
  }
}

void Panel::collect_draw(std::vector<DrawCommand*>& out, bool force_rebuild) {
  if (force_rebuild || !static_draw) {
    build_draw_buffer();
    reset_draw_origin_after_build();
  }
  emit_draw_buffer(out);

  const float content_x = x;
  const float content_y = y + title_height;
  for (Component* child : components) {
    if (!child) {
      continue;
    }
    child->ui = ui;
    child->collect_draw(out, false);
    child->apply_draw_origin(content_x, content_y, layer);
  }
}

void Panel::collect_overlay_draw(std::vector<DrawCommand*>& out) {
  for (Component* child : components) {
    if (child) {
      child->collect_overlay_draw(out);
    }
  }
}

void Panel::on_nudge_draw_origin(float dx, float dy, int dlayer) {
  for (Component* child : components) {
    if (child) {
      child->nudge_draw_origin(dx, dy, dlayer);
    }
  }
}
