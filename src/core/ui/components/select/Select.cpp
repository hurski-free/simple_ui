#include "Select.h"

#include <algorithm>
#include <cmath>

namespace {

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
  cmd.texture_id = -1;
  cmd.layer = 0;
  cmd.text.clear();
  cmd.wrap = false;
}

}  // namespace

Select::Select() {
  const SelectStylePreset& p = ui_get_style_presets().select;
  width = p.width;
  height = p.height;
  dropdown_height = p.dropdown_height;
  item_height = p.item_height;
  text_color = p.text_color;
  dropdown_bg = p.dropdown_bg;
  item_hover_color = p.item_hover_color;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  ui_apply_scrollbar_preset(scroll_y, p.scroll_y);
}

Select::~Select() = default;

bool Select::captures_input() const {
  return open_;
}

void Select::bind_data(int* data) {
  bound_ = data;
  if (bound_) {
    selected = *bound_;
  }
}

void Select::WriteBound() {
  if (bound_) {
    *bound_ = selected;
  }
}

float Select::ContentHeight() const {
  return static_cast<float>(options.size()) * item_height;
}

bool Select::ShowScroll() const {
  const float content = ContentHeight();
  if (scroll_y.mode == ScrollMode::Hidden) {
    return false;
  }
  if (scroll_y.mode == ScrollMode::Scroll) {
    return true;
  }
  return content > dropdown_height + 0.5f;
}

float Select::ViewportWidth() const {
  if (ShowScroll()) {
    return std::max(0.f, width - scroll_y.style_base.thickness);
  }
  return width;
}

int Select::MaxVisibleSlots() const {
  if (item_height <= 0.f) {
    return 1;
  }
  return std::max(1, static_cast<int>(std::ceil(dropdown_height / item_height)) + 2);
}

void Select::ClampScroll() {
  const float max_scroll =
      std::max(0.f, ContentHeight() - dropdown_height);
  scroll_offset_ = std::clamp(scroll_offset_, 0.f, max_scroll);
}

void Select::EnsureBuffer() {
  // Closed: box(5) + label(1) + arrow(1) = 7
  // Open extras: dropdown bg(1) + clip(1) + unclip(1) + items(V)*2 (bg+text)
  //              + scroll track(1) + thumb(1)
  const size_t v = static_cast<size_t>(MaxVisibleSlots());
  const size_t need = 7 + 3 + v * 2 + 2;
  if (draw_command_buffer_.size() < need) {
    draw_command_buffer_.resize(need);
  }
}

void Select::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  out_h = height;
}

void Select::handle_messages(const MouseEvents& mouse,
                             const KeyboardEvents&) {
  if (disabled) {
    state = ComponentState::Base;
    open_ = false;
    dragging_scroll_ = false;
    return;
  }

  const bool over_header = Hit(mouse.x, mouse.y, x, y, width, height);
  const float list_y = y + height;
  const float vp_w = ViewportWidth();
  const bool over_list =
      open_ && Hit(mouse.x, mouse.y, x, list_y, width, dropdown_height);
  const float bar_x = x + vp_w;
  const bool over_bar =
      open_ && ShowScroll() &&
      Hit(mouse.x, mouse.y, bar_x, list_y, scroll_y.style_base.thickness,
          dropdown_height);

  if (mouse.left_released) {
    dragging_scroll_ = false;
  }

  if (dragging_scroll_ && mouse.left_down && ShowScroll()) {
    scroll_state_ = ComponentState::Active;
    const float content = ContentHeight();
    const float track_len = dropdown_height;
    const float thickness = scroll_y.style_base.thickness;
    const float thumb_len =
        std::max(thickness, track_len * (dropdown_height / content));
    const float travel = std::max(0.f, track_len - thumb_len);
    const float max_offset = std::max(0.f, content - dropdown_height);
    if (travel > 0.f && max_offset > 0.f) {
      const float delta = mouse.y - drag_mouse_anchor_;
      scroll_offset_ = drag_scroll_anchor_ + (delta / travel) * max_offset;
      ClampScroll();
    }
    return;
  }

  if (mouse.left_pressed && over_bar && ContentHeight() > dropdown_height) {
    dragging_scroll_ = true;
    scroll_state_ = ComponentState::Active;
    drag_mouse_anchor_ = mouse.y;
    drag_scroll_anchor_ = scroll_offset_;
    return;
  }

  scroll_state_ = over_bar ? ComponentState::Hovered : ComponentState::Base;

  if (open_ && mouse.wheel_delta != 0.f && ShowScroll()) {
    scroll_offset_ -= mouse.wheel_delta * item_height;
    ClampScroll();
  }

  hovered_index_ = -1;
  if (open_ && over_list && !over_bar) {
    const float local_y = mouse.y - list_y + scroll_offset_;
    const int idx = static_cast<int>(local_y / item_height);
    if (idx >= 0 && idx < static_cast<int>(options.size())) {
      hovered_index_ = idx;
    }
  }

  if (mouse.left_pressed) {
    if (over_header) {
      open_ = !open_;
      if (open_) {
        ClampScroll();
      }
    } else if (open_ && over_list && hovered_index_ >= 0) {
      selected = hovered_index_;
      WriteBound();
      open_ = false;
      enqueue_event(on_click);
    } else if (!over_list && !over_bar) {
      open_ = false;
    }
  }

  if (open_ || over_header) {
    state = mouse.left_down && over_header ? ComponentState::Active
                                          : ComponentState::Hovered;
  } else {
    state = ComponentState::Base;
  }
}

