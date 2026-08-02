#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

constexpr const char* kStarVS = R"(
cbuffer ScreenCB : register(b0) {
  float2 screen_size;
  float2 _pad;
};

struct VSIn {
  float2 pos : POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

struct VSOut {
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

VSOut main(VSIn input) {
  VSOut o;
  float2 ndc = float2(input.pos.x / screen_size.x * 2.0f - 1.0f,
                      1.0f - input.pos.y / screen_size.y * 2.0f);
  o.pos = float4(ndc, 0.0f, 1.0f);
  o.uv = input.uv;
  o.color = input.color;
  return o;
}
)";

constexpr const char* kStarPS = R"(
struct PSIn {
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

float4 main(PSIn input) : SV_TARGET {
  float2 p = input.uv * 2.0f - 1.0f;
  float d = length(p);
  float alpha = saturate(1.0f - smoothstep(0.55f, 1.0f, d));
  return float4(input.color.rgb, input.color.a * alpha);
}
)";

struct StarVertex {
  float x, y;
  float u, v;
  float r, g, b, a;
};

struct Star {
  float x = 0.f;
  float y = 0.f;
  float radius = 1.f;
  float speed = 40.f;
  float r = 1.f;
  float g = 1.f;
  float b = 1.f;
  float brightness = 1.f;
};

float Rand01(unsigned& state) {
  state = state * 1664525u + 1013904223u;
  return static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

class StarField {
public:
  bool init(ID3D11Device* device) {
    if (!device) {
      return false;
    }
    device_ = device;
    device_->AddRef();

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(kStarVS, strlen(kStarVS), nullptr, nullptr, nullptr,
                            "main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                            &vs_blob, &errors);
    if (FAILED(hr)) {
      if (errors) {
        errors->Release();
      }
      return false;
    }
    if (errors) {
      errors->Release();
      errors = nullptr;
    }
    hr = D3DCompile(kStarPS, strlen(kStarPS), nullptr, nullptr, nullptr, "main",
                    "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps_blob,
                    &errors);
    if (FAILED(hr)) {
      if (errors) {
        errors->Release();
      }
      vs_blob->Release();
      return false;
    }
    if (errors) {
      errors->Release();
    }

    hr = device_->CreateVertexShader(vs_blob->GetBufferPointer(),
                                     vs_blob->GetBufferSize(), nullptr, &vs_);
    if (FAILED(hr)) {
      vs_blob->Release();
      ps_blob->Release();
      return false;
    }
    hr = device_->CreatePixelShader(ps_blob->GetBufferPointer(),
                                    ps_blob->GetBufferSize(), nullptr, &ps_);
    ps_blob->Release();
    if (FAILED(hr)) {
      vs_blob->Release();
      return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    hr = device_->CreateInputLayout(layout, 3, vs_blob->GetBufferPointer(),
                                    vs_blob->GetBufferSize(), &input_layout_);
    vs_blob->Release();
    if (FAILED(hr)) {
      return false;
    }

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 16;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device_->CreateBuffer(&cbd, nullptr, &screen_cb_);
    if (FAILED(hr)) {
      return false;
    }

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device_->CreateBlendState(&blend, &blend_state_);
    if (FAILED(hr)) {
      return false;
    }

    D3D11_RASTERIZER_DESC rast{};
    rast.FillMode = D3D11_FILL_SOLID;
    rast.CullMode = D3D11_CULL_NONE;
    rast.DepthClipEnable = TRUE;
    hr = device_->CreateRasterizerState(&rast, &raster_state_);
    return SUCCEEDED(hr);
  }

  ~StarField() { release(); }

  void release() {
    if (vb_) {
      vb_->Release();
      vb_ = nullptr;
      vb_capacity_ = 0;
    }
    if (screen_cb_) {
      screen_cb_->Release();
      screen_cb_ = nullptr;
    }
    if (blend_state_) {
      blend_state_->Release();
      blend_state_ = nullptr;
    }
    if (raster_state_) {
      raster_state_->Release();
      raster_state_ = nullptr;
    }
    if (input_layout_) {
      input_layout_->Release();
      input_layout_ = nullptr;
    }
    if (vs_) {
      vs_->Release();
      vs_ = nullptr;
    }
    if (ps_) {
      ps_->Release();
      ps_ = nullptr;
    }
    if (device_) {
      device_->Release();
      device_ = nullptr;
    }
  }

  void resize(int width, int height) {
    if (width <= 0 || height <= 0) {
      return;
    }
    if (width == width_ && height == height_ && !stars_.empty()) {
      return;
    }
    width_ = width;
    height_ = height;
    spawn_all();
  }

  void update(float dt) {
    if (width_ <= 0 || height_ <= 0) {
      return;
    }
    for (Star& star : stars_) {
      star.x += star.speed * dt;
      if (star.x - star.radius > static_cast<float>(width_)) {
        respawn(star, true);
      }
    }
  }

  void draw(ID3D11DeviceContext* context) {
    if (!context || !vs_ || !ps_ || stars_.empty() || width_ <= 0 ||
        height_ <= 0) {
      return;
    }

    const size_t vertex_count = stars_.size() * 6;
    if (!ensure_vb(static_cast<UINT>(vertex_count))) {
      return;
    }

    std::vector<StarVertex> verts(vertex_count);
    size_t vi = 0;
    for (const Star& star : stars_) {
      const float x0 = star.x - star.radius;
      const float y0 = star.y - star.radius;
      const float x1 = star.x + star.radius;
      const float y1 = star.y + star.radius;
      const float r = star.r * star.brightness;
      const float g = star.g * star.brightness;
      const float b = star.b * star.brightness;
      const float a = star.brightness;

      const StarVertex quad[6] = {
          {x0, y0, 0.f, 0.f, r, g, b, a}, {x1, y0, 1.f, 0.f, r, g, b, a},
          {x0, y1, 0.f, 1.f, r, g, b, a}, {x1, y0, 1.f, 0.f, r, g, b, a},
          {x1, y1, 1.f, 1.f, r, g, b, a}, {x0, y1, 0.f, 1.f, r, g, b, a},
      };
      for (const StarVertex& v : quad) {
        verts[vi++] = v;
      }
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      return;
    }
    std::memcpy(mapped.pData, verts.data(),
                verts.size() * sizeof(StarVertex));
    context->Unmap(vb_, 0);

    if (SUCCEEDED(context->Map(screen_cb_, 0, D3D11_MAP_WRITE_DISCARD, 0,
                               &mapped))) {
      float* data = static_cast<float*>(mapped.pData);
      data[0] = static_cast<float>(width_);
      data[1] = static_cast<float>(height_);
      data[2] = 0.f;
      data[3] = 0.f;
      context->Unmap(screen_cb_, 0);
    }

    const float blend_factor[4] = {0, 0, 0, 0};
    context->OMSetBlendState(blend_state_, blend_factor, 0xffffffff);
    context->RSSetState(raster_state_);
    context->OMSetDepthStencilState(nullptr, 0);
    context->IASetInputLayout(input_layout_);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(StarVertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &vb_, &stride, &offset);
    context->VSSetShader(vs_, nullptr, 0);
    context->PSSetShader(ps_, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &screen_cb_);
    context->GSSetShader(nullptr, nullptr, 0);
    context->Draw(static_cast<UINT>(vertex_count), 0);
  }

private:
  bool ensure_vb(UINT vertex_count) {
    if (vb_ && vb_capacity_ >= vertex_count) {
      return true;
    }
    if (vb_) {
      vb_->Release();
      vb_ = nullptr;
    }
    UINT cap = vb_capacity_ > 0 ? vb_capacity_ : 256u;
    while (cap < vertex_count) {
      cap *= 2;
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = cap * sizeof(StarVertex);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&bd, nullptr, &vb_))) {
      return false;
    }
    vb_capacity_ = cap;
    return true;
  }

  void spawn_all() {
    stars_.clear();
    stars_.resize(120);
    rng_ = 0xC0FFEEu ^ static_cast<unsigned>(width_ * 73856093u) ^
           static_cast<unsigned>(height_ * 19349663u);
    for (Star& star : stars_) {
      respawn(star, false);
    }
  }

  void respawn(Star& star, bool from_left) {
    const float area_w = static_cast<float>(width_);
    const float area_h = static_cast<float>(height_);

    star.radius = 0.8f + Rand01(rng_) * 2.8f;
    star.speed = 6.f + Rand01(rng_) * 22.f;
    star.brightness = 0.25f + Rand01(rng_) * 0.75f;

    // Mix of cool whites, blues, yellows, and soft reds.
    const float roll = Rand01(rng_);
    if (roll < 0.45f) {
      star.r = 0.85f + Rand01(rng_) * 0.15f;
      star.g = 0.88f + Rand01(rng_) * 0.12f;
      star.b = 1.f;
    } else if (roll < 0.7f) {
      star.r = 0.55f + Rand01(rng_) * 0.25f;
      star.g = 0.7f + Rand01(rng_) * 0.25f;
      star.b = 1.f;
    } else if (roll < 0.88f) {
      star.r = 1.f;
      star.g = 0.85f + Rand01(rng_) * 0.15f;
      star.b = 0.45f + Rand01(rng_) * 0.35f;
    } else {
      star.r = 1.f;
      star.g = 0.55f + Rand01(rng_) * 0.25f;
      star.b = 0.55f + Rand01(rng_) * 0.25f;
    }

    star.y = star.radius + Rand01(rng_) * std::max(1.f, area_h - star.radius * 2.f);
    if (from_left) {
      star.x = -star.radius - Rand01(rng_) * 40.f;
    } else {
      star.x = Rand01(rng_) * area_w;
    }
  }

  ID3D11Device* device_ = nullptr;
  ID3D11VertexShader* vs_ = nullptr;
  ID3D11PixelShader* ps_ = nullptr;
  ID3D11InputLayout* input_layout_ = nullptr;
  ID3D11Buffer* vb_ = nullptr;
  ID3D11Buffer* screen_cb_ = nullptr;
  ID3D11BlendState* blend_state_ = nullptr;
  ID3D11RasterizerState* raster_state_ = nullptr;
  UINT vb_capacity_ = 0;
  int width_ = 0;
  int height_ = 0;
  unsigned rng_ = 1;
  std::vector<Star> stars_;
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

  StarField stars;
  if (!stars.init(ui_get_device(ctx))) {
    MessageBoxW(nullptr, L"Failed to init star field", L"Error",
                MB_OK | MB_ICONERROR);
    ui_destroy(ctx);
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

  AppSettings draft = applied;
  ui_set_brightness(ctx, applied.brightness / 100.f);

  // --- Main menu ---
  Canvas background;
  background.layer = 0;
  background.clear_color = {0.f, 0.f, 0.f, 1.f};
  background.tint = {1.f, 1.f, 1.f, 1.f};

  Button btn_settings;
  Button btn_exit;
  Label fps_label;

  StyleMenuButton(btn_settings);
  StyleMenuButton(btn_exit);
  btn_settings.layer = 1;
  btn_exit.layer = 1;
  fps_label.layer = 1;

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

  main_menu.components = {&background, &btn_settings, &btn_exit, &fps_label};
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
    ui_set_brightness(ctx, applied.brightness / 100.f);
    active_scene = &main_menu;
  };

  auto layout_ui = [&]() {
    const float screen_w = static_cast<float>(ui_get_width(ctx));
    const float screen_h = static_cast<float>(ui_get_height(ctx));

    background.x = 0.f;
    background.y = 0.f;
    background.width = screen_w;
    background.height = screen_h;

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

    if (active_scene == &settings) {
      draft.brightness = std::clamp(draft.brightness, 50.f, 100.f);
      ui_set_brightness(ctx, draft.brightness / 100.f);
    } else {
      ui_set_brightness(ctx, applied.brightness / 100.f);
    }

    active_scene->handle_messages(ctx);
    active_scene->update(dt);

    ui_clear(ctx, {0.1f, 0.2f, 0.35f, 1.f});

    if (active_scene == &main_menu) {
      if (background.begin_draw(ctx, true)) {
        stars.resize(background.texture_width(), background.texture_height());
        stars.update(dt);
        stars.draw(ui_get_device_context(ctx));
        background.end_draw(ctx);
      }
    }

    active_scene->draw(ctx);
    active_scene->handle_events();
    ui_present(ctx);
  }

  stars.release();
  ui_destroy(ctx);
  return 0;
}
