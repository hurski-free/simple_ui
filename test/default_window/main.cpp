#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "simple_ui.h"

namespace {

float EstimateWrappedHeight(UiContext* ctx, const std::wstring& text,
                            float width, float font_size) {
  const float line_h = std::max(1.f, ui_font_line_height(ctx, font_size));
  if (text.empty() || width <= 0.f) {
    return line_h;
  }

  int lines = 0;
  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    if (text[i] == L'\n') {
      ++lines;
      ++i;
      continue;
    }

    size_t line_end = text.find(L'\n', i);
    if (line_end == std::wstring::npos) {
      line_end = n;
    }

    float x = 0.f;
    size_t word_start = i;
    while (word_start < line_end) {
      while (word_start < line_end && text[word_start] == L' ') {
        x += ui_measure_text_n(ctx, L" ", 1, font_size);
        ++word_start;
      }
      if (word_start >= line_end) {
        break;
      }

      size_t word_end = word_start;
      while (word_end < line_end && text[word_end] != L' ') {
        ++word_end;
      }

      const float word_w =
          ui_measure_text_n(ctx, text.c_str() + word_start,
                            word_end - word_start, font_size);
      if (x > 0.f && x + word_w > width) {
        ++lines;
        x = 0.f;
      }
      x += word_w;
      word_start = word_end;
    }

    ++lines;
    i = line_end;
    if (i < n && text[i] == L'\n') {
      ++i;
    }
  }

  return static_cast<float>(std::max(1, lines)) * line_h;
}

struct ShowcaseLayout {
  UiContext* ctx = nullptr;
  std::vector<Component*>* children = nullptr;
  float screen_w = 1280.f;
  float pad = 24.f;
  float mid_gap = 20.f;
  float row_gap = 14.f;
  float section_gap = 28.f;
  float y = 20.f;

  float LeftWidth() const { return screen_w * 0.5f - pad - mid_gap * 0.5f; }
  float RightX() const { return screen_w * 0.5f + mid_gap * 0.5f; }

  void AddTitle(Label& title, const wchar_t* name) {
    title.text = name;
    title.font_size = 32.f;
    title.color = {1.f, 1.f, 1.f, 1.f};
    title.width = screen_w - pad * 2.f - 16.f;
    title.height = 40.f;
    title.text_align = TextAlign::LeftMiddle;
    title.x = pad;
    title.y = y;
    children->push_back(&title);
    y += title.height + 16.f;
  }

  void AddRow(Text& desc, Component& widget, const wchar_t* mode_text) {
    desc.text = mode_text;
    desc.font_size = 16.f;
    desc.color = {0.82f, 0.85f, 0.92f, 1.f};
    desc.width = LeftWidth();
    const float desc_h =
        EstimateWrappedHeight(ctx, desc.text, desc.width, desc.font_size);
    desc.height = desc_h;
    desc.x = pad;
    desc.y = y;

    float widget_w = 0.f;
    float widget_h = 0.f;
    widget.get_layout_size(widget_w, widget_h);
    widget.x = RightX();
    widget.y = y;

    children->push_back(&desc);
    children->push_back(&widget);

    y += std::max(desc_h, widget_h) + row_gap;
  }

  void EndSection() { y += section_gap; }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  // Default theme (constructors copy presets at creation time).
  ui_set_style_presets(UiStylePresets{});

  const ScreenSettings screen{
      ScreenMode::Windowed,
      1280,
      720,
  };

  UiContext* ctx = ui_create(L"Default Components Showcase", &screen, nullptr);
  if (!ui_is_valid(ctx)) {
    MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
    return 1;
  }

  const float screen_w = static_cast<float>(ui_get_width(ctx));
  const float screen_h = static_cast<float>(ui_get_height(ctx));

  Scene main;

  ScrollView root;
  root.x = 0.f;
  root.y = 0.f;
  root.width = screen_w;
  root.height = screen_h;

  std::vector<Component*> items;
  ShowcaseLayout layout;
  layout.ctx = ctx;
  layout.children = &items;
  layout.screen_w = screen_w;

  // Bound demo data.
  std::wstring input_text = L"Hello";
  std::wstring area_text =
      L"Line 1\nLine 2\nLine 3\nLine 4\nLine 5\nLine 6\nLine 7\nLine 8";
  std::wstring area_empty;
  bool check_a = true;
  bool check_b = false;
  bool check_c = true;
  bool toggle_a = false;
  bool toggle_b = true;
  int radio_v = 0;
  int radio_sq = 1;
  int radio_h = 0;
  int select_a = 0;
  int select_b = 2;
  float range_a = 40.f;
  float range_b = 65.f;
  float range_c = 25.f;
  float range_d = 50.f;
  float progress_a = 0.35f;
  float progress_b = 0.72f;

