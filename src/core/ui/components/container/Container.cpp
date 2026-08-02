#include "Container.h"

#include <algorithm>

namespace {

bool PointInRect(float px, float py, float x, float y, float w, float h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

// Buffer layout: [0..4] box, [5] PushClip, [6] PopClip,
// [7..8] vertical track/thumb, [9..10] horizontal track/thumb.
constexpr size_t kBufBox = 0;
constexpr size_t kBufClip = 5;
constexpr size_t kBufUnclip = 6;
constexpr size_t kBufVTrack = 7;
constexpr size_t kBufVThumb = 8;
constexpr size_t kBufHTrack = 9;
constexpr size_t kBufHThumb = 10;
constexpr size_t kBufCount = 11;

}  // namespace

Container::Container() {
  const ContainerStylePreset& p = ui_get_style_presets().container;
  width = p.width;
  height = p.height;
  place_mode = p.place_mode;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  ui_apply_scrollbar_preset(scroll_x, p.scroll_x);
  ui_apply_scrollbar_preset(scroll_y, p.scroll_y);
  draw_command_buffer_.resize(kBufCount);
}

Container::~Container() = default;

bool Container::captures_input() const {
  for (Component* child : components) {
    if (child && child->captures_input()) {
      return true;
    }
  }
  return false;
}

Container::Metrics Container::ComputeMetrics() const {
  Metrics m;
  float max_r = 0.f;
  float max_b = 0.f;
  for (Component* child : components) {
    if (!child) {
      continue;
    }
    float cw = 0.f;
    float ch = 0.f;
    child->get_layout_size(cw, ch);
    max_r = std::max(max_r, child->x + cw);
    max_b = std::max(max_b, child->y + ch);
  }
  m.content_w = max_r;
  m.content_h = max_b;

  m.bar_thickness_x = scroll_x.style_base.thickness;
  m.bar_thickness_y = scroll_y.style_base.thickness;

  const bool need_x = m.content_w > width + 0.5f;
  const bool need_y = m.content_h > height + 0.5f;

  m.show_x = (scroll_x.mode == ScrollMode::Scroll) ||
             (scroll_x.mode == ScrollMode::Auto && need_x);
  m.show_y = (scroll_y.mode == ScrollMode::Scroll) ||
             (scroll_y.mode == ScrollMode::Auto && need_y);

  // Auto: if one bar appears and eats space (In), the other may become needed.
  if (place_mode == ScrollPlaceMode::In) {
    if (m.show_y && scroll_x.mode == ScrollMode::Auto &&
        m.content_w > width - m.bar_thickness_y + 0.5f) {
      m.show_x = true;
    }
    if (m.show_x && scroll_y.mode == ScrollMode::Auto &&
        m.content_h > height - m.bar_thickness_x + 0.5f) {
      m.show_y = true;
    }
  }

  m.viewport_w = width;
  m.viewport_h = height;
  if (place_mode == ScrollPlaceMode::In) {
    if (m.show_y) {
      m.viewport_w = std::max(0.f, width - m.bar_thickness_y);
    }
    if (m.show_x) {
      m.viewport_h = std::max(0.f, height - m.bar_thickness_x);
    }
  }

  return m;
}

void Container::InvalidateMetrics() const {
  metrics_valid_ = false;
}

const Container::Metrics& Container::MetricsCached() const {
  if (!metrics_valid_) {
    metrics_cache_ = ComputeMetrics();
    metrics_valid_ = true;
  }
  return metrics_cache_;
}

void Container::ClampScroll(const Metrics& m) {
  const float max_x = std::max(0.f, m.content_w - m.viewport_w);
  const float max_y = std::max(0.f, m.content_h - m.viewport_h);
  scroll_offset_x_ = std::clamp(scroll_offset_x_, 0.f, max_x);
  scroll_offset_y_ = std::clamp(scroll_offset_y_, 0.f, max_y);
}

const ScrollbarStateStyle& Container::StyleForState(const ScrollBar& bar,
                                                   ComponentState s) const {
  switch (s) {
    case ComponentState::Hovered:
      return bar.style_hovered;
    case ComponentState::Active:
      return bar.style_active;
    case ComponentState::Base:
    default:
      return bar.style_base;
  }
}

void Container::get_layout_size(float& out_w, float& out_h) const {
  InvalidateMetrics();
  const Metrics& m = MetricsCached();
  out_w = width;
  out_h = height;
  if (place_mode == ScrollPlaceMode::Out) {
    if (m.show_y) {
      out_w += m.bar_thickness_y;
    }
    if (m.show_x) {
      out_h += m.bar_thickness_x;
    }
  }
}

void Container::ClearCommand(DrawCommand& cmd) {
  cmd.type = DrawCommandType::Rect;
  cmd.x = 0.f;
  cmd.y = 0.f;
  cmd.width = 0.f;
  cmd.height = 0.f;
  cmd.color = {};
  cmd.texture_id = -1;
  cmd.layer = layer;
  cmd.text.clear();
  cmd.wrap = false;
}

void Container::WriteScrollbar(size_t track_index, size_t thumb_index,
                               bool vertical, const Metrics& m) {
  DrawCommand& track = draw_command_buffer_[track_index];
  DrawCommand& thumb = draw_command_buffer_[thumb_index];

  const bool show = vertical ? m.show_y : m.show_x;
  if (!show) {
    ClearCommand(track);
    ClearCommand(thumb);
    return;
  }

  const ScrollBar& bar = vertical ? scroll_y : scroll_x;
  const ComponentState bar_state =
      vertical ? scroll_y_state_ : scroll_x_state_;
  const ScrollbarStateStyle& st = StyleForState(bar, bar_state);
  const float thickness = st.thickness;

  float track_x = 0.f;
  float track_y = 0.f;
  float track_w = 0.f;
  float track_h = 0.f;

  if (vertical) {
    track_x = x + m.viewport_w;
    track_y = y;
    track_w = thickness;
    track_h = m.viewport_h;
  } else {
    track_x = x;
    track_y = y + m.viewport_h;
    track_w = m.viewport_w;
    track_h = thickness;
  }

  track.type = DrawCommandType::Rect;
  track.x = track_x;
  track.y = track_y;
  track.width = track_w;
  track.height = track_h;
  track.color = st.track_color;
  track.layer = layer;
  track.text.clear();
  track.wrap = false;

  const float content = vertical ? m.content_h : m.content_w;
  const float viewport = vertical ? m.viewport_h : m.viewport_w;
  const float offset = vertical ? scroll_offset_y_ : scroll_offset_x_;
  if (content <= viewport + 0.5f) {
    ClearCommand(thumb);
    return;
  }

  const float track_len = vertical ? track_h : track_w;
  const float thumb_len =
      std::max(thickness, track_len * (viewport / content));
  const float max_offset = content - viewport;
  const float travel = track_len - thumb_len;
  const float thumb_pos =
      (max_offset > 0.f) ? (offset / max_offset) * travel : 0.f;

  thumb.type = DrawCommandType::Rect;
  thumb.layer = layer;
  thumb.color = st.thumb_color;
  thumb.text.clear();
  thumb.wrap = false;
  if (vertical) {
    thumb.x = track_x;
    thumb.y = track_y + thumb_pos;
    thumb.width = thickness;
    thumb.height = thumb_len;
  } else {
    thumb.x = track_x + thumb_pos;
    thumb.y = track_y;
    thumb.width = thumb_len;
    thumb.height = thickness;
  }
}

void Container::build_draw_buffer() {
  if (draw_command_buffer_.size() < kBufCount) {
    draw_command_buffer_.resize(kBufCount);
  }

  InvalidateMetrics();
  const Metrics& m = MetricsCached();
  ClampScroll(m);

  write_box_commands(kBufBox, width, height);

  DrawCommand& clip = draw_command_buffer_[kBufClip];
  clip.type = DrawCommandType::PushClip;
  clip.x = x;
  clip.y = y;
  clip.width = m.viewport_w;
  clip.height = m.viewport_h;
  clip.color = {};
  clip.layer = layer;
  clip.text.clear();
  clip.wrap = false;

  DrawCommand& unclip = draw_command_buffer_[kBufUnclip];
  unclip.type = DrawCommandType::PopClip;
  unclip.x = 0.f;
  unclip.y = 0.f;
  unclip.width = 0.f;
  unclip.height = 0.f;
  unclip.color = {};
  unclip.layer = layer;
  unclip.text.clear();
  unclip.wrap = false;

  WriteScrollbar(kBufVTrack, kBufVThumb, true, m);
  WriteScrollbar(kBufHTrack, kBufHThumb, false, m);
}

void Container::collect_draw(std::vector<DrawCommand*>& out,
                             bool force_rebuild) {
  InvalidateMetrics();
  if (force_rebuild || !static_draw) {
    build_draw_buffer();
    reset_draw_origin_after_build();
  }

  const Metrics& m = MetricsCached();
  float ox = scroll_offset_x_;
  float oy = scroll_offset_y_;
  const float max_x = std::max(0.f, m.content_w - m.viewport_w);
  const float max_y = std::max(0.f, m.content_h - m.viewport_h);
  ox = std::clamp(ox, 0.f, max_x);
  oy = std::clamp(oy, 0.f, max_y);

  // Box
  for (size_t i = kBufBox; i < kBufClip; ++i) {
    DrawCommand& cmd = draw_command_buffer_[i];
    if (cmd.width > 0.f && cmd.height > 0.f && cmd.color.a > 0.f) {
      out.push_back(&cmd);
    }
  }

  out.push_back(&draw_command_buffer_[kBufClip]);

  const float origin_x = x - ox;
  const float origin_y = y - oy;
  for (Component* child : components) {
    if (!child) {
      continue;
    }
    child->ui = ui;
    child->collect_draw(out, false);
    // Patch in place after emit: pointers in `out` already reference the buffer.
    child->apply_draw_origin(origin_x, origin_y, layer);
  }

  out.push_back(&draw_command_buffer_[kBufUnclip]);

  auto emit_slot = [&](size_t index) {
    DrawCommand& cmd = draw_command_buffer_[index];
    if (cmd.width > 0.f && cmd.height > 0.f && cmd.color.a > 0.f) {
      out.push_back(&cmd);
    }
  };
  emit_slot(kBufVTrack);
  emit_slot(kBufVThumb);
  emit_slot(kBufHTrack);
  emit_slot(kBufHThumb);
}

void Container::collect_overlay_draw(std::vector<DrawCommand*>& out) {
  // Child overlay buffers were built/shifted during collect_draw.
  for (Component* child : components) {
    if (child) {
      child->collect_overlay_draw(out);
    }
  }
}

void Container::on_nudge_draw_origin(float dx, float dy, int dlayer) {
  for (Component* child : components) {
    if (child) {
      child->nudge_draw_origin(dx, dy, dlayer);
    }
  }
}

void Container::handle_messages(const MouseEvents& mouse,
                                const KeyboardEvents& keyboard) {
  if (disabled) {
    state = ComponentState::Base;
    dragging_x_ = false;
    dragging_y_ = false;
    scroll_x_state_ = ComponentState::Base;
    scroll_y_state_ = ComponentState::Base;
    return;
  }

  InvalidateMetrics();
  const Metrics& m = MetricsCached();
  ClampScroll(m);

  const bool over_view =
      PointInRect(mouse.x, mouse.y, x, y, m.viewport_w, m.viewport_h);

  // Prefer focused (capturing) children: do not scroll this container.
  bool child_capture = false;
  for (Component* child : components) {
    if (child && child->captures_input()) {
      child_capture = true;
      break;
    }
  }
  if (child_capture) {
    dragging_x_ = false;
    dragging_y_ = false;
    scroll_x_state_ = ComponentState::Base;
    scroll_y_state_ = ComponentState::Base;
    state = over_view ? ComponentState::Hovered : ComponentState::Base;
    for (Component* child : components) {
      if (!child || !child->captures_input()) {
        continue;
      }
      const float saved_x = child->x;
      const float saved_y = child->y;
      child->x = x + saved_x - scroll_offset_x_;
      child->y = y + saved_y - scroll_offset_y_;
      child->ui = ui;
      child->handle_messages(mouse, keyboard);
      child->x = saved_x;
      child->y = saved_y;
    }
    return;
  }

  // Scrollbars hit areas
  const float tx = x + m.viewport_w;
  const float ty = y + m.viewport_h;
  const bool over_vbar =
      m.show_y && PointInRect(mouse.x, mouse.y, tx, y, m.bar_thickness_y,
                              m.viewport_h);
  const bool over_hbar =
      m.show_x && PointInRect(mouse.x, mouse.y, x, ty, m.viewport_w,
                              m.bar_thickness_x);

  if (mouse.left_released) {
    dragging_x_ = false;
    dragging_y_ = false;
  }

  if (dragging_y_ && mouse.left_down && m.show_y) {
    scroll_y_state_ = ComponentState::Active;
    const float content = m.content_h;
    const float viewport = m.viewport_h;
    const float track_len = m.viewport_h;
    const float thumb_len =
        std::max(m.bar_thickness_y, track_len * (viewport / content));
    const float travel = std::max(0.f, track_len - thumb_len);
    const float max_offset = std::max(0.f, content - viewport);
    if (travel > 0.f && max_offset > 0.f) {
      const float delta = mouse.y - drag_mouse_anchor_;
      scroll_offset_y_ = drag_thumb_anchor_ + (delta / travel) * max_offset;
    }
    ClampScroll(m);
    return;
  }

  if (dragging_x_ && mouse.left_down && m.show_x) {
    scroll_x_state_ = ComponentState::Active;
    const float content = m.content_w;
    const float viewport = m.viewport_w;
    const float track_len = m.viewport_w;
    const float thumb_len =
        std::max(m.bar_thickness_x, track_len * (viewport / content));
    const float travel = std::max(0.f, track_len - thumb_len);
    const float max_offset = std::max(0.f, content - viewport);
    if (travel > 0.f && max_offset > 0.f) {
      const float delta = mouse.x - drag_mouse_anchor_;
      scroll_offset_x_ = drag_thumb_anchor_ + (delta / travel) * max_offset;
    }
    ClampScroll(m);
    return;
  }

  scroll_y_state_ = over_vbar ? ComponentState::Hovered : ComponentState::Base;
  scroll_x_state_ = over_hbar ? ComponentState::Hovered : ComponentState::Base;

  if (mouse.left_pressed && over_vbar && m.show_y && m.content_h > m.viewport_h) {
    dragging_y_ = true;
    scroll_y_state_ = ComponentState::Active;
    drag_mouse_anchor_ = mouse.y;
    drag_thumb_anchor_ = scroll_offset_y_;
    return;
  }
  if (mouse.left_pressed && over_hbar && m.show_x && m.content_w > m.viewport_w) {
    dragging_x_ = true;
    scroll_x_state_ = ComponentState::Active;
    drag_mouse_anchor_ = mouse.x;
    drag_thumb_anchor_ = scroll_offset_x_;
    return;
  }

  state = over_view ? ComponentState::Hovered : ComponentState::Base;

  if (!over_view) {
    return;
  }

  // Children first (topmost / later siblings first) so a nested scrollable
  // under the cursor can consume the wheel before this container.
  for (auto it = components.rbegin(); it != components.rend(); ++it) {
    Component* child = *it;
    if (!child) {
      continue;
    }
    const float saved_x = child->x;
    const float saved_y = child->y;
    child->x = x + saved_x - scroll_offset_x_;
    child->y = y + saved_y - scroll_offset_y_;
    child->ui = ui;
    child->handle_messages(mouse, keyboard);
    child->x = saved_x;
    child->y = saved_y;
  }

  if (mouse.wheel_delta != 0.f && !mouse.wheel_consumed) {
    const bool can_y = m.show_y && m.content_h > m.viewport_h;
    const bool can_x = m.show_x && m.content_w > m.viewport_w;
    if (can_y) {
      scroll_offset_y_ -= mouse.wheel_delta * 40.f;
      mouse.wheel_consumed = true;
    } else if (can_x) {
      scroll_offset_x_ -= mouse.wheel_delta * 40.f;
      mouse.wheel_consumed = true;
    }
    ClampScroll(m);
  }
}

void Container::update(float dt) {
  Component::update(dt);
  for (Component* child : components) {
    if (child) {
      child->update(dt);
    }
  }
}

void Container::handle_events() {
  Component::handle_events();
  for (Component* child : components) {
    if (child) {
      child->handle_events();
    }
  }
}
