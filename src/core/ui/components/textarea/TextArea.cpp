#include "TextArea.h"

#include <algorithm>
#include <cstring>
#include <windows.h>

namespace {

constexpr size_t kMaxSelRects = 48;
constexpr size_t kClip = 5;
constexpr size_t kSelBase = 6;
constexpr size_t kText = kSelBase + kMaxSelRects;
constexpr size_t kCaret = kText + 1;
constexpr size_t kUnclip = kCaret + 1;
constexpr size_t kTrack = kUnclip + 1;
constexpr size_t kThumb = kTrack + 1;
constexpr size_t kBufCount = kThumb + 1;
constexpr float kAvgGlyph = 8.5f;

bool Hit(float px, float py, float x, float y, float w, float h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

bool CopyUnicodeToClipboard(HWND hwnd, const std::wstring& text) {
  if (!OpenClipboard(hwnd)) {
    return false;
  }
  EmptyClipboard();
  const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!mem) {
    CloseClipboard();
    return false;
  }
  void* locked = GlobalLock(mem);
  if (!locked) {
    GlobalFree(mem);
    CloseClipboard();
    return false;
  }
  std::memcpy(locked, text.c_str(), bytes);
  GlobalUnlock(mem);
  if (!SetClipboardData(CF_UNICODETEXT, mem)) {
    GlobalFree(mem);
    CloseClipboard();
    return false;
  }
  CloseClipboard();
  return true;
}

}  // namespace

TextArea::TextArea() {
  const TextAreaStylePreset& p = ui_get_style_presets().text_area;
  width = p.width;
  height = p.height;
  padding = p.padding;
  line_height = p.line_height;
  text_color = p.text_color;
  placeholder_color = p.placeholder_color;
  caret_color = p.caret_color;
  selection_color = p.selection_color;
  caret_blink_period = p.caret_blink_period;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  ui_apply_scrollbar_preset(scroll_y, p.scroll_y);
  draw_command_buffer_.resize(kBufCount);
}

TextArea::~TextArea() = default;

bool TextArea::captures_input() const {
  return focused;
}

void TextArea::bind_data(std::wstring* data) {
  bound_ = data;
  if (bound_) {
    text = *bound_;
    caret_ = text.size();
    sel_anchor_ = caret_;
  }
}

void TextArea::WriteBound() {
  if (bound_) {
    *bound_ = text;
  }
}

void TextArea::EnsureBuffer() {
  if (draw_command_buffer_.size() < kBufCount) {
    draw_command_buffer_.resize(kBufCount);
  }
}

int TextArea::LineCount() const {
  int lines = 1;
  for (wchar_t ch : text) {
    if (ch == L'\n') {
      ++lines;
    }
  }
  return lines;
}

void TextArea::ClampScroll() {
  const float rh = RowHeight();
  const float content = static_cast<float>(LineCount()) * rh;
  const float view = std::max(0.f, height - padding * 2.f);
  scroll_offset_ = std::clamp(scroll_offset_, 0.f, std::max(0.f, content - view));
}

void TextArea::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  out_h = height;
}

float TextArea::MeasureTextWidth(const std::wstring& s) const {
  return MeasureTextWidth(s.c_str(), s.size());
}

float TextArea::MeasureTextWidth(const wchar_t* text, size_t length) const {
  if (!text || length == 0) {
    return 0.f;
  }
  if (ui) {
    return ui_measure_text_n(ui, text, length, effective_font_size(), font);
  }
  return static_cast<float>(length) * kAvgGlyph *
         (effective_font_size() / 18.f);
}

size_t TextArea::IndexAtX(const wchar_t* text, size_t length,
                          float local_x) const {
  if (!text || length == 0) {
    return 0;
  }
  if (ui) {
    return ui_text_index_at_x_n(ui, text, length, local_x,
                                effective_font_size(), font);
  }
  if (local_x <= 0.f) {
    return 0;
  }
  const float glyph = kAvgGlyph * (effective_font_size() / 18.f);
  size_t col = static_cast<size_t>(local_x / glyph + 0.5f);
  if (col > length) {
    col = length;
  }
  return col;
}

float TextArea::RowHeight() const {
  // Prefer atlas metrics so caret/selection stay aligned with rendered text.
  if (ui) {
    const float h = ui_font_line_height(ui, effective_font_size(), font);
    if (h > 0.f) {
      return h;
    }
  }
  if (line_height > 0.f) {
    return line_height;
  }
  return effective_font_size() * 1.2f;
}

bool TextArea::HasSelection() const {
  return sel_anchor_ != caret_;
}