  // --- Button ---
  Label title_button;
  Text d_btn_default, d_btn_disabled, d_btn_border_out, d_btn_border_in;
  Button btn_default, btn_disabled, btn_border_out, btn_border_in;

  layout.AddTitle(title_button, L"Button");
  btn_default.text = L"Button";
  layout.AddRow(d_btn_default, btn_default, L"(default)");

  btn_disabled.text = L"Disabled";
  btn_disabled.disabled = true;
  layout.AddRow(d_btn_disabled, btn_disabled, L"disabled");

  btn_border_out.text = L"Border Out";
  btn_border_out.style_base.border = {2.f, BorderMode::Out, {0.9f, 0.95f, 1.f, 1.f}};
  btn_border_out.style_hovered = btn_border_out.style_base;
  btn_border_out.style_hovered.background_color = {0.32f, 0.52f, 0.82f, 1.f};
  btn_border_out.style_active = btn_border_out.style_base;
  layout.AddRow(d_btn_border_out, btn_border_out, L"BorderMode::Out");

  btn_border_in.text = L"Border In";
  btn_border_in.style_base.border = {2.f, BorderMode::In, {0.9f, 0.95f, 1.f, 1.f}};
  btn_border_in.style_hovered = btn_border_in.style_base;
  btn_border_in.style_hovered.background_color = {0.32f, 0.52f, 0.82f, 1.f};
  btn_border_in.style_active = btn_border_in.style_base;
  layout.AddRow(d_btn_border_in, btn_border_in, L"BorderMode::In");
  layout.EndSection();

  // --- Text ---
  Label title_text;
  Text d_text_default, d_text_wrap, d_text_outline;
  Text text_default, text_wrap, text_outline;

  layout.AddTitle(title_text, L"Text");
  text_default.text = L"Single-line sample";
  text_default.width = 280.f;
  layout.AddRow(d_text_default, text_default, L"(default)");

  text_wrap.text =
      L"Wrapped multi-line text that should break across several lines "
      L"within the component width.";
  text_wrap.width = 280.f;
  text_wrap.height =
      EstimateWrappedHeight(ctx, text_wrap.text, text_wrap.width,
                            text_wrap.font_size);
  layout.AddRow(d_text_wrap, text_wrap, L"wrap within width");

  text_outline.text = L"Outlined text sample";
  text_outline.width = 280.f;
  text_outline.color = {1.f, 0.95f, 0.45f, 1.f};
  text_outline.outline = {2.f, {1.f, 1.f, 1.f, 1.f}};
  layout.AddRow(d_text_outline, text_outline, L"outline thickness + color");
  layout.EndSection();

  // --- Label ---
  Label title_label;
  Text d_label_default, d_label_center, d_label_left_top, d_label_outline;
  Label label_default, label_center, label_left_top, label_outline;

  layout.AddTitle(title_label, L"Label");
  label_default.text = L"LeftMiddle";
  label_default.width = 220.f;
  label_default.height = 28.f;
  label_default.style_base.background_color = {0.18f, 0.2f, 0.28f, 1.f};
  label_default.style_hovered = label_default.style_base;
  label_default.style_active = label_default.style_base;
  layout.AddRow(d_label_default, label_default, L"(default) TextAlign::LeftMiddle");

  label_center.text = L"Center";
  label_center.width = 220.f;
  label_center.height = 28.f;
  label_center.text_align = TextAlign::Center;
  label_center.style_base.background_color = {0.18f, 0.2f, 0.28f, 1.f};
  label_center.style_hovered = label_center.style_base;
  label_center.style_active = label_center.style_base;
  layout.AddRow(d_label_center, label_center, L"TextAlign::Center");

  label_left_top.text = L"LeftTop";
  label_left_top.width = 220.f;
  label_left_top.height = 28.f;
  label_left_top.text_align = TextAlign::LeftTop;
  label_left_top.style_base.background_color = {0.18f, 0.2f, 0.28f, 1.f};
  label_left_top.style_hovered = label_left_top.style_base;
  label_left_top.style_active = label_left_top.style_base;
  layout.AddRow(d_label_left_top, label_left_top, L"TextAlign::LeftTop");

