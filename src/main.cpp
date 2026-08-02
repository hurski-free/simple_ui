#include "core/UiContext.h"

#include <memory>
#include <string>
#include <vector>
#include <windows.h>

namespace {

const FontAtlas* ResolveFont(const UiContext* ctx, const FontAtlas* font) {
  if (font && font->is_valid()) {
    return font;
  }
  if (ctx && ctx->font_atlas.is_valid()) {
    return &ctx->font_atlas;
  }
  return nullptr;
}

float FontMetricScale(const FontAtlas* atlas, float font_size) {
  if (!atlas || !atlas->is_valid()) {
    return 1.f;
  }
  const float base = atlas->size_px();
  if (font_size <= 0.f || base <= 0.f) {
    return 1.f;
  }
  return font_size / base;
}

std::string WideToUtf8(const wchar_t* path) {
  if (!path || !*path) {
    return {};
  }
  const int bytes =
      WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 1) {
    return {};
  }
  std::string out(static_cast<size_t>(bytes - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, path, -1, out.data(), bytes, nullptr, nullptr);
  return out;
}

}  // namespace

UiContext* ui_create(const wchar_t* title, const ScreenSettings* screenSettings,
                     const wchar_t* iconPath) {
  auto* ctx = new UiContext(title, screenSettings, iconPath);
  if (!ctx->window.IsValid()) {
    delete ctx;
    return nullptr;
  }

  if (!ctx->renderer.init(ctx->window.GetDevice())) {
    delete ctx;
    return nullptr;
  }

  // Default UI font (bitmap atlas at a fixed pixel size).
  if (ctx->font_atlas.build("C:\\Windows\\Fonts\\segoeui.ttf", 32.f) ||
      ctx->font_atlas.build("C:\\Windows\\Fonts\\arial.ttf", 32.f)) {
    ctx->renderer.register_font_atlas(ctx->font_atlas);
    ctx->renderer.set_default_font_atlas(&ctx->font_atlas);
  }

  return ctx;
}

void ui_destroy(UiContext* ctx) {
  delete ctx;
}

bool ui_is_valid(const UiContext* ctx) {
  return ctx != nullptr && ctx->window.IsValid() && ctx->renderer.is_valid();
}

bool ui_process_messages(UiContext* ctx) {
  if (!ctx) {
    return false;
  }
  return ctx->window.ProcessMessages();
}

void ui_clear(UiContext* ctx, const Color& color) {
  if (!ctx) {
    return;
  }
  ctx->window.Clear(color);
}

void ui_present(UiContext* ctx) {
  if (!ctx) {
    return;
  }
  ctx->window.Present();
}

HWND ui_get_hwnd(const UiContext* ctx) {
  return ctx ? ctx->window.GetHwnd() : nullptr;
}

ID3D11Device* ui_get_device(const UiContext* ctx) {
  return ctx ? ctx->window.GetDevice() : nullptr;
}

ID3D11DeviceContext* ui_get_device_context(const UiContext* ctx) {
  return ctx ? ctx->window.GetContext() : nullptr;
}

int ui_get_width(const UiContext* ctx) {
  return ctx ? ctx->window.GetWidth() : 0;
}

int ui_get_height(const UiContext* ctx) {
  return ctx ? ctx->window.GetHeight() : 0;
}

ScreenMode ui_get_screen_mode(const UiContext* ctx) {
  return ctx ? ctx->window.GetScreenMode() : ScreenMode::Windowed;
}

bool ui_set_screen_mode(UiContext* ctx, ScreenMode mode) {
  if (!ctx) {
    return false;
  }
  return ctx->window.SetScreenMode(mode);
}

bool ui_set_msaa_samples(UiContext* ctx, int samples) {
  if (!ctx) {
    return false;
  }
  return ctx->window.SetMsaaSamples(samples);
}

int ui_get_msaa_samples(const UiContext* ctx) {
  return ctx ? ctx->window.GetMsaaSamples() : 1;
}

const MouseEvents* ui_get_mouse_events(const UiContext* ctx) {
  return ctx ? &ctx->window.GetMouseEvents() : nullptr;
}

const KeyboardEvents* ui_get_keyboard_events(const UiContext* ctx) {
  return ctx ? &ctx->window.GetKeyboardEvents() : nullptr;
}

int ui_load_texture(UiContext* ctx, const wchar_t* path) {
  return ctx ? ctx->renderer.load_texture_file(path) : -1;
}