void Select::build_draw_buffer() {
  EnsureBuffer();
  ClampScroll();

  write_box_commands(0, width, height);

  DrawCommand& label = draw_command_buffer_[5];
  label.type = DrawCommandType::Text;
  label.x = x + 10.f;
  label.y = y;
  label.width = width - 28.f;
  label.height = height;
  label.color = text_color;
  label.layer = layer;
  label.wrap = false;
  label.text_align = TextAlign::LeftMiddle;
  if (selected >= 0 && selected < static_cast<int>(options.size())) {
    label.text = options[static_cast<size_t>(selected)];
  } else {
    label.text = L"";
  }
  apply_font(label);

  DrawCommand& arrow = draw_command_buffer_[6];
  arrow.type = DrawCommandType::Text;
  arrow.x = x + width - 22.f;
  arrow.y = y;
  arrow.width = 16.f;
  arrow.height = height;
  arrow.color = text_color;
  arrow.layer = layer;
  arrow.text = open_ ? L"^" : L"v";
  arrow.wrap = false;
  arrow.text_align = TextAlign::Center;
  apply_font(arrow);

  const size_t n = options.size();
  const size_t base = 7;
  const int max_slots = MaxVisibleSlots();

  if (!open_) {
    visible_first_ = 0;
    visible_count_ = 0;
    for (size_t i = base; i < draw_command_buffer_.size(); ++i) {
      ClearCmd(draw_command_buffer_[i]);
    }
    return;
  }

  const int drop_layer = layer + 1000;
  const float list_y = y + height;
  const float vp_w = ViewportWidth();

  DrawCommand& drop_bg = draw_command_buffer_[base];
  drop_bg.type = DrawCommandType::Rect;
  drop_bg.x = x;
  drop_bg.y = list_y;
  drop_bg.width = width;
  drop_bg.height = dropdown_height;
  drop_bg.color = dropdown_bg;
  drop_bg.layer = drop_layer;
  drop_bg.text.clear();
  drop_bg.wrap = false;

  DrawCommand& clip = draw_command_buffer_[base + 1];
  clip.type = DrawCommandType::PushClip;
  clip.x = x;
  clip.y = list_y;
  clip.width = vp_w;
  clip.height = dropdown_height;
  clip.layer = drop_layer;
  clip.color = {};
  clip.text.clear();
  clip.wrap = false;

  if (item_height > 0.f && n > 0) {
    visible_first_ = static_cast<int>(scroll_offset_ / item_height);
    if (visible_first_ < 0) {
      visible_first_ = 0;
    }
    if (visible_first_ >= static_cast<int>(n)) {
      visible_first_ = static_cast<int>(n) - 1;
    }
    visible_count_ = std::min(max_slots, static_cast<int>(n) - visible_first_);
  } else {
    visible_first_ = 0;
    visible_count_ = 0;
  }

  for (int slot_i = 0; slot_i < max_slots; ++slot_i) {
    const size_t slot = base + 3 + static_cast<size_t>(slot_i) * 2;
    if (slot_i >= visible_count_) {
      ClearCmd(draw_command_buffer_[slot]);
      ClearCmd(draw_command_buffer_[slot + 1]);
      continue;
    }

    const int i = visible_first_ + slot_i;
    const float iy =
        list_y + static_cast<float>(i) * item_height - scroll_offset_;

    DrawCommand& item_bg = draw_command_buffer_[slot];
    item_bg.type = DrawCommandType::Rect;
    item_bg.x = x;
    item_bg.y = iy;
    item_bg.width = vp_w;
    item_bg.height = item_height;
    item_bg.layer = drop_layer;
    item_bg.text.clear();
    item_bg.wrap = false;
    if (i == hovered_index_ || i == selected) {
      item_bg.color = item_hover_color;
    } else {
      item_bg.color = {0.f, 0.f, 0.f, 0.f};
    }

    DrawCommand& item_text = draw_command_buffer_[slot + 1];
    item_text.type = DrawCommandType::Text;
    item_text.x = x + 10.f;
    item_text.y = iy;
    item_text.width = vp_w - 12.f;
    item_text.height = item_height;
    item_text.color = text_color;
    item_text.layer = drop_layer;
    item_text.text = options[static_cast<size_t>(i)];
    item_text.wrap = false;
    item_text.text_align = TextAlign::LeftMiddle;
    apply_font(item_text);
  }

  DrawCommand& unclip = draw_command_buffer_[base + 2];
  unclip.type = DrawCommandType::PopClip;
  unclip.layer = drop_layer;
  unclip.width = 0.f;
  unclip.height = 0.f;
  unclip.text.clear();

  const size_t scroll_base = base + 3 + static_cast<size_t>(max_slots) * 2;
  DrawCommand& track = draw_command_buffer_[scroll_base];
  DrawCommand& thumb = draw_command_buffer_[scroll_base + 1];

  if (!ShowScroll()) {
    ClearCmd(track);
    ClearCmd(thumb);
    return;
  }

  const ScrollbarStateStyle& st =
      (scroll_state_ == ComponentState::Active)
          ? scroll_y.style_active
          : (scroll_state_ == ComponentState::Hovered
                 ? scroll_y.style_hovered
                 : scroll_y.style_base);
  const float thickness = st.thickness;
  const float content = ContentHeight();

  track.type = DrawCommandType::Rect;
  track.x = x + vp_w;
  track.y = list_y;
  track.width = thickness;
  track.height = dropdown_height;
  track.color = st.track_color;
  track.layer = drop_layer;
  track.text.clear();
  track.wrap = false;

  if (content <= dropdown_height + 0.5f) {
    ClearCmd(thumb);
    return;
  }

  const float thumb_len =
      std::max(thickness, dropdown_height * (dropdown_height / content));
  const float max_offset = content - dropdown_height;
  const float travel = dropdown_height - thumb_len;
  const float thumb_pos =
      (max_offset > 0.f) ? (scroll_offset_ / max_offset) * travel : 0.f;

  thumb.type = DrawCommandType::Rect;
  thumb.x = track.x;
  thumb.y = list_y + thumb_pos;
  thumb.width = thickness;
  thumb.height = thumb_len;
  thumb.color = st.thumb_color;
  thumb.layer = drop_layer;
  thumb.text.clear();
  thumb.wrap = false;
}