  label_outline.text = L"Outline";
  label_outline.width = 220.f;
  label_outline.height = 28.f;
  label_outline.text_align = TextAlign::Center;
  label_outline.color = {1.f, 1.f, 1.f, 1.f};
  label_outline.outline = {2.f, {0.85f, 0.25f, 0.2f, 1.f}};
  label_outline.style_base.background_color = {0.18f, 0.2f, 0.28f, 1.f};
  label_outline.style_hovered = label_outline.style_base;
  label_outline.style_active = label_outline.style_base;
  layout.AddRow(d_label_outline, label_outline, L"outline thickness + color");
  layout.EndSection();

  // --- Input ---
  Label title_input;
  Text d_input_default, d_input_filled, d_input_disabled;
  Input input_default, input_filled, input_disabled;

  layout.AddTitle(title_input, L"Input");
  input_default.placeholder = L"Placeholder";
  layout.AddRow(d_input_default, input_default, L"(default) empty + placeholder");

  input_filled.bind_data(&input_text);
  layout.AddRow(d_input_filled, input_filled, L"bound text");

  input_disabled.text = L"Read only";
  input_disabled.disabled = true;
  layout.AddRow(d_input_disabled, input_disabled, L"disabled");
  layout.EndSection();

  // --- TextArea ---
  Label title_textarea;
  Text d_area_default, d_area_scroll;
  TextArea area_default, area_scroll;

  layout.AddTitle(title_textarea, L"TextArea");
  area_default.placeholder = L"Type here...";
  area_default.bind_data(&area_empty);
  layout.AddRow(d_area_default, area_default, L"(default) placeholder");

  area_scroll.bind_data(&area_text);
  layout.AddRow(d_area_scroll, area_scroll, L"multi-line + ScrollMode::Auto");
  layout.EndSection();

  // --- Select ---
  Label title_select;
  Text d_select_default, d_select_many;
  Select select_default, select_many;

  layout.AddTitle(title_select, L"Select");
  select_default.options = {L"Option A", L"Option B", L"Option C"};
  select_default.bind_data(&select_a);
  layout.AddRow(d_select_default, select_default, L"(default)");

  select_many.options = {L"One",   L"Two",  L"Three", L"Four", L"Five",
                         L"Six",   L"Seven", L"Eight", L"Nine", L"Ten"};
  select_many.bind_data(&select_b);
  layout.AddRow(d_select_many, select_many, L"many options (dropdown scroll)");
  layout.EndSection();

  // --- Checkbox ---
  Label title_checkbox;
  Text d_check_default, d_check_mark, d_check_off, d_check_disabled;
  Checkbox check_default, check_mark, check_off, check_disabled;

  layout.AddTitle(title_checkbox, L"Checkbox");
  check_default.label = L"Square";
  check_default.bind_data(&check_a);
  layout.AddRow(d_check_default, check_default,
                L"(default) CheckMarkKind::Square");

  check_mark.label = L"Checkmark";
  check_mark.check_kind = CheckMarkKind::Checkmark;
  check_mark.bind_data(&check_c);
  layout.AddRow(d_check_mark, check_mark, L"CheckMarkKind::Checkmark");

  check_off.label = L"Unchecked";
  check_off.bind_data(&check_b);
  layout.AddRow(d_check_off, check_off, L"unchecked");

  check_disabled.label = L"Disabled";
  check_disabled.checked = true;
  check_disabled.disabled = true;
  layout.AddRow(d_check_disabled, check_disabled, L"disabled");
  layout.EndSection();

  // --- RadioGroup ---
  Label title_radio;
  Text d_radio_default, d_radio_square, d_radio_horizontal;
  RadioGroup radio_default, radio_square, radio_horizontal;

  layout.AddTitle(title_radio, L"RadioGroup");
  radio_default.options = {L"First", L"Second", L"Third"};
  radio_default.bind_data(&radio_v);
  layout.AddRow(d_radio_default, radio_default,
                L"(default) RadioMode::Circle, Vertical");

  radio_square.options = {L"Alpha", L"Beta", L"Gamma"};
  radio_square.mode = RadioMode::Square;
  radio_square.bind_data(&radio_sq);
  layout.AddRow(d_radio_square, radio_square, L"RadioMode::Square, Vertical");

  radio_horizontal.options = {L"Low", L"Mid", L"High"};
  radio_horizontal.orientation = RadioOrientation::Horizontal;
  radio_horizontal.bind_data(&radio_h);
  layout.AddRow(d_radio_horizontal, radio_horizontal,
                L"RadioOrientation::Horizontal");
  layout.EndSection();

  // --- Toggle ---
  Label title_toggle;
  Text d_toggle_default, d_toggle_on, d_toggle_label, d_toggle_disabled;
  Toggle toggle_default, toggle_on, toggle_label, toggle_disabled;

