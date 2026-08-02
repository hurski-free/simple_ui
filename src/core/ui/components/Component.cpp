#include "Component.h"

#include <algorithm>

namespace {

float Clamp01(float t) {
  return std::max(0.f, std::min(1.f, t));
}

Color LerpColor(const Color& a, const Color& b, float t) {
  return {
      a.r + (b.r - a.r) * t,
      a.g + (b.g - a.g) * t,
      a.b + (b.b - a.b) * t,
      a.a + (b.a - a.a) * t,
  };
}

float EffectiveThickness(const Border& border) {
  return border.mode == BorderMode::None ? 0.f : border.thickness;
}

Border LerpBorder(const Border& a, const Border& b, float t) {
  Border out{};

  if (a.mode == BorderMode::None && b.mode == BorderMode::None) {
    out.mode = BorderMode::None;
    out.thickness = 0.f;
    out.color = LerpColor(a.color, b.color, t);
    return out;
  }

  const float thickness_a = EffectiveThickness(a);
  const float thickness_b = EffectiveThickness(b);
  out.thickness = thickness_a + (thickness_b - thickness_a) * t;
  out.color = LerpColor(a.color, b.color, t);

  if (b.mode == BorderMode::None) {
    out.mode = (t >= 1.f) ? BorderMode::None : a.mode;
    if (out.mode == BorderMode::None) {
      out.thickness = 0.f;
    }
  } else if (a.mode == BorderMode::None) {
    out.mode = b.mode;
  } else {
    out.mode = b.mode;
  }

  return out;
}

void CopyBordersFromStyle(const ComponentStyle& style, Border out[5]) {
  out[0] = style.border;
  out[1] = style.border_left;
  out[2] = style.border_right;
  out[3] = style.border_top;
  out[4] = style.border_bottom;
}

void CopyBordersToStyle(const Border in[5], ComponentStyle& style) {
  style.border = in[0];
  style.border_left = in[1];
  style.border_right = in[2];
  style.border_top = in[3];
  style.border_bottom = in[4];
}

void WriteRect(DrawCommand& cmd, float x, float y, float width, float height,
               const Color& color, int layer) {
  cmd.type = DrawCommandType::Rect;
  cmd.x = x;
  cmd.y = y;
  cmd.width = width;
  cmd.height = height;
  cmd.color = color;
  cmd.texture_id = -1;
  cmd.layer = layer;
  cmd.text.clear();
  cmd.wrap = false;
}

void ClearRect(DrawCommand& cmd) {
  cmd.type = DrawCommandType::Rect;
  cmd.x = 0.f;
  cmd.y = 0.f;
  cmd.width = 0.f;
  cmd.height = 0.f;
  cmd.color = {};
  cmd.texture_id = -1;
  cmd.layer = 0;
  cmd.text.clear();
  cmd.wrap = false;
}

const Border& ResolveBorder(const Border& side, const Border& fallback) {
  return side.mode != BorderMode::None ? side : fallback;
}

float OutExtent(const Border& border) {
  return (border.mode == BorderMode::Out && border.thickness > 0.f)
             ? border.thickness
             : 0.f;
}

}  // namespace
Component::Component() {
  const ComponentStylePreset& p = ui_get_style_presets().component;
  font_size = p.font_size;
  transition = p.transition;
}

Component::~Component() = default;

void Component::handle_messages(const MouseEvents&, const KeyboardEvents&) {
  if (disabled) {
    return;
  }
}

void Component::update(float dt) {
  SyncStyleAnimation();
  const bool animating =
      (bg_duration_ > 0.f && bg_elapsed_ < bg_duration_) ||
      (border_duration_ > 0.f && border_elapsed_ < border_duration_);
  if (animating) {
    AdvanceStyleAnimation(dt);
  }
}

void Component::build_draw_buffer() {}

void Component::build_draw_static() {
  static_draw = true;
  build_draw_buffer();
  reset_draw_origin_after_build();
}

void Component::collect_draw(std::vector<DrawCommand*>& out,
                             bool force_rebuild) {
  if (force_rebuild || !static_draw) {
    build_draw_buffer();
    reset_draw_origin_after_build();
  }
  emit_draw_buffer(out);
}