size_t TextArea::SelMin() const {
  return std::min(sel_anchor_, caret_);
}

size_t TextArea::SelMax() const {
  return std::max(sel_anchor_, caret_);
}

void TextArea::ClearSelection() {
  sel_anchor_ = caret_;
}

void TextArea::LineBounds(int line, size_t& out_start, size_t& out_end) const {
  int cur = 0;
  out_start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (cur == line) {
      out_start = i;
      break;
    }
    if (text[i] == L'\n') {
      ++cur;
      out_start = i + 1;
    }
  }
  if (cur < line) {
    out_start = text.size();
    out_end = text.size();
    return;
  }
  out_end = text.size();
  for (size_t i = out_start; i < text.size(); ++i) {
    if (text[i] == L'\n') {
      out_end = i;
      return;
    }
  }
}

void TextArea::IndexToLineCol(size_t index, int& out_line,
                              size_t& out_col) const {
  out_line = 0;
  out_col = 0;
  size_t line_start = 0;
  const size_t n = std::min(index, text.size());
  for (size_t i = 0; i < n; ++i) {
    if (text[i] == L'\n') {
      ++out_line;
      line_start = i + 1;
    }
  }
  out_col = n - line_start;
}

size_t TextArea::LineColToIndex(int line, size_t col) const {
  size_t start = 0;
  size_t end = 0;
  LineBounds(line, start, end);
  if (col > end - start) {
    col = end - start;
  }
  return start + col;
}

size_t TextArea::IndexAtLocal(float local_x, float local_y) const {
  const float rh = RowHeight();
  const float y_content = local_y + scroll_offset_;
  int target_line = static_cast<int>(y_content / rh);
  if (target_line < 0) {
    target_line = 0;
  }
  const int lines = LineCount();
  if (target_line >= lines) {
    target_line = lines - 1;
  }

  size_t line_start = 0;
  size_t line_end = 0;
  LineBounds(target_line, line_start, line_end);
  const size_t col =
      IndexAtX(text.c_str() + line_start, line_end - line_start, local_x);
  return line_start + col;
}

void TextArea::MoveCaretTo(size_t index, bool extend_selection) {
  if (index > text.size()) {
    index = text.size();
  }
  caret_ = index;
  if (!extend_selection) {
    sel_anchor_ = caret_;
  }
  caret_blink_ = 0.f;
  caret_visible_ = true;
  EnsureCaretVisible();
}

void TextArea::PlaceCaretAt(float local_x, float local_y,
                            bool extend_selection) {
  prefer_x_valid_ = false;
  MoveCaretTo(IndexAtLocal(local_x, local_y), extend_selection);
}

void TextArea::MoveVertical(int delta_lines, bool extend_selection) {
  int line = 0;
  size_t col = 0;
  IndexToLineCol(caret_, line, col);
  size_t line_start = 0;
  size_t line_end = 0;
  LineBounds(line, line_start, line_end);

  if (!prefer_x_valid_) {
    preferred_x_ =
        MeasureTextWidth(text.c_str() + line_start, caret_ - line_start);
    prefer_x_valid_ = true;
  }

  int new_line = line + delta_lines;
  const int max_line = LineCount() - 1;
  if (new_line < 0) {
    new_line = 0;
  } else if (new_line > max_line) {
    new_line = max_line;
  }

  LineBounds(new_line, line_start, line_end);
  const size_t new_col =
      IndexAtX(text.c_str() + line_start, line_end - line_start, preferred_x_);
  MoveCaretTo(line_start + new_col, extend_selection);
}

void TextArea::EnsureCaretVisible() {
  int line = 0;
  size_t col = 0;
  IndexToLineCol(caret_, line, col);
  const float rh = RowHeight();
  const float view_h = std::max(0.f, height - padding * 2.f);
  const float caret_y = static_cast<float>(line) * rh;
  if (caret_y < scroll_offset_) {
    scroll_offset_ = caret_y;
  } else if (caret_y + rh > scroll_offset_ + view_h) {
    scroll_offset_ = caret_y + rh - view_h;
  }
  ClampScroll();
}

bool TextArea::DeleteSelection() {
  if (!HasSelection()) {
    return false;
  }
  const size_t a = SelMin();
  const size_t b = SelMax();
  text.erase(a, b - a);
  caret_ = a;
  sel_anchor_ = a;
  prefer_x_valid_ = false;
  return true;
}

bool TextArea::CopySelection() const {
  if (!HasSelection()) {
    return false;
  }
  const std::wstring selected = text.substr(SelMin(), SelMax() - SelMin());
  HWND hwnd = ui ? ui_get_hwnd(ui) : nullptr;
  return CopyUnicodeToClipboard(hwnd, selected);
}