  layout.AddTitle(title_toggle, L"Toggle");
  toggle_default.bind_data(&toggle_a);
  layout.AddRow(d_toggle_default, toggle_default, L"(default) off");

  toggle_on.bind_data(&toggle_b);
  layout.AddRow(d_toggle_on, toggle_on, L"on");

  toggle_label.label = L"Enable feature";
  layout.AddRow(d_toggle_label, toggle_label, L"with label");

  toggle_disabled.checked = true;
  toggle_disabled.disabled = true;
  toggle_disabled.label = L"Disabled";
  layout.AddRow(d_toggle_disabled, toggle_disabled, L"disabled");
  layout.EndSection();

  // --- Range ---
  Label title_range;
  Text d_range_default, d_range_value, d_range_label, d_range_ticks;
  Range range_default, range_value, range_label, range_ticks;

  layout.AddTitle(title_range, L"Range");
  range_default.bind_data(&range_a);
  // Clear left label so track-only default is visible.
  range_default.text.clear();
  layout.AddRow(d_range_default, range_default, L"(default) track only");

  range_value.show_value = true;
  range_value.text.clear();
  range_value.bind_data(&range_b);
  layout.AddRow(d_range_value, range_value, L"show_value");

  range_label.text = L"Volume";
  range_label.show_value = true;
  range_label.bind_data(&range_c);
  layout.AddRow(d_range_label, range_label, L"left caption + value box");

  range_ticks.text.clear();
  range_ticks.show_value = true;
  range_ticks.tick_labels = {{0.f, L"0"}, {50.f, L"50"}, {100.f, L"100"}};
  range_ticks.bind_data(&range_d);
  layout.AddRow(d_range_ticks, range_ticks, L"tick_labels under track");
  layout.EndSection();

  // --- ProgressBar ---
  Label title_progress;
  Text d_progress_default, d_progress_percent, d_progress_full;
  ProgressBar progress_default, progress_percent, progress_full;

  layout.AddTitle(title_progress, L"ProgressBar");
  progress_default.bind_data(&progress_a);
  layout.AddRow(d_progress_default, progress_default, L"(default)");

  progress_percent.show_percent = true;
  progress_percent.bind_data(&progress_b);
  layout.AddRow(d_progress_percent, progress_percent, L"show_percent");

  progress_full.value = 1.f;
  progress_full.show_percent = true;
  layout.AddRow(d_progress_full, progress_full, L"value = max");
  layout.EndSection();

  // --- Image ---
  Label title_image;
  Text d_image_default, d_image_tint;
  Image image_default, image_tint;