void Component::collect_overlay_draw(std::vector<DrawCommand*>&) {}

void Component::reset_draw_origin_after_build() {
  draw_origin_x_ = 0.f;
  draw_origin_y_ = 0.f;
  draw_origin_layer_ = layer;
}

void Component::apply_draw_origin(float origin_x, float origin_y,
                                  int origin_layer) {
  nudge_draw_origin(origin_x - draw_origin_x_, origin_y - draw_origin_y_,
                    origin_layer - draw_origin_layer_);
}

void Component::nudge_draw_origin(float dx, float dy, int dlayer) {
  if (dx == 0.f && dy == 0.f && dlayer == 0) {
    return;
  }
  for (DrawCommand& cmd : draw_command_buffer_) {
    cmd.x += dx;
    cmd.y += dy;
    cmd.layer += dlayer;
  }
  draw_origin_x_ += dx;
  draw_origin_y_ += dy;
  draw_origin_layer_ += dlayer;
  on_nudge_draw_origin(dx, dy, dlayer);
}

void Component::on_nudge_draw_origin(float, float, int) {}

void Component::emit_draw_buffer(std::vector<DrawCommand*>& out) {
  for (DrawCommand& cmd : draw_command_buffer_) {
    if (cmd.type == DrawCommandType::Rect ||
        cmd.type == DrawCommandType::Circle ||
        cmd.type == DrawCommandType::RoundedRect ||
        cmd.type == DrawCommandType::Triangle) {
      if (cmd.width <= 0.f || cmd.height <= 0.f || cmd.color.a <= 0.f) {
        continue;
      }
    } else if (cmd.type == DrawCommandType::Image) {
      if (cmd.width <= 0.f || cmd.height <= 0.f || cmd.texture_id < 0) {
        continue;
      }
    } else if (cmd.type == DrawCommandType::Text) {
      if (cmd.text.empty()) {
        continue;
      }
    }
    out.push_back(&cmd);
  }
}

void Component::enqueue_event(const std::function<void()>& handler) {
  if (handler) {
    event_queue_.push_back(handler);
  }
}

void Component::handle_events() {
  std::vector<std::function<void()>> pending;
  pending.swap(event_queue_);
  for (const std::function<void()>& handler : pending) {
    if (handler) {
      handler();
    }
  }
}

void Component::get_layout_size(float& out_w, float& out_h) const {
  out_w = 0.f;
  out_h = 0.f;
}

bool Component::captures_input() const {
  return false;
}

const ComponentStyle& Component::visual_style() const {
  return visual_;
}

const ComponentStyle& Component::TargetStyle() const {
  if (disabled) {
    return style_disabled;
  }
  switch (state) {
    case ComponentState::Hovered:
      return style_hovered;
    case ComponentState::Active:
      return style_active;
    case ComponentState::Base:
    default:
      return style_base;
  }
}

void Component::SyncStyleAnimation() {
  const ComponentStyle& target = TargetStyle();

  if (!style_initialized_) {
    visual_ = target;
    style_initialized_ = true;
    last_state_ = state;
    last_disabled_ = disabled;
    bg_elapsed_ = 0.f;
    bg_duration_ = 0.f;
    border_elapsed_ = 0.f;
    border_duration_ = 0.f;
    return;
  }

  if (state == last_state_ && disabled == last_disabled_) {
    return;
  }

  last_state_ = state;
  last_disabled_ = disabled;

  bg_from_ = visual_.background_color;
  bg_to_ = target.background_color;
  bg_elapsed_ = 0.f;
  bg_duration_ = transition.background_duration;

  CopyBordersFromStyle(visual_, borders_from_);
  CopyBordersFromStyle(target, borders_to_);
  border_elapsed_ = 0.f;
  border_duration_ = transition.border_duration;

  if (bg_duration_ <= 0.f) {
    visual_.background_color = bg_to_;
  }
  if (border_duration_ <= 0.f) {
    CopyBordersToStyle(borders_to_, visual_);
  }
}