int ui_create_texture(UiContext* ctx, int width, int height,
                      const unsigned char* rgba) {
  return ctx ? ctx->renderer.create_texture_rgba(width, height, rgba) : -1;
}

void ui_unload_texture(UiContext* ctx, int texture_id) {
  if (ctx) {
    ctx->renderer.unload_texture(texture_id);
  }
}

bool ui_get_texture_size(UiContext* ctx, int texture_id, int* out_w,
                         int* out_h) {
  return ctx ? ctx->renderer.get_texture_size(texture_id, out_w, out_h) : false;
}

const FontAtlas* ui_default_font(const UiContext* ctx) {
  if (!ctx || !ctx->font_atlas.is_valid()) {
    return nullptr;
  }
  return &ctx->font_atlas;
}

const FontAtlas* ui_create_font(UiContext* ctx, const wchar_t* ttf_path,
                                float size_px) {
  if (!ctx || !ttf_path || size_px <= 0.f) {
    return nullptr;
  }
  const std::string path_utf8 = WideToUtf8(ttf_path);
  if (path_utf8.empty()) {
    return nullptr;
  }

  auto atlas = std::make_unique<FontAtlas>();
  if (!atlas->build(path_utf8.c_str(), size_px)) {
    return nullptr;
  }
  if (!ctx->renderer.register_font_atlas(*atlas)) {
    return nullptr;
  }
  const FontAtlas* raw = atlas.get();
  ctx->fonts.push_back(std::move(atlas));
  return raw;
}

void ui_destroy_font(UiContext* ctx, const FontAtlas* font) {
  if (!ctx || !font || font == &ctx->font_atlas) {
    return;
  }
  ctx->renderer.unregister_font_atlas(font);
  for (auto it = ctx->fonts.begin(); it != ctx->fonts.end(); ++it) {
    if (it->get() == font) {
      ctx->fonts.erase(it);
      return;
    }
  }
}

float ui_measure_text(const UiContext* ctx, const wchar_t* text,
                      float font_size, const FontAtlas* font) {
  const FontAtlas* atlas = ResolveFont(ctx, font);
  if (!atlas || !text) {
    return 0.f;
  }
  const float base = atlas->measure_width(text);
  return base * FontMetricScale(atlas, font_size);
}

float ui_measure_text_n(const UiContext* ctx, const wchar_t* text,
                        size_t length, float font_size, const FontAtlas* font) {
  const FontAtlas* atlas = ResolveFont(ctx, font);
  if (!atlas || !text || length == 0) {
    return 0.f;
  }
  const float base = atlas->measure_width(text, length);
  return base * FontMetricScale(atlas, font_size);
}

size_t ui_text_index_at_x(const UiContext* ctx, const wchar_t* text, float x,
                          float font_size, const FontAtlas* font) {
  const FontAtlas* atlas = ResolveFont(ctx, font);
  if (!atlas || !text) {
    return 0;
  }
  const float scale = FontMetricScale(atlas, font_size);
  float local_x = x;
  if (scale > 0.f) {
    local_x = x / scale;
  }
  return atlas->index_at_x(text, local_x);
}

size_t ui_text_index_at_x_n(const UiContext* ctx, const wchar_t* text,
                            size_t length, float x, float font_size,
                            const FontAtlas* font) {
  const FontAtlas* atlas = ResolveFont(ctx, font);
  if (!atlas || !text || length == 0) {
    return 0;
  }
  const float scale = FontMetricScale(atlas, font_size);
  float local_x = x;
  if (scale > 0.f) {
    local_x = x / scale;
  }
  return atlas->index_at_x(text, length, local_x);
}

float ui_font_atlas_size(const UiContext* ctx, const FontAtlas* font) {
  const FontAtlas* atlas = ResolveFont(ctx, font);
  return atlas ? atlas->size_px() : 0.f;
}

float ui_font_line_height(const UiContext* ctx, float font_size,
                          const FontAtlas* font) {
  const FontAtlas* atlas = ResolveFont(ctx, font);
  if (!atlas) {
    return 0.f;
  }
  return atlas->line_height() * FontMetricScale(atlas, font_size);
}

float ui_font_ascent(const UiContext* ctx, float font_size,
                     const FontAtlas* font) {
  const FontAtlas* atlas = ResolveFont(ctx, font);
  if (!atlas) {
    return 0.f;
  }
  return atlas->ascent() * FontMetricScale(atlas, font_size);
}
