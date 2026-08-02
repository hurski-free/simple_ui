#include <windows.h>

#include <string>

#include "simple_ui.h"

namespace {

std::wstring ExeDir() {
  wchar_t path[MAX_PATH]{};
  const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return {};
  }
  std::wstring dir(path, len);
  const size_t slash = dir.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    return {};
  }
  dir.resize(slash + 1);
  return dir;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  const std::wstring exe_dir = ExeDir();
  const std::wstring icon_path = exe_dir + L"icon.ico";
  const std::wstring font_path = exe_dir + L"fonts\\Orbitron-Medium.ttf";

  const ScreenSettings screen{
      ScreenMode::Windowed,
      960,
      540,
  };

  UiContext* ctx = ui_create(L"Custom Font", &screen, icon_path.c_str());
  if (!ui_is_valid(ctx)) {
    MessageBoxW(nullptr, L"Failed to create window", L"Error",
                MB_OK | MB_ICONERROR);
    return 1;
  }

  const FontAtlas* orbitron = ui_create_font(ctx, font_path.c_str(), 32.f);
  if (!orbitron) {
    MessageBoxW(nullptr, L"Failed to load Orbitron-Medium.ttf", L"Error",
                MB_OK | MB_ICONERROR);
    ui_destroy(ctx);
    return 1;
  }

  std::wstring input_value = L"Type here";

  Input input;
  input.width = 360.f;
  input.height = 44.f;
  input.font = orbitron;
  input.font_size = 24.f;
  input.placeholder = L"Custom font input";
  input.bind_data(&input_value);

  Button button;
  button.width = 220.f;
  button.height = 48.f;
  button.font = orbitron;
  button.font_size = 24.f;
  button.text = L"Orbitron";
  button.style_base.background_color = {0.16f, 0.2f, 0.32f, 1.f};
  button.style_base.border = {2.f, BorderMode::Out, {0.85f, 0.9f, 1.f, 1.f}};
  button.style_hovered = button.style_base;
  button.style_hovered.background_color = {0.26f, 0.36f, 0.55f, 1.f};
  button.style_active = button.style_base;
  button.style_active.background_color = {0.12f, 0.16f, 0.26f, 1.f};
  button.text_color = {1.f, 1.f, 1.f, 1.f};
  button.transition.background_duration = 0.15f;

  Text label;
  label.width = 480.f;
  label.height = 40.f;
  label.font = orbitron;
  label.font_size = 28.f;
  label.color = {0.95f, 0.97f, 1.f, 1.f};
  label.text = L"Just text with Orbitron";

  const float screen_w = static_cast<float>(ui_get_width(ctx));
  const float screen_h = static_cast<float>(ui_get_height(ctx));
  const float gap = 20.f;
  const float total_h = input.height + gap + button.height + gap + label.height;
  float y = (screen_h - total_h) * 0.5f;

  input.x = (screen_w - input.width) * 0.5f;
  input.y = y;
  y += input.height + gap;

  button.x = (screen_w - button.width) * 0.5f;
  button.y = y;
  y += button.height + gap;

  label.x = (screen_w - label.width) * 0.5f;
  label.y = y;

  Scene scene;
  scene.components = {&input, &button, &label};
  scene.prepare_scene();

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

    scene.handle_messages(ctx);
    scene.update(dt);
    ui_clear(ctx, {0.08f, 0.1f, 0.16f, 1.f});
    scene.draw(ctx);
    scene.handle_events();
    ui_present(ctx);
  }

  ui_destroy(ctx);
  return 0;
}
