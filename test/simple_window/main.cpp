#include <windows.h>

#include <algorithm>
#include <string>

#include "simple_ui.h"

namespace {

struct ResolutionOption {
  int width = 0;
  int height = 0;
  const wchar_t* label = nullptr;
};

const ResolutionOption kResolutions[] = {
    {1280, 720, L"1280 x 720"},
    {1600, 900, L"1600 x 900"},
    {1920, 1080, L"1920 x 1080"},
    {2560, 1440, L"2560 x 1440"},
    {3840, 2160, L"3840 x 2160"},
};

const ScreenMode kScreenModes[] = {
    ScreenMode::Windowed,
    ScreenMode::Borderless,
    ScreenMode::Fullscreen,
};

struct AppSettings {
  int resolution_index = 0;
  int screen_mode_index = 0;
  float brightness = 100.f;
};

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

int FindResolutionIndex(int width, int height) {
  const int count =
      static_cast<int>(sizeof(kResolutions) / sizeof(kResolutions[0]));
  for (int i = 0; i < count; ++i) {
    if (kResolutions[i].width == width && kResolutions[i].height == height) {
      return i;
    }
  }
  return 0;
}

int FindScreenModeIndex(ScreenMode mode) {
  const int count =
      static_cast<int>(sizeof(kScreenModes) / sizeof(kScreenModes[0]));
  for (int i = 0; i < count; ++i) {
    if (kScreenModes[i] == mode) {
      return i;
    }
  }
  return 0;
}

bool SettingsValid(const AppSettings& s) {
  const int res_count =
      static_cast<int>(sizeof(kResolutions) / sizeof(kResolutions[0]));
  const int mode_count =
      static_cast<int>(sizeof(kScreenModes) / sizeof(kScreenModes[0]));
  return s.resolution_index >= 0 && s.resolution_index < res_count &&
         s.screen_mode_index >= 0 && s.screen_mode_index < mode_count;
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

  Scene main_menu;
  Scene settings;
  Scene* active_scene = &main_menu;

  AppSettings applied;
  applied.resolution_index =
      FindResolutionIndex(ui_get_width(ctx), ui_get_height(ctx));
  applied.screen_mode_index = FindScreenModeIndex(ui_get_screen_mode(ctx));
  applied.brightness = 100.f;

  // Draft is a working copy of applied; widgets edit only the draft.
  AppSettings draft = applied;
  ui_set_brightness(ctx, applied.brightness / 100.f);

  // --- Main menu ---
  Button btn_settings;
  Button btn_exit;
  Label fps_label;

  StyleMenuButton(btn_settings);
  StyleMenuButton(btn_exit);

  btn_settings.text = L"Settings";
  btn_exit.text = L"Exit";

  fps_label.text = L"FPS: --";
  fps_label.width = 140.f;
  fps_label.height = 24.f;
  fps_label.x = 12.f;
  fps_label.y = 12.f;
  fps_label.color = {0.95f, 0.97f, 1.f, 1.f};
  fps_label.font_size = 18.f;

  btn_exit.on_click = [ctx]() { PostQuitMessage(0); };

  main_menu.components = {&btn_settings, &btn_exit, &fps_label};
  main_menu.prepare_scene();

  // --- Settings (bound to draft) ---
  Text settings_title;
  settings_title.text = L"Settings";
  settings_title.width = 300.f;
  settings_title.height = 40.f;
  settings_title.color = {1.f, 1.f, 1.f, 1.f};
  settings_title.font_size = 32.f;

  Label resolution_label;
  resolution_label.text = L"Resolution";
  resolution_label.width = 220.f;
  resolution_label.height = 28.f;
  resolution_label.color = {0.9f, 0.93f, 1.f, 1.f};
  resolution_label.font_size = 20.f;

  Select resolution;
  resolution.width = 280.f;
  resolution.height = 40.f;
  resolution.dropdown_height = 160.f;
  resolution.font_size = 20.f;
  for (const ResolutionOption& opt : kResolutions) {
    resolution.options.push_back(opt.label);
  }
  resolution.bind_data(&draft.resolution_index);

  Label screen_mode_label;
  screen_mode_label.text = L"ScreenMode";
  screen_mode_label.width = 220.f;
  screen_mode_label.height = 28.f;
  screen_mode_label.color = {0.9f, 0.93f, 1.f, 1.f};
  screen_mode_label.font_size = 20.f;

  Select screen_mode;
  screen_mode.width = 280.f;
  screen_mode.height = 40.f;
  screen_mode.dropdown_height = 100.f;
  screen_mode.font_size = 20.f;
  screen_mode.options = {L"Windowed", L"Borderless", L"Fullscreen"};
  screen_mode.bind_data(&draft.screen_mode_index);

  Label brightness_label;
  brightness_label.text = L"Brightness";
  brightness_label.width = 220.f;
  brightness_label.height = 28.f;
  brightness_label.color = {0.9f, 0.93f, 1.f, 1.f};
  brightness_label.font_size = 20.f;

  Range brightness_slider;
  brightness_slider.width = 180.f;
  brightness_slider.min_value = 50.f;
  brightness_slider.max_value = 100.f;
  brightness_slider.step = 1.f;
  brightness_slider.text.clear();
  brightness_slider.show_value = true;
  brightness_slider.tick_labels = {{50.f, L"50"}, {100.f, L"100"}};
  brightness_slider.bind_data(&draft.brightness);

  Button btn_back;
  Button btn_apply;
  StyleMenuButton(btn_back);
  StyleMenuButton(btn_apply);
  btn_back.width = 140.f;
  btn_back.height = 40.f;
  btn_back.font_size = 24.f;
  btn_back.text = L"Back";
  btn_apply.width = 140.f;
  btn_apply.height = 40.f;
  btn_apply.font_size = 24.f;
  btn_apply.text = L"Apply";

  auto sync_widgets_from_draft = [&]() {
    resolution.selected = draft.resolution_index;
    screen_mode.selected = draft.screen_mode_index;
    brightness_slider.value = draft.brightness;
  };

  auto open_settings = [&]() {
    draft = applied;
    sync_widgets_from_draft();
    active_scene = &settings;
  };

  btn_settings.on_click = open_settings;

  btn_back.on_click = [&]() {
    // Discard draft; restore applied brightness (only setting with live preview).
    ui_set_brightness(ctx, applied.brightness / 100.f);
    active_scene = &main_menu;
  };

  auto layout_ui = [&]() {
    const float screen_w = static_cast<float>(ui_get_width(ctx));
    const float screen_h = static_cast<float>(ui_get_height(ctx));

    const float menu_gap = 14.f;
    const float menu_total_h = btn_settings.height * 2.f + menu_gap;
    float menu_y = (screen_h - menu_total_h) * 0.5f;
    const float menu_x = (screen_w - btn_settings.width) * 0.5f;

    btn_settings.x = menu_x;
    btn_settings.y = menu_y;
    menu_y += btn_settings.height + menu_gap;
    btn_exit.x = menu_x;
    btn_exit.y = menu_y;

    settings_title.x = (screen_w - settings_title.width) * 0.5f;
    settings_title.y = screen_h * 0.12f;

    const float field_x = (screen_w - resolution.width) * 0.5f;
    float cy = settings_title.y + 56.f;

    resolution_label.x = field_x;
    resolution_label.y = cy;
    cy += resolution_label.height + 8.f;
    resolution.x = field_x;
    resolution.y = cy;
    cy += resolution.height + 24.f;

    screen_mode_label.x = field_x;
    screen_mode_label.y = cy;
    cy += screen_mode_label.height + 8.f;
    screen_mode.x = field_x;
    screen_mode.y = cy;
    cy += screen_mode.height + 24.f;

    brightness_label.x = field_x;
    brightness_label.y = cy;
    cy += brightness_label.height + 8.f;
    brightness_slider.x = field_x;
    brightness_slider.y = cy;

    float bright_w = 0.f;
    float bright_h = 0.f;
    brightness_slider.get_layout_size(bright_w, bright_h);
    cy += bright_h + 36.f;

    const float footer_gap = 16.f;
    const float footer_total = btn_back.width + footer_gap + btn_apply.width;
    const float footer_x = (screen_w - footer_total) * 0.5f;
    btn_back.x = footer_x;
    btn_back.y = cy;
    btn_apply.x = footer_x + btn_back.width + footer_gap;
    btn_apply.y = cy;
  };

  btn_apply.on_click = [&]() {
    draft.brightness = std::clamp(draft.brightness, 50.f, 100.f);
    if (!SettingsValid(draft)) {
      return;
    }

    applied = draft;

    const ResolutionOption& res = kResolutions[applied.resolution_index];
    const ScreenMode mode = kScreenModes[applied.screen_mode_index];
    ui_set_brightness(ctx, applied.brightness / 100.f);
    ui_set_screen_size(ctx, res.width, res.height);
    ui_set_screen_mode(ctx, mode);
    layout_ui();
  };

  settings.components = {&settings_title,   &resolution_label, &resolution,
                         &screen_mode_label, &screen_mode,     &brightness_label,
                         &brightness_slider, &btn_back,        &btn_apply};
  settings.prepare_scene();

  layout_ui();

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

    // Live preview only for brightness, and only while Settings is open.
    if (active_scene == &settings) {
      draft.brightness = std::clamp(draft.brightness, 50.f, 100.f);
      ui_set_brightness(ctx, draft.brightness / 100.f);
    } else {
      ui_set_brightness(ctx, applied.brightness / 100.f);
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