void Select::collect_draw(std::vector<DrawCommand*>& out, bool force_rebuild) {
  if (force_rebuild || !static_draw) {
    build_draw_buffer();
    reset_draw_origin_after_build();
  }

  // Header only — dropdown is emitted in collect_overlay_draw (above parent clip).
  for (size_t i = 0; i < 7; ++i) {
    DrawCommand& cmd = draw_command_buffer_[i];
    if (cmd.type == DrawCommandType::Rect ||
        cmd.type == DrawCommandType::Circle ||
        cmd.type == DrawCommandType::RoundedRect) {
      if (cmd.width <= 0.f || cmd.height <= 0.f || cmd.color.a <= 0.f) {
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

void Select::collect_overlay_draw(std::vector<DrawCommand*>& out) {
  if (!open_ || draw_command_buffer_.size() < 10) {
    return;
  }

  const size_t base = 7;
  const int max_slots = MaxVisibleSlots();

  out.push_back(&draw_command_buffer_[base]);      // drop bg
  out.push_back(&draw_command_buffer_[base + 1]);  // clip

  for (int slot_i = 0; slot_i < visible_count_; ++slot_i) {
    const size_t slot = base + 3 + static_cast<size_t>(slot_i) * 2;
    DrawCommand& item_bg = draw_command_buffer_[slot];
    if (item_bg.color.a > 0.f && item_bg.width > 0.f) {
      out.push_back(&item_bg);
    }
    DrawCommand& item_text = draw_command_buffer_[slot + 1];
    if (!item_text.text.empty()) {
      out.push_back(&item_text);
    }
  }

  out.push_back(&draw_command_buffer_[base + 2]);  // unclip

  const size_t scroll_base = base + 3 + static_cast<size_t>(max_slots) * 2;
  if (scroll_base + 1 < draw_command_buffer_.size()) {
    DrawCommand& track = draw_command_buffer_[scroll_base];
    DrawCommand& thumb = draw_command_buffer_[scroll_base + 1];
    if (track.width > 0.f && track.height > 0.f && track.color.a > 0.f) {
      out.push_back(&track);
    }
    if (thumb.width > 0.f && thumb.height > 0.f && thumb.color.a > 0.f) {
      out.push_back(&thumb);
    }
  }
}