void TextArea::update(float dt) {
  Component::update(dt);
  if (!focused) {
    caret_visible_ = false;
    dragging_select_ = false;
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

void TextArea::handle_messages(const MouseEvents& mouse,
                               const KeyboardEvents& keyboard) {
  if (disabled) {
    focused = false;
    state = ComponentState::Base;
    dragging_select_ = false;
    return;
  }

  const bool hovered = Hit(mouse.x, mouse.y, x, y, width, height);
  const bool shift = keyboard.down[VK_SHIFT];
  const bool ctrl = keyboard.down[VK_CONTROL];

  if (mouse.left_released) {
    dragging_select_ = false;
  }

  if (mouse.left_pressed) {
    focused = hovered;
    if (focused) {
      dragging_select_ = true;
      PlaceCaretAt(mouse.x - x - padding, mouse.y - y - padding, shift);
    } else {
      ClearSelection();
    }
  } else if (dragging_select_ && mouse.left_down && focused) {
    PlaceCaretAt(mouse.x - x - padding, mouse.y - y - padding, true);
  }

  if (focused) {
    state = ComponentState::Active;
  } else if (hovered) {
    state = ComponentState::Hovered;
  } else {
    state = ComponentState::Base;
  }

  if (focused && mouse.wheel_delta != 0.f) {
    scroll_offset_ -= mouse.wheel_delta * RowHeight();
    ClampScroll();
  }

  if (!focused) {
    return;
  }

  bool changed = false;

  if (ctrl && keyboard.pressed['C']) {
    CopySelection();
  }
  if (ctrl && keyboard.pressed['A']) {
    sel_anchor_ = 0;
    caret_ = text.size();
    prefer_x_valid_ = false;
    caret_blink_ = 0.f;
    caret_visible_ = true;
  }
  if (ctrl && keyboard.pressed['X']) {
    if (CopySelection() && DeleteSelection()) {
      changed = true;
    }
  }

  if (keyboard.pressed[VK_RETURN]) {
    DeleteSelection();
    text.insert(caret_, 1, L'\n');
    ++caret_;
    ClearSelection();
    prefer_x_valid_ = false;
    changed = true;
  }

  if (keyboard.pressed[VK_BACK]) {
    if (HasSelection()) {
      DeleteSelection();
      changed = true;
    } else if (caret_ > 0) {
      text.erase(caret_ - 1, 1);
      --caret_;
      ClearSelection();
      prefer_x_valid_ = false;
      changed = true;
    }
  }
  if (keyboard.pressed[VK_DELETE]) {
    if (HasSelection()) {
      DeleteSelection();
      changed = true;
    } else if (caret_ < text.size()) {
      text.erase(caret_, 1);
      ClearSelection();
      prefer_x_valid_ = false;
      changed = true;
    }
  }

  if (keyboard.pressed[VK_LEFT]) {
    prefer_x_valid_ = false;
    if (!shift && HasSelection()) {
      MoveCaretTo(SelMin(), false);
    } else if (caret_ > 0) {
      MoveCaretTo(caret_ - 1, shift);
    } else if (!shift) {
      ClearSelection();
    }
  }
  if (keyboard.pressed[VK_RIGHT]) {
    prefer_x_valid_ = false;
    if (!shift && HasSelection()) {
      MoveCaretTo(SelMax(), false);
    } else if (caret_ < text.size()) {
      MoveCaretTo(caret_ + 1, shift);
    } else if (!shift) {
      ClearSelection();
    }
  }
  if (keyboard.pressed[VK_UP]) {
    MoveVertical(-1, shift);
  }
  if (keyboard.pressed[VK_DOWN]) {
    MoveVertical(1, shift);
  }
  if (keyboard.pressed[VK_HOME]) {
    prefer_x_valid_ = false;
    int line = 0;
    size_t col = 0;
    IndexToLineCol(caret_, line, col);
    size_t start = 0;
    size_t end = 0;
    LineBounds(line, start, end);
    MoveCaretTo(ctrl ? 0 : start, shift);
  }
  if (keyboard.pressed[VK_END]) {
    prefer_x_valid_ = false;
    int line = 0;
    size_t col = 0;
    IndexToLineCol(caret_, line, col);
    size_t start = 0;
    size_t end = 0;
    LineBounds(line, start, end);
    MoveCaretTo(ctrl ? text.size() : end, shift);
  }

  for (int i = 0; i < keyboard.char_count; ++i) {
    const wchar_t ch = keyboard.chars[i];
    if (ch < 32) {
      continue;
    }
    // Skip Ctrl+letter control codes (1..26) already filtered by < 32.
    DeleteSelection();
    text.insert(caret_, 1, ch);
    ++caret_;
    ClearSelection();
    prefer_x_valid_ = false;
    changed = true;
  }

  if (changed) {
    caret_blink_ = 0.f;
    caret_visible_ = true;
    WriteBound();
    EnsureCaretVisible();
  }
}

void TextArea::build_draw_buffer() {
  EnsureBuffer();
  ClampScroll();
  write_box_commands(0, width, height);

  const float view_w = width - padding * 2.f -
                       (scroll_y.mode != ScrollMode::Hidden ? 12.f : 0.f);
  const float view_h = height - padding * 2.f;

  DrawCommand& clip = draw_command_buffer_[kClip];
  clip.type = DrawCommandType::PushClip;
  clip.x = x + padding;
  clip.y = y + padding;
  clip.width = view_w;
  clip.height = view_h;
  clip.layer = layer;

  for (size_t i = 0; i < kMaxSelRects; ++i) {
    DrawCommand& s = draw_command_buffer_[kSelBase + i];
    s.type = DrawCommandType::Rect;
    s.width = 0.f;
    s.height = 0.f;
    s.color.a = 0.f;
  }

  if (HasSelection()) {
    int line_a = 0;
    int line_b = 0;
    size_t col_a = 0;
    size_t col_b = 0;
    IndexToLineCol(SelMin(), line_a, col_a);
    IndexToLineCol(SelMax(), line_b, col_b);
    const float rh = RowHeight();
    const int lines = LineCount();
    const int first_vis =
        std::max(0, static_cast<int>(scroll_offset_ / rh) - 1);
    const int last_vis = std::min(
        lines - 1,
        static_cast<int>((scroll_offset_ + view_h) / rh) + 1);

    size_t sel_slot = 0;
    for (int line = line_a; line <= line_b && sel_slot < kMaxSelRects; ++line) {
      if (line < first_vis || line > last_vis) {
        continue;
      }
      size_t ls = 0;
      size_t le = 0;
      LineBounds(line, ls, le);
      const size_t seg_a = (line == line_a) ? SelMin() : ls;
      const size_t seg_b = (line == line_b) ? SelMax() : le;

      const float x0 =
          MeasureTextWidth(text.c_str() + ls, seg_a > ls ? seg_a - ls : 0);
      float x1 =
          MeasureTextWidth(text.c_str() + ls, seg_b > ls ? seg_b - ls : 0);
      if (x1 <= x0) {
        x1 = x0 + 4.f;
      }

      DrawCommand& s = draw_command_buffer_[kSelBase + sel_slot];
      s.type = DrawCommandType::Rect;
      s.x = x + padding + x0;
      s.y = y + padding - scroll_offset_ + static_cast<float>(line) * rh;
      s.width = x1 - x0;
      s.height = rh;
      s.color = selection_color;
      s.layer = layer;
      s.text.clear();
      ++sel_slot;
    }
  }

  const float rh = RowHeight();
  const float fs = effective_font_size();
  const int line_count = LineCount();
  int first_line = 0;
  int last_line = 0;
  if (rh > 0.f && line_count > 0) {
    first_line = std::max(0, static_cast<int>(scroll_offset_ / rh));
    last_line = std::min(
        line_count - 1,
        static_cast<int>((scroll_offset_ + view_h) / rh) + 1);
  }

  size_t vis_start = 0;
  size_t vis_end = 0;
  if (!text.empty()) {
    size_t unused = 0;
    LineBounds(first_line, vis_start, unused);
    LineBounds(last_line, unused, vis_end);
  }

  DrawCommand& text_cmd = draw_command_buffer_[kText];
  text_cmd.type = DrawCommandType::Text;
  text_cmd.x = x + padding;
  text_cmd.y = y + padding - scroll_offset_ + static_cast<float>(first_line) * rh;
  text_cmd.width = view_w;
  text_cmd.height =
      std::max(rh, static_cast<float>(last_line - first_line + 1) * rh);
  text_cmd.layer = layer;
  text_cmd.wrap = false;
  text_cmd.text_align = TextAlign::LeftTop;
  if (text.empty() && !focused && !placeholder.empty()) {
    text_cmd.text = placeholder;
    text_cmd.color = placeholder_color;
    text_cmd.y = y + padding - scroll_offset_;
  } else if (vis_end > vis_start) {
    text_cmd.text.assign(text.data() + vis_start, vis_end - vis_start);
    text_cmd.color = text_color;
  } else {
    text_cmd.text.clear();
    text_cmd.color = text_color;
  }
  apply_font(text_cmd);

  auto write_caret = [&](DrawCommand& caret) {
    int line = 0;
    size_t line_start = 0;
    for (size_t i = 0; i < caret_ && i < text.size(); ++i) {
      if (text[i] == L'\n') {
        ++line;
        line_start = i + 1;
      }
    }
    // Match Renderer LeftTop: line top = text_y + line * line_height,
    // baseline = line_top + ascent.
    const float text_top = y + padding - scroll_offset_;
    const float line_top = text_top + static_cast<float>(line) * rh;
    float ascent = fs * 0.8f;
    if (ui) {
      const float a = ui_font_ascent(ui, fs, font);
      if (a > 0.f) {
        ascent = a;
      }
    }
    const float caret_h =
        std::min(rh * 0.9f, std::max(fs * 0.75f, ascent * 0.85f));
    caret.type = DrawCommandType::Rect;
    caret.x =
        x + padding + MeasureTextWidth(text.c_str() + line_start,
                                       caret_ > line_start ? caret_ - line_start
                                                           : 0);
    caret.y = line_top + std::max(0.f, ascent - caret_h);
    caret.width = std::max(1.5f, fs * 0.08f);
    caret.height = caret_h;
    caret.color = caret_color;
    caret.layer = layer;
  };

  DrawCommand& caret = draw_command_buffer_[kCaret];
  if (focused && caret_visible_) {
    write_caret(caret);
  } else {
    caret.type = DrawCommandType::Rect;
    caret.width = 0.f;
    caret.height = 0.f;
    caret.color.a = 0.f;
  }

  DrawCommand& unclip = draw_command_buffer_[kUnclip];
  unclip.type = DrawCommandType::PopClip;
  unclip.layer = layer;

  const float content = static_cast<float>(LineCount()) * rh;
  DrawCommand& track = draw_command_buffer_[kTrack];
  DrawCommand& thumb = draw_command_buffer_[kThumb];
  if (scroll_y.mode != ScrollMode::Hidden && content > view_h + 0.5f) {
    const float thickness = scroll_y.style_base.thickness;
    track.type = DrawCommandType::Rect;
    track.x = x + width - thickness;
    track.y = y;
    track.width = thickness;
    track.height = height;
    track.color = scroll_y.style_base.track_color;
    track.layer = layer;

    const float thumb_len = std::max(thickness, height * (view_h / content));
    const float max_off = content - view_h;
    const float travel = height - thumb_len;
    const float thumb_pos =
        (max_off > 0.f) ? (scroll_offset_ / max_off) * travel : 0.f;
    thumb.type = DrawCommandType::Rect;
    thumb.x = track.x;
    thumb.y = y + thumb_pos;
    thumb.width = thickness;
    thumb.height = thumb_len;
    thumb.color = scroll_y.style_base.thumb_color;
    thumb.layer = layer;
  } else {
    track.width = 0.f;
    track.height = 0.f;
    track.color.a = 0.f;
    thumb.width = 0.f;
    thumb.height = 0.f;
    thumb.color.a = 0.f;
  }
}

void TextArea::collect_draw(std::vector<DrawCommand*>& out, bool force_rebuild) {
  if (force_rebuild || !static_draw) {
    build_draw_buffer();
    reset_draw_origin_after_build();
  }

  for (size_t i = 0; i < 5; ++i) {
    DrawCommand& cmd = draw_command_buffer_[i];
    if (cmd.width > 0.f && cmd.height > 0.f && cmd.color.a > 0.f) {
      out.push_back(&cmd);
    }
  }
  out.push_back(&draw_command_buffer_[kClip]);
  for (size_t i = 0; i < kMaxSelRects; ++i) {
    DrawCommand& s = draw_command_buffer_[kSelBase + i];
    if (s.width > 0.f && s.height > 0.f && s.color.a > 0.f) {
      out.push_back(&s);
    }
  }
  if (!draw_command_buffer_[kText].text.empty()) {
    out.push_back(&draw_command_buffer_[kText]);
  }
  DrawCommand& caret = draw_command_buffer_[kCaret];
  if (caret.width > 0.f && caret.height > 0.f) {
    out.push_back(&caret);
  }
  out.push_back(&draw_command_buffer_[kUnclip]);
  DrawCommand& track = draw_command_buffer_[kTrack];
  DrawCommand& thumb = draw_command_buffer_[kThumb];
  if (track.width > 0.f) {
    out.push_back(&track);
  }
  if (thumb.width > 0.f) {
    out.push_back(&thumb);
  }
}