  std::vector<unsigned char> pixels(64 * 64 * 4);
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const size_t i = static_cast<size_t>(y * 64 + x) * 4;
      const bool dark = ((x / 8) + (y / 8)) % 2 == 0;
      pixels[i + 0] = dark ? 40 : 200;
      pixels[i + 1] = dark ? 80 : 210;
      pixels[i + 2] = dark ? 140 : 230;
      pixels[i + 3] = 255;
    }
  }
  const int tex_id = ui_create_texture(ctx, 64, 64, pixels.data());

  layout.AddTitle(title_image, L"Image");
  image_default.texture_id = tex_id;
  layout.AddRow(d_image_default, image_default, L"(default) tint white");

  image_tint.texture_id = tex_id;
  image_tint.tint = {1.f, 0.55f, 0.35f, 1.f};
  layout.AddRow(d_image_tint, image_tint, L"custom tint");
  layout.EndSection();

  // --- Container ---
  Label title_container;
  Text d_container_default;
  Container container_demo;
  Label container_child_a, container_child_b;

  layout.AddTitle(title_container, L"Container");
  container_demo.width = 320.f;
  container_demo.height = 120.f;
  container_demo.style_base.background_color = {0.12f, 0.14f, 0.2f, 1.f};
  container_demo.style_base.border = {
      1.5f, BorderMode::In, {0.55f, 0.6f, 0.75f, 1.f}};
  container_demo.style_hovered = container_demo.style_base;
  container_demo.style_active = container_demo.style_base;

  container_child_a.text = L"Child A";
  container_child_a.x = 12.f;
  container_child_a.y = 16.f;
  container_child_a.width = 120.f;
  container_child_a.height = 24.f;

  container_child_b.text = L"Child B";
  container_child_b.x = 12.f;
  container_child_b.y = 52.f;
  container_child_b.width = 120.f;
  container_child_b.height = 24.f;

  container_demo.components = {&container_child_a, &container_child_b};
  layout.AddRow(d_container_default, container_demo,
                L"(default) fixed size, children relative");
  layout.EndSection();

  // --- ScrollView (nested) ---
  Label title_scrollview;
  Text d_scroll_nested;
  ScrollView nested_scroll;
  Label nested_line_1, nested_line_2, nested_line_3, nested_line_4, nested_line_5;

  layout.AddTitle(title_scrollview, L"ScrollView");
  nested_scroll.width = 320.f;
  nested_scroll.height = 110.f;
  nested_scroll.style_base.background_color = {0.12f, 0.14f, 0.2f, 1.f};
  nested_scroll.style_hovered = nested_scroll.style_base;
  nested_scroll.style_active = nested_scroll.style_base;

  auto place_nested_label = [](Label& label, const wchar_t* text, float y) {
    label.text = text;
    label.x = 12.f;
    label.y = y;
    label.width = 260.f;
    label.height = 24.f;
  };
  place_nested_label(nested_line_1, L"Nested row 1", 8.f);
  place_nested_label(nested_line_2, L"Nested row 2", 40.f);
  place_nested_label(nested_line_3, L"Nested row 3", 72.f);
  place_nested_label(nested_line_4, L"Nested row 4", 104.f);
  place_nested_label(nested_line_5, L"Nested row 5", 136.f);
  nested_scroll.components = {&nested_line_1, &nested_line_2, &nested_line_3,
                              &nested_line_4, &nested_line_5};
  layout.AddRow(d_scroll_nested, nested_scroll,
                L"nested ScrollView, scroll_y = Auto");
  layout.EndSection();

  // --- Panel ---
  Label title_panel;
  Text d_panel_default, d_panel_static;
  Panel panel_default, panel_static;
  Label panel_body, panel_static_body;

  layout.AddTitle(title_panel, L"Panel");
  panel_default.width = 360.f;
  panel_default.height = 140.f;
  panel_default.title = L"Draggable panel";
  panel_body.text = L"Child content inside panel";
  panel_body.x = 16.f;
  panel_body.y = 20.f;
  panel_body.width = 300.f;
  panel_body.height = 28.f;
  panel_default.components = {&panel_body};
  layout.AddRow(d_panel_default, panel_default, L"(default) draggable title bar");

  panel_static.width = 360.f;
  panel_static.height = 120.f;
  panel_static.title = L"Static panel";
  panel_static.draggable = false;
  panel_static_body.text = L"draggable = false";
  panel_static_body.x = 16.f;
  panel_static_body.y = 16.f;
  panel_static_body.width = 300.f;
  panel_static_body.height = 28.f;
  panel_static.components = {&panel_static_body};
  layout.AddRow(d_panel_static, panel_static, L"draggable = false");
  layout.EndSection();

  // --- Modal ---
  Label title_modal;
  Text d_modal_default;
  Button btn_open_modal;
  Modal demo_modal;
  Label modal_body;
  Button modal_ok;

  layout.AddTitle(title_modal, L"Modal");
  btn_open_modal.text = L"Open Modal";
  btn_open_modal.on_click = [&]() { demo_modal.open = true; };
  layout.AddRow(d_modal_default, btn_open_modal,
                L"(default) open via button; overlay + dialog");

  // Bottom spacer so last rows are not clipped by scrollbar.
  layout.y += 40.f;

  demo_modal.screen_width = screen_w;
  demo_modal.screen_height = screen_h;
  demo_modal.title = L"Modal dialog";
  demo_modal.width = 420.f;
  demo_modal.height = 200.f;
  modal_body.text = L"Default ModalStylePreset content";
  modal_body.x = 24.f;
  modal_body.y = 24.f;
  modal_body.width = 360.f;
  modal_body.height = 40.f;
  modal_ok.text = L"OK";
  modal_ok.x = (demo_modal.width - modal_ok.width) * 0.5f;
  modal_ok.y = 100.f;
  modal_ok.on_click = [&]() { demo_modal.open = false; };
  demo_modal.components = {&modal_body, &modal_ok};
  demo_modal.on_close = [&]() { demo_modal.open = false; };

  root.components = items;
  main.components = {&root, &demo_modal};
  main.prepare_scene();

  LARGE_INTEGER frequency{};
  LARGE_INTEGER last_time{};
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&last_time);

  while (ui_process_messages(ctx)) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const float dt =
        static_cast<float>(now.QuadPart - last_time.QuadPart) /
        static_cast<float>(frequency.QuadPart);
    last_time = now;

    main.handle_messages(ctx);
    main.update(dt);
    ui_clear(ctx, {0.06f, 0.07f, 0.1f, 1.f});
    main.draw(ctx);
    main.handle_events();
    ui_present(ctx);
  }

  ui_destroy(ctx);
  return 0;
}
