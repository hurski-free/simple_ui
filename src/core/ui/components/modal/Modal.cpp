#include "Modal.h"

namespace {

bool Hit(float px, float py, float x, float y, float w, float h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

}  // namespace

Modal::Modal() {
  const ModalStylePreset& p = ui_get_style_presets().modal;
  width = p.width;
  height = p.height;
  title_height = p.title_height;
  screen_width = p.screen_width;
  screen_height = p.screen_height;
  layer = p.layer;
  overlay_color = p.overlay_color;
  title_color = p.title_color;
  title_bar_color = p.title_bar_color;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(8);
}

Modal::~Modal() = default;

bool Modal::captures_input() const {
  return open;
}

void Modal::LayoutDialog() {
  x = (screen_width - width) * 0.5f;
  y = (screen_height - height) * 0.5f;
}

void Modal::get_layout_size(float& out_w, float& out_h) const {
  out_w = open ? screen_width : 0.f;
  out_h = open ? screen_height : 0.f;
}

void Modal::handle_messages(const MouseEvents& mouse,
                            const KeyboardEvents& keyboard) {
  if (!open || disabled) {
    return;
  }
  LayoutDialog();

  const bool over_dialog = Hit(mouse.x, mouse.y, x, y, width, height);
  if (mouse.left_pressed && !over_dialog && close_on_overlay_click) {
    open = false;
    if (on_close) {
      enqueue_event(on_close);
    }
    return;
  }

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

void Modal::update(float dt) {
  if (!open) {
    return;
  }
  Component::update(dt);
  for (Component* child : components) {
    if (child) {
      child->update(dt);
    }
  }
}

void Modal::handle_events() {
  Component::handle_events();
  if (!open) {
    return;
  }
  for (Component* child : components) {
    if (child) {
      child->handle_events();
    }
  }
}

void Modal::build_draw_buffer() {
  if (!open) {
    for (DrawCommand& cmd : draw_command_buffer_) {
      cmd.width = 0.f;
      cmd.height = 0.f;
      cmd.text.clear();
      cmd.color.a = 0.f;
    }
    return;
  }

  LayoutDialog();
  if (draw_command_buffer_.size() < 8) {
    draw_command_buffer_.resize(8);
  }

  DrawCommand& overlay = draw_command_buffer_[0];
  overlay.type = DrawCommandType::Rect;
  overlay.x = 0.f;
  overlay.y = 0.f;
  overlay.width = screen_width;
  overlay.height = screen_height;
  overlay.color = overlay_color;
  overlay.layer = layer;

  // Dialog body uses slots 1..4 via manual write (skip Component visual for overlay).
  DrawCommand& body = draw_command_buffer_[1];
  body.type = DrawCommandType::Rect;
  body.x = x;
  body.y = y;
  body.width = width;
  body.height = height;
  body.color = visual_style().background_color;
  body.layer = layer;

  DrawCommand& title_bg = draw_command_buffer_[2];
  title_bg.type = DrawCommandType::Rect;
  title_bg.x = x;
  title_bg.y = y;
  title_bg.width = width;
  title_bg.height = title_height;
  title_bg.color = title_bar_color;
  title_bg.layer = layer;

  DrawCommand& title_text = draw_command_buffer_[3];
  title_text.type = DrawCommandType::Text;
  title_text.x = x + 12.f;
  title_text.y = y;
  title_text.width = width - 24.f;
  title_text.height = title_height;
  title_text.color = title_color;
  title_text.layer = layer;
  title_text.text = title;
  title_text.wrap = false;
  title_text.text_align = TextAlign::LeftMiddle;
  apply_font(title_text);

  for (size_t i = 4; i < draw_command_buffer_.size(); ++i) {
    draw_command_buffer_[i].width = 0.f;
    draw_command_buffer_[i].height = 0.f;
    draw_command_buffer_[i].text.clear();
    draw_command_buffer_[i].color.a = 0.f;
  }
}

void Modal::collect_draw(std::vector<DrawCommand*>& out, bool force_rebuild) {
  if (!open) {
    return;
  }
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

void Modal::collect_overlay_draw(std::vector<DrawCommand*>& out) {
  if (!open) {
    return;
  }
  for (Component* child : components) {
    if (child) {
      child->collect_overlay_draw(out);
    }
  }
}

void Modal::on_nudge_draw_origin(float dx, float dy, int dlayer) {
  for (Component* child : components) {
    if (child) {
      child->nudge_draw_origin(dx, dy, dlayer);
    }
  }
}
