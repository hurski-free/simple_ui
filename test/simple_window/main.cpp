#include <windows.h>

#include "simple_ui.h"

namespace {

void StyleMenuButton(Button& button) {
  button.width = 220.f;
  button.height = 48.f;

  button.style_base.background_color = {0.18f, 0.32f, 0.55f, 1.f};
  button.style_base.border = {2.f, BorderMode::Out, {0.85f, 0.9f, 1.f, 1.f}};

  button.style_hovered = button.style_base;
  button.style_hovered.background_color = {0.28f, 0.45f, 0.72f, 1.f};

  button.style_active = button.style_base;
  button.style_active.background_color = {0.12f, 0.22f, 0.4f, 1.f};
  button.style_active.border = {0.f, BorderMode::None, {}};

  button.text_color = {1.f, 1.f, 1.f, 1.f};
  button.transition.background_duration = 0.15f;

  button.font_size = 32.f;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  const ScreenSettings screen{
      ScreenMode::Windowed,
      1280,
      720,
  };

  UiContext* ctx = ui_create(L"Simple Window", &screen, L"icon.ico");
  if (!ui_is_valid(ctx)) {
    MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
    return 1;
  }

  const float screen_w = static_cast<float>(ui_get_width(ctx));
  const float screen_h = static_cast<float>(ui_get_height(ctx));

  Scene main_menu;
  Scene authors;
  Scene settings;
  Scene* active_scene = &main_menu;

  // Bound data for widgets.
  std::wstring player_name = L"Player";
  int resolution_index = 1;
  float volume = 50.f;
  bool vsync_enabled = true;
  int quality_index = 1;

  // --- Main menu ---
  Button btn_start;
  Button btn_settings;
  Button btn_authors;
  Button btn_exit;
  Input name_input;
  Label fps_label;

  StyleMenuButton(btn_start);
  StyleMenuButton(btn_settings);
  StyleMenuButton(btn_authors);
  StyleMenuButton(btn_exit);

  btn_start.text = L"Start";
  btn_settings.text = L"Settings";
  btn_authors.text = L"Authors";
  btn_exit.text = L"Exit";

  name_input.width = 220.f;
  name_input.height = 36.f;
  name_input.placeholder = L"Player name";
  name_input.bind_data(&player_name);

  const float menu_gap = 14.f;
  const float menu_total_h =
      btn_start.height * 4.f + menu_gap * 4.f + name_input.height;
  float menu_y = (screen_h - menu_total_h) * 0.5f;
  const float menu_x = (screen_w - btn_start.width) * 0.5f;

  auto place_menu_button = [&](Button& b) {
    b.x = menu_x;
    b.y = menu_y;
    menu_y += b.height + menu_gap;
  };
  place_menu_button(btn_start);
  place_menu_button(btn_settings);
  place_menu_button(btn_authors);
  place_menu_button(btn_exit);

  name_input.x = menu_x;
  name_input.y = menu_y;

  fps_label.text = L"FPS: --";
  fps_label.width = 140.f;
  fps_label.height = 24.f;
  fps_label.x = 12.f;
  fps_label.y = 12.f;
  fps_label.color = {0.95f, 0.97f, 1.f, 1.f};
  fps_label.font_size = 18.f;

  btn_start.on_click = []() {};
  btn_settings.on_click = [&]() { active_scene = &settings; };
  btn_authors.on_click = [&]() { active_scene = &authors; };
  btn_exit.on_click = [ctx]() { PostQuitMessage(0); };

  main_menu.components = {&btn_start, &btn_settings, &btn_authors, &btn_exit,
                          &name_input, &fps_label};
  main_menu.prepare_scene();

  // --- Authors ---
  Container authors_box;
  authors_box.width = 420.f;
  authors_box.height = 360.f;
  authors_box.x = (screen_w - authors_box.width) * 0.5f;
  authors_box.y = (screen_h - authors_box.height) * 0.5f - 20.f;
  authors_box.style_base.background_color = {0.08f, 0.1f, 0.16f, 0.95f};
  authors_box.style_base.border = {2.f, BorderMode::Out, {0.7f, 0.8f, 1.f, 1.f}};
  authors_box.style_hovered = authors_box.style_base;
  authors_box.style_active = authors_box.style_base;
  authors_box.scroll_y.mode = ScrollMode::Auto;
  authors_box.place_mode = ScrollPlaceMode::In;

  Text authors_list;
  authors_list.x = 16.f;
  authors_list.y = 16.f;
  authors_list.width = authors_box.width - 44.f;
  authors_list.color = {0.92f, 0.94f, 1.f, 1.f};
  authors_list.text =
      L"Elena Morozova\n"
      L"Viktor Hale\n"
      L"Mira Solenne\n"
      L"Jonah Crowe\n"
      L"Aisha Renard\n"
      L"Theo Blackwood\n"
      L"Nadia Voss\n"
      L"Kai Nakamura\n"
      L"Liora Quinn\n"
      L"Sebastian Drake\n"
      L"Freya Lindholm\n"
      L"Omar Castillo\n"
      L"Ivy Marchand\n"
      L"Roman Petrov\n"
      L"Celeste Byrne\n"
      L"Darius Okonkwo\n"
      L"Sable Winters\n"
      L"Henrik Valen\n"
      L"Yuna Park\n"
      L"Cassian Rowe";
  authors_list.height = 0.f;
  {
    int lines = 1;
    for (wchar_t ch : authors_list.text) {
      if (ch == L'\n') {
        ++lines;
      }
    }
    authors_list.height = static_cast<float>(lines) * 28.f + 16.f;
  }

  authors_box.components.push_back(&authors_list);

  Button btn_back_authors;
  StyleMenuButton(btn_back_authors);
  btn_back_authors.width = 160.f;
  btn_back_authors.height = 40.f;
  btn_back_authors.text = L"Back";
  btn_back_authors.x = (screen_w - btn_back_authors.width) * 0.5f;
  btn_back_authors.y = authors_box.y + authors_box.height + 24.f;
  btn_back_authors.on_click = [&]() { active_scene = &main_menu; };

  authors.components = {&authors_box, &btn_back_authors};
  authors.prepare_scene();

  // --- Settings ---
  Text settings_title;
  settings_title.text = L"Settings";
  settings_title.width = 300.f;
  settings_title.height = 36.f;
  settings_title.color = {1.f, 1.f, 1.f, 1.f};
  settings_title.x = (screen_w - 300.f) * 0.5f;
  settings_title.y = 48.f;

  ScrollView settings_box;
  settings_box.width = 600.f;
  settings_box.height = 420.f;
  settings_box.x = (screen_w - settings_box.width) * 0.5f;
  settings_box.y = settings_title.y + 48.f;
  settings_box.style_base.background_color = {0.08f, 0.1f, 0.16f, 0.95f};
  settings_box.style_base.border = {2.f, BorderMode::Out, {0.7f, 0.8f, 1.f, 1.f}};
  settings_box.style_hovered = settings_box.style_base;
  settings_box.style_active = settings_box.style_base;
  settings_box.place_mode = ScrollPlaceMode::In;

  const float content_x = 24.f;
  float cy = 20.f;

  Select resolution;
  resolution.width = 350.f;
  resolution.height = 50.f;
  resolution.dropdown_height = 120.f;
  resolution.options = {L"1280 x 720", L"1600 x 900", L"1920 x 1080",
                        L"2560 x 1440", L"3840 x 2160"};
  resolution.bind_data(&resolution_index);
  resolution.x = content_x;
  resolution.y = cy;
  resolution.layer = 1;
  resolution.font_size = 24.f;
  cy += resolution.height + 24.f;

  Range volume_slider;
  volume_slider.width = 150.f;
  volume_slider.min_value = 0.f;
  volume_slider.max_value = 100.f;
  volume_slider.step = 5.f;
  volume_slider.text = L"Volume";
  volume_slider.label_width = 70.f;
  volume_slider.show_value = true;
  volume_slider.tick_labels = {{0.f, L"Min"}, {100.f, L"Max"}};
  volume_slider.bind_data(&volume);
  volume_slider.x = content_x;
  volume_slider.y = cy;

  float volume_w = 0.f;
  float volume_h = 0.f;
  volume_slider.get_layout_size(volume_w, volume_h);
  cy += volume_h + 20.f;

  Checkbox vsync;
  vsync.label = L"Enable VSync";
  vsync.bind_data(&vsync_enabled);
  vsync.x = content_x;
  vsync.y = cy;
  cy += 36.f;

  Checkbox accept_terms;
  bool terms_ok = false;
  accept_terms.label = L"Accept terms";
  accept_terms.check_kind = CheckMarkKind::Checkmark;
  accept_terms.bind_data(&terms_ok);
  accept_terms.x = content_x;
  accept_terms.y = cy;
  cy += 36.f;

  Toggle mute_toggle;
  bool muted = false;
  mute_toggle.label = L"Mute audio";
  mute_toggle.transition_duration = 0.25f;
  mute_toggle.bind_data(&muted);
  mute_toggle.x = content_x;
  mute_toggle.y = cy;
  cy += 40.f;

  ProgressBar load_bar;
  float load_progress = 0.65f;
  load_bar.width = 280.f;
  load_bar.show_percent = true;
  load_bar.bind_data(&load_progress);
  load_bar.x = content_x;
  load_bar.y = cy;
  cy += 40.f;

  Label notes_label;
  notes_label.text = L"Notes";
  notes_label.width = 120.f;
  notes_label.x = content_x;
  notes_label.y = cy;
  cy += 28.f;

  TextArea notes;
  std::wstring notes_data = L"Settings notes...";
  notes.width = 280.f;
  notes.height = 90.f;
  notes.bind_data(&notes_data);
  notes.x = content_x;
  notes.y = cy;
  notes.font_size = 24.f;
  cy += notes.height + 20.f;

  RadioGroup quality;
  quality.options = {L"Low", L"Medium", L"High"};
  quality.orientation = RadioOrientation::Horizontal;
  quality.item_width = 90.f;
  quality.bind_data(&quality_index);
  quality.x = content_x;
  quality.y = cy;
  cy += 56.f;

  settings_box.components = {&resolution,   &volume_slider, &vsync,
                             &accept_terms, &mute_toggle,   &load_bar,
                             &notes_label,  &notes,         &quality};

  Button btn_back_settings;
  StyleMenuButton(btn_back_settings);
  btn_back_settings.width = 140.f;
  btn_back_settings.height = 40.f;
  btn_back_settings.text = L"Back";
  btn_back_settings.y = settings_box.y + settings_box.height + 20.f;
  btn_back_settings.on_click = [&]() { active_scene = &main_menu; };

  // Demo modal
  Modal about_modal;
  about_modal.screen_width = screen_w;
  about_modal.screen_height = screen_h;
  about_modal.title = L"About";
  about_modal.width = 360.f;
  about_modal.height = 180.f;
  Label about_text;
  about_text.text = L"Simple UI demo settings";
  about_text.width = 320.f;
  about_text.height = 40.f;
  about_text.x = 20.f;
  about_text.y = 20.f;
  Button about_ok;
  StyleMenuButton(about_ok);
  about_ok.text = L"OK";
  about_ok.width = 100.f;
  about_ok.height = 36.f;
  about_ok.x = 130.f;
  about_ok.y = 90.f;
  about_ok.on_click = [&]() { about_modal.open = false; };
  about_modal.components = {&about_text, &about_ok};

  Button btn_about;
  StyleMenuButton(btn_about);
  btn_about.width = 140.f;
  btn_about.height = 40.f;
  btn_about.text = L"About";
  btn_about.y = btn_back_settings.y;
  btn_about.on_click = [&]() { about_modal.open = true; };

  const float footer_gap = 16.f;
  const float footer_total =
      btn_back_settings.width + footer_gap + btn_about.width;
  const float footer_x = (screen_w - footer_total) * 0.5f;
  btn_back_settings.x = footer_x;
  btn_about.x = footer_x + btn_back_settings.width + footer_gap;

  settings.components = {&settings_title, &settings_box, &btn_back_settings,
                         &btn_about, &about_modal};
  settings.prepare_scene();

  LARGE_INTEGER frequency{};
  LARGE_INTEGER last_time{};
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&last_time);

  float fps_accum_time = 0.f;
  int fps_accum_frames = 0;

  while (ui_process_messages(ctx)) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const float dt =
        static_cast<float>(now.QuadPart - last_time.QuadPart) /
        static_cast<float>(frequency.QuadPart);
    last_time = now;

    fps_accum_time += dt;
    ++fps_accum_frames;
    if (fps_accum_time >= 0.25f) {
      const int fps = static_cast<int>(
          static_cast<float>(fps_accum_frames) / fps_accum_time + 0.5f);
      fps_label.text = L"FPS: " + std::to_wstring(fps);
      fps_accum_time = 0.f;
      fps_accum_frames = 0;
    }

    active_scene->handle_messages(ctx);
    active_scene->update(dt);
    ui_clear(ctx, {0.1f, 0.2f, 0.35f, 1.f});
    active_scene->draw(ctx);
    active_scene->handle_events();
    ui_present(ctx);
  }

  ui_destroy(ctx);
  return 0;
}