void Component::AdvanceStyleAnimation(float dt) {
  const bool bg_active = bg_duration_ > 0.f && bg_elapsed_ < bg_duration_;
  const bool border_active =
      border_duration_ > 0.f && border_elapsed_ < border_duration_;
  if (!bg_active && !border_active) {
    return;
  }
  if (dt < 0.f) {
    dt = 0.f;
  }

  if (bg_active) {
    bg_elapsed_ += dt;
    const float t = Clamp01(bg_elapsed_ / bg_duration_);
    visual_.background_color = LerpColor(bg_from_, bg_to_, t);
  }

  if (border_active) {
    border_elapsed_ += dt;
    const float t = Clamp01(border_elapsed_ / border_duration_);
    Border lerped[5];
    for (int i = 0; i < 5; ++i) {
      lerped[i] = LerpBorder(borders_from_[i], borders_to_[i], t);
    }
    CopyBordersToStyle(lerped, visual_);
  }
}

void Component::apply_font(DrawCommand& cmd) const {
  cmd.font_size = font_size;
  cmd.font_atlas = font;
}

float Component::effective_font_size() const {
  if (font_size > 0.f) {
    return font_size;
  }
  const float atlas = ui_font_atlas_size(ui, font);
  return atlas > 0.f ? atlas : 18.f;
}

void Component::write_box_commands(size_t start, float width, float height) {
  if (draw_command_buffer_.size() < start + 5) {
    draw_command_buffer_.resize(start + 5);
  }

  const ComponentStyle& style = visual_;
  WriteRect(draw_command_buffer_[start], x, y, width, height,
            style.background_color, layer);

  const Border& left = ResolveBorder(style.border_left, style.border);
  const Border& right = ResolveBorder(style.border_right, style.border);
  const Border& top = ResolveBorder(style.border_top, style.border);
  const Border& bottom = ResolveBorder(style.border_bottom, style.border);

  const float out_l = OutExtent(left);
  const float out_r = OutExtent(right);

  // Left
  if (left.mode == BorderMode::In && left.thickness > 0.f) {
    WriteRect(draw_command_buffer_[start + 1], x, y, left.thickness, height,
              left.color, layer);
  } else if (left.mode == BorderMode::Out && left.thickness > 0.f) {
    WriteRect(draw_command_buffer_[start + 1], x - left.thickness, y,
              left.thickness, height, left.color, layer);
  } else {
    ClearRect(draw_command_buffer_[start + 1]);
  }

  // Right
  if (right.mode == BorderMode::In && right.thickness > 0.f) {
    WriteRect(draw_command_buffer_[start + 2], x + width - right.thickness, y,
              right.thickness, height, right.color, layer);
  } else if (right.mode == BorderMode::Out && right.thickness > 0.f) {
    WriteRect(draw_command_buffer_[start + 2], x + width, y, right.thickness,
              height, right.color, layer);
  } else {
    ClearRect(draw_command_buffer_[start + 2]);
  }

  // Top
  if (top.mode == BorderMode::In && top.thickness > 0.f) {
    WriteRect(draw_command_buffer_[start + 3], x, y, width, top.thickness,
              top.color, layer);
  } else if (top.mode == BorderMode::Out && top.thickness > 0.f) {
    WriteRect(draw_command_buffer_[start + 3], x - out_l, y - top.thickness,
              width + out_l + out_r, top.thickness, top.color, layer);
  } else {
    ClearRect(draw_command_buffer_[start + 3]);
  }

  // Bottom
  if (bottom.mode == BorderMode::In && bottom.thickness > 0.f) {
    WriteRect(draw_command_buffer_[start + 4], x,
              y + height - bottom.thickness, width, bottom.thickness,
              bottom.color, layer);
  } else if (bottom.mode == BorderMode::Out && bottom.thickness > 0.f) {
    WriteRect(draw_command_buffer_[start + 4], x - out_l, y + height,
              width + out_l + out_r, bottom.thickness, bottom.color, layer);
  } else {
    ClearRect(draw_command_buffer_[start + 4]);
  }
}
