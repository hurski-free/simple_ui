#include "Window.h"

extern HINSTANCE g_hInstance;

namespace {
constexpr wchar_t kClassName[] = L"SimpleUIWindowClass";
bool g_classRegistered = false;

// Fixed-size decorated window (not user-resizable).
constexpr DWORD kWindowedStyle =
    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

constexpr const char* kBlitVS = R"(
struct VSOut {
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID) {
  VSOut o;
  o.uv = float2((id << 1) & 2, id & 2);
  o.pos = float4(o.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
  return o;
}
)";

constexpr const char* kBlitPS = R"(
Texture2D scene_tex : register(t0);
SamplerState scene_samp : register(s0);

cbuffer BlitParams : register(b0) {
  float brightness;
  float3 _pad;
};

struct PSIn {
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET {
  float4 c = scene_tex.Sample(scene_samp, input.uv);
  c.rgb *= brightness;
  return c;
}
)";
}  // namespace

Window::Window(const wchar_t* title, const ScreenSettings* screenSettings,
               const wchar_t* iconPath) {
  if (!screenSettings) {
    return;
  }

  screenMode_ = screenSettings->screen_mode;
  windowed_width_ = screenSettings->screen_width > 0
                        ? screenSettings->screen_width
                        : 1280;
  windowed_height_ = screenSettings->screen_height > 0
                         ? screenSettings->screen_height
                         : 720;
  msaa_samples_ = NormalizeMsaaSamples(screenSettings->msaa_samples);

  switch (screenMode_) {
    case ScreenMode::Windowed:
      width_ = windowed_width_;
      height_ = windowed_height_;
      break;
    case ScreenMode::Borderless:
      width_ = GetSystemMetrics(SM_CXSCREEN);
      height_ = GetSystemMetrics(SM_CYSCREEN);
      break;
    case ScreenMode::Fullscreen:
      width_ = screenSettings->screen_width > 0
                   ? screenSettings->screen_width
                   : GetSystemMetrics(SM_CXSCREEN);
      height_ = screenSettings->screen_height > 0
                    ? screenSettings->screen_height
                    : GetSystemMetrics(SM_CYSCREEN);
      break;
  }

  if (!InitWindow(title, iconPath) || !InitDirectX()) {
    Cleanup();
  }
}

Window::~Window() {
  Cleanup();
}

int Window::NormalizeMsaaSamples(int samples) {
  if (samples >= 8) {
    return 8;
  }
  if (samples >= 4) {
    return 4;
  }
  if (samples >= 2) {
    return 2;
  }
  return 1;
}

bool Window::IsValid() const {
  return hwnd_ != nullptr && device_ != nullptr && context_ != nullptr &&
         swapChain_ != nullptr && backbuffer_rtv_ != nullptr &&
         scene_rtv_ != nullptr && scene_tex_ != nullptr &&
         (resolve_srv_ != nullptr || scene_srv_ != nullptr);
}

bool Window::ProcessMessages() {
  BeginFrameInput();

  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      return false;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return true;
}

void Window::Clear(const Color& color) {
  if (!context_ || !scene_rtv_) {
    return;
  }
  BindSceneTarget();
  const float clear_color[] = {color.r, color.g, color.b, color.a};
  context_->ClearRenderTargetView(scene_rtv_, clear_color);
}

void Window::Present() {
  if (!swapChain_ || !context_ || !scene_rtv_) {
    return;
  }

  // Unbind the offscreen scene before resolve/sample.
  ID3D11RenderTargetView* null_rtv = nullptr;
  context_->OMSetRenderTargets(1, &null_rtv, nullptr);

  if (msaa_samples_ > 1 && scene_tex_ && resolve_tex_) {
    context_->ResolveSubresource(resolve_tex_, 0, scene_tex_, 0,
                                 DXGI_FORMAT_R8G8B8A8_UNORM);
  }

  BlitSceneToBackbuffer();

  swapChain_->Present(1, 0);
  BindSceneTarget();
}

ScreenMode Window::GetScreenMode() const {
  return screenMode_;
}

int Window::GetWindowedWidth() const {
  return windowed_width_;
}

int Window::GetWindowedHeight() const {
  return windowed_height_;
}

bool Window::SetScreenSize(int width, int height) {
  if (!IsValid() || width <= 0 || height <= 0) {
    return false;
  }

  windowed_width_ = width;
  windowed_height_ = height;

  if (screenMode_ != ScreenMode::Windowed) {
    return true;
  }

  RECT rect = {0, 0, width, height};
  AdjustWindowRect(&rect, kWindowedStyle, FALSE);
  SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left,
               rect.bottom - rect.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  return ResizeSwapChain(static_cast<UINT>(width), static_cast<UINT>(height));
}

int Window::GetMsaaSamples() const {
  return msaa_samples_;
}

void Window::SetBrightness(float brightness) {
  brightness_ = brightness < 0.f ? 0.f : brightness;
}

float Window::GetBrightness() const {
  return brightness_;
}

bool Window::SetMsaaSamples(int samples) {
  if (!device_ || !context_) {
    return false;
  }
  const int normalized = NormalizeMsaaSamples(samples);
  if (normalized == msaa_samples_ && scene_rtv_) {
    return true;
  }
  msaa_samples_ = normalized;
  if (!CreateSceneTargets()) {
    return false;
  }
  BindSceneTarget();
  return true;
}

bool Window::ResizeSwapChain(UINT width, UINT height) {
  if (!swapChain_ || !device_ || !context_) {
    return false;
  }

  context_->OMSetRenderTargets(0, nullptr, nullptr);
  ReleaseSceneTargets();
  if (backbuffer_rtv_) {
    backbuffer_rtv_->Release();
    backbuffer_rtv_ = nullptr;
  }

  HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN,
                                         DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
  if (FAILED(hr)) {
    return false;
  }

  DXGI_SWAP_CHAIN_DESC desc{};
  swapChain_->GetDesc(&desc);
  width_ = static_cast<int>(desc.BufferDesc.Width);
  height_ = static_cast<int>(desc.BufferDesc.Height);

  if (!CreateBackbufferRtv() || !CreateSceneTargets()) {
    return false;
  }

  BindSceneTarget();
  return true;
}

bool Window::SetScreenMode(ScreenMode mode) {
  if (!IsValid()) {
    return false;
  }
  if (mode == screenMode_) {
    return true;
  }

  if (mode == ScreenMode::Windowed) {
    BOOL fullscreen = FALSE;
    swapChain_->GetFullscreenState(&fullscreen, nullptr);
    if (fullscreen) {
      swapChain_->SetFullscreenState(FALSE, nullptr);
    }

    SetWindowLongPtrW(hwnd_, GWL_STYLE, kWindowedStyle);
    RECT rect = {0, 0, windowed_width_, windowed_height_};
    AdjustWindowRect(&rect, kWindowedStyle, FALSE);
    SetWindowPos(hwnd_, nullptr, 80, 80, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    if (!ResizeSwapChain(static_cast<UINT>(windowed_width_),
                         static_cast<UINT>(windowed_height_))) {
      return false;
    }
  } else if (mode == ScreenMode::Borderless) {
    BOOL fullscreen = FALSE;
    swapChain_->GetFullscreenState(&fullscreen, nullptr);
    if (fullscreen) {
      swapChain_->SetFullscreenState(FALSE, nullptr);
    }

    const int screen_w = GetSystemMetrics(SM_CXSCREEN);
    const int screen_h = GetSystemMetrics(SM_CYSCREEN);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, screen_w, screen_h,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    if (!ResizeSwapChain(static_cast<UINT>(screen_w),
                         static_cast<UINT>(screen_h))) {
      return false;
    }
  } else {
    SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP);
    ShowWindow(hwnd_, SW_SHOW);
    const HRESULT hr = swapChain_->SetFullscreenState(TRUE, nullptr);
    if (FAILED(hr)) {
      return false;
    }
    if (!ResizeSwapChain(0, 0)) {
      return false;
    }
  }

  screenMode_ = mode;
  return true;
}

HWND Window::GetHwnd() const {
  return hwnd_;
}

ID3D11Device* Window::GetDevice() const {
  return device_;
}

ID3D11DeviceContext* Window::GetContext() const {
  return context_;
}

int Window::GetWidth() const {
  return width_;
}

int Window::GetHeight() const {
  return height_;
}

const MouseEvents& Window::GetMouseEvents() const {
  return mouse_;
}

const KeyboardEvents& Window::GetKeyboardEvents() const {
  return keyboard_;
}

void Window::BeginFrameInput() {
  mouse_.left_pressed = false;
  mouse_.right_pressed = false;
  mouse_.middle_pressed = false;
  mouse_.left_released = false;
  mouse_.right_released = false;
  mouse_.middle_released = false;
  mouse_.wheel_delta = 0.f;
  mouse_.wheel_consumed = false;
  mouse_.click_consumed = false;

  for (int i = 0; i < KeyboardEvents::kKeyCount; ++i) {
    keyboard_.pressed[i] = false;
    keyboard_.released[i] = false;
  }
  keyboard_.char_count = 0;
}

void Window::HandleInputMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_MOUSEMOVE:
      mouse_.x = static_cast<float>(static_cast<short>(LOWORD(lParam)));
      mouse_.y = static_cast<float>(static_cast<short>(HIWORD(lParam)));
      break;
    case WM_LBUTTONDOWN:
      mouse_.left_down = true;
      mouse_.left_pressed = true;
      break;
    case WM_LBUTTONUP:
      mouse_.left_down = false;
      mouse_.left_released = true;
      break;
    case WM_RBUTTONDOWN:
      mouse_.right_down = true;
      mouse_.right_pressed = true;
      break;
    case WM_RBUTTONUP:
      mouse_.right_down = false;
      mouse_.right_released = true;
      break;
    case WM_MBUTTONDOWN:
      mouse_.middle_down = true;
      mouse_.middle_pressed = true;
      break;
    case WM_MBUTTONUP:
      mouse_.middle_down = false;
      mouse_.middle_released = true;
      break;
    case WM_MOUSEWHEEL:
      mouse_.wheel_delta +=
          static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
          static_cast<float>(WHEEL_DELTA);
      break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      const int key = static_cast<int>(wParam) & 0xFF;
      // Mark pressed on initial down and OS auto-repeat (hold Backspace, arrows).
      keyboard_.pressed[key] = true;
      keyboard_.down[key] = true;
      break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      const int key = static_cast<int>(wParam) & 0xFF;
      keyboard_.down[key] = false;
      keyboard_.released[key] = true;
      break;
    }
    case WM_CHAR: {
      const wchar_t ch = static_cast<wchar_t>(wParam);
      if (ch >= 32 && keyboard_.char_count < KeyboardEvents::kMaxChars) {
        keyboard_.chars[keyboard_.char_count++] = ch;
      }
      break;
    }
    default:
      break;
  }
}

bool Window::RegisterWindowClass() {
  if (g_classRegistered) {
    return true;
  }

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = Window::WndProc;
  wc.hInstance = g_hInstance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kClassName;

  if (!RegisterClassExW(&wc)) {
    return false;
  }

  g_classRegistered = true;
  return true;
}

bool Window::InitWindow(const wchar_t* title, const wchar_t* iconPath) {
  if (!g_hInstance) {
    return false;
  }

  if (!RegisterWindowClass()) {
    return false;
  }

  DWORD style = kWindowedStyle;
  int x = CW_USEDEFAULT;
  int y = CW_USEDEFAULT;
  int windowWidth = width_;
  int windowHeight = height_;

  switch (screenMode_) {
    case ScreenMode::Windowed: {
      RECT rect = {0, 0, width_, height_};
      AdjustWindowRect(&rect, style, FALSE);
      windowWidth = rect.right - rect.left;
      windowHeight = rect.bottom - rect.top;
      break;
    }
    case ScreenMode::Borderless:
    case ScreenMode::Fullscreen:
      style = WS_POPUP;
      x = 0;
      y = 0;
      windowWidth = width_;
      windowHeight = height_;
      break;
  }

  hwnd_ = CreateWindowExW(
      0,
      kClassName,
      title,
      style,
      x,
      y,
      windowWidth,
      windowHeight,
      nullptr,
      nullptr,
      g_hInstance,
      this);

  if (!hwnd_) {
    return false;
  }

  if (iconPath) {
    iconBig_ = static_cast<HICON>(LoadImageW(
        nullptr, iconPath, IMAGE_ICON, GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE));
    iconSmall_ = static_cast<HICON>(LoadImageW(
        nullptr, iconPath, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE));
    if (iconBig_) {
      SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconBig_));
    }
    if (iconSmall_) {
      SendMessageW(hwnd_, WM_SETICON, ICON_SMALL,
                   reinterpret_cast<LPARAM>(iconSmall_));
    }
  }

  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
  return true;
}

bool Window::InitDirectX() {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 1;
  sd.BufferDesc.Width = static_cast<UINT>(width_);
  sd.BufferDesc.Height = static_cast<UINT>(height_);
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd_;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  constexpr D3D_FEATURE_LEVEL levels[] = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
  };
  D3D_FEATURE_LEVEL featureLevel{};

  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      nullptr,
      D3D_DRIVER_TYPE_HARDWARE,
      nullptr,
      0,
      levels,
      ARRAYSIZE(levels),
      D3D11_SDK_VERSION,
      &sd,
      &swapChain_,
      &device_,
      &featureLevel,
      &context_);

  if (FAILED(hr)) {
    return false;
  }

  if (screenMode_ == ScreenMode::Fullscreen) {
    hr = swapChain_->SetFullscreenState(TRUE, nullptr);
    if (FAILED(hr)) {
      return false;
    }
  }

  if (!CreateBackbufferRtv() || !InitBlitResources() || !CreateSceneTargets()) {
    return false;
  }

  BindSceneTarget();
  return true;
}

bool Window::CreateBackbufferRtv() {
  if (!swapChain_ || !device_) {
    return false;
  }
  if (backbuffer_rtv_) {
    backbuffer_rtv_->Release();
    backbuffer_rtv_ = nullptr;
  }

  ID3D11Texture2D* backBuffer = nullptr;
  HRESULT hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&backBuffer));
  if (FAILED(hr)) {
    return false;
  }

  hr = device_->CreateRenderTargetView(backBuffer, nullptr, &backbuffer_rtv_);
  backBuffer->Release();
  return SUCCEEDED(hr);
}

void Window::ReleaseSceneTargets() {
  if (resolve_srv_) {
    resolve_srv_->Release();
    resolve_srv_ = nullptr;
  }
  if (resolve_tex_) {
    resolve_tex_->Release();
    resolve_tex_ = nullptr;
  }
  if (scene_srv_) {
    scene_srv_->Release();
    scene_srv_ = nullptr;
  }
  if (scene_rtv_) {
    scene_rtv_->Release();
    scene_rtv_ = nullptr;
  }
  if (scene_tex_) {
    scene_tex_->Release();
    scene_tex_ = nullptr;
  }
}

bool Window::CreateSceneTargets() {
  ReleaseSceneTargets();
  if (!device_ || width_ <= 0 || height_ <= 0) {
    return false;
  }

  UINT sample_count = static_cast<UINT>(NormalizeMsaaSamples(msaa_samples_));
  UINT quality = 0;
  if (sample_count > 1) {
    UINT levels = 0;
    device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,
                                           sample_count, &levels);
    while (sample_count > 1 && levels == 0) {
      sample_count /= 2;
      if (sample_count > 1) {
        device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,
                                               sample_count, &levels);
      }
    }
    if (levels > 0) {
      quality = levels - 1;
    } else {
      sample_count = 1;
      quality = 0;
    }
  }
  msaa_samples_ = static_cast<int>(sample_count);

  // Always render UI into an offscreen color target (OpenGL-FBO style).
  D3D11_TEXTURE2D_DESC td{};
  td.Width = static_cast<UINT>(width_);
  td.Height = static_cast<UINT>(height_);
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = sample_count;
  td.SampleDesc.Quality = quality;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (sample_count == 1) {
    td.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
  }

  HRESULT hr = device_->CreateTexture2D(&td, nullptr, &scene_tex_);
  if (FAILED(hr)) {
    return false;
  }

  hr = device_->CreateRenderTargetView(scene_tex_, nullptr, &scene_rtv_);
  if (FAILED(hr)) {
    ReleaseSceneTargets();
    return false;
  }

  if (sample_count == 1) {
    hr = device_->CreateShaderResourceView(scene_tex_, nullptr, &scene_srv_);
    if (FAILED(hr)) {
      ReleaseSceneTargets();
      return false;
    }
    return true;
  }

  D3D11_TEXTURE2D_DESC rd = td;
  rd.SampleDesc.Count = 1;
  rd.SampleDesc.Quality = 0;
  rd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  hr = device_->CreateTexture2D(&rd, nullptr, &resolve_tex_);
  if (FAILED(hr)) {
    ReleaseSceneTargets();
    return false;
  }
  hr = device_->CreateShaderResourceView(resolve_tex_, nullptr, &resolve_srv_);
  if (FAILED(hr)) {
    ReleaseSceneTargets();
    return false;
  }
  return true;
}

bool Window::InitBlitResources() {
  ReleaseBlitResources();
  if (!device_) {
    return false;
  }

  if (!blit_shader_.create_shader(device_, kBlitVS, kBlitPS)) {
    return false;
  }

  D3D11_BUFFER_DESC cbd{};
  cbd.ByteWidth = 16;  // float4
  cbd.Usage = D3D11_USAGE_DYNAMIC;
  cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  HRESULT hr = device_->CreateBuffer(&cbd, nullptr, &blit_cb_);
  if (FAILED(hr)) {
    return false;
  }

  D3D11_SAMPLER_DESC samp{};
  samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  samp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  samp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
  samp.MaxLOD = D3D11_FLOAT32_MAX;
  hr = device_->CreateSamplerState(&samp, &blit_sampler_);
  if (FAILED(hr)) {
    return false;
  }

  D3D11_BLEND_DESC blend{};
  blend.RenderTarget[0].BlendEnable = FALSE;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  hr = device_->CreateBlendState(&blend, &blit_blend_);
  if (FAILED(hr)) {
    return false;
  }

  D3D11_RASTERIZER_DESC rast{};
  rast.FillMode = D3D11_FILL_SOLID;
  rast.CullMode = D3D11_CULL_NONE;
  rast.DepthClipEnable = TRUE;
  rast.ScissorEnable = FALSE;
  hr = device_->CreateRasterizerState(&rast, &blit_raster_);
  return SUCCEEDED(hr);
}

void Window::ReleaseBlitResources() {
  if (blit_cb_) {
    blit_cb_->Release();
    blit_cb_ = nullptr;
  }
  if (blit_sampler_) {
    blit_sampler_->Release();
    blit_sampler_ = nullptr;
  }
  if (blit_blend_) {
    blit_blend_->Release();
    blit_blend_ = nullptr;
  }
  if (blit_raster_) {
    blit_raster_->Release();
    blit_raster_ = nullptr;
  }
}

void Window::BindSceneTarget() {
  if (!context_ || !scene_rtv_) {
    return;
  }
  context_->OMSetRenderTargets(1, &scene_rtv_, nullptr);

  D3D11_VIEWPORT vp{};
  vp.Width = static_cast<float>(width_);
  vp.Height = static_cast<float>(height_);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  context_->RSSetViewports(1, &vp);
}

void Window::BlitSceneToBackbuffer() {
  ID3D11ShaderResourceView* src_srv =
      resolve_srv_ ? resolve_srv_ : scene_srv_;
  if (!context_ || !backbuffer_rtv_ || !src_srv || !blit_cb_) {
    return;
  }

  context_->OMSetRenderTargets(1, &backbuffer_rtv_, nullptr);

  D3D11_VIEWPORT vp{};
  vp.Width = static_cast<float>(width_);
  vp.Height = static_cast<float>(height_);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  context_->RSSetViewports(1, &vp);

  if (blit_raster_) {
    context_->RSSetState(blit_raster_);
  }
  const float blend_factor[4] = {0, 0, 0, 0};
  context_->OMSetBlendState(blit_blend_, blend_factor, 0xffffffff);

  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (SUCCEEDED(context_->Map(blit_cb_, 0, D3D11_MAP_WRITE_DISCARD, 0,
                              &mapped))) {
    float* data = static_cast<float*>(mapped.pData);
    data[0] = brightness_;
    data[1] = 0.f;
    data[2] = 0.f;
    data[3] = 0.f;
    context_->Unmap(blit_cb_, 0);
  }

  blit_shader_.bind(context_);
  context_->PSSetConstantBuffers(0, 1, &blit_cb_);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  UINT stride = 0;
  UINT offset = 0;
  ID3D11Buffer* null_vb = nullptr;
  context_->IASetVertexBuffers(0, 1, &null_vb, &stride, &offset);
  context_->PSSetShaderResources(0, 1, &src_srv);
  context_->PSSetSamplers(0, 1, &blit_sampler_);
  context_->Draw(3, 0);

  ID3D11ShaderResourceView* null_srv = nullptr;
  context_->PSSetShaderResources(0, 1, &null_srv);
}

void Window::Cleanup() {
  if (swapChain_) {
    BOOL isFullscreen = FALSE;
    swapChain_->GetFullscreenState(&isFullscreen, nullptr);
    if (isFullscreen) {
      swapChain_->SetFullscreenState(FALSE, nullptr);
    }
  }

  if (context_) {
    context_->OMSetRenderTargets(0, nullptr, nullptr);
  }

  ReleaseSceneTargets();
  ReleaseBlitResources();

  if (backbuffer_rtv_) {
    backbuffer_rtv_->Release();
    backbuffer_rtv_ = nullptr;
  }
  if (swapChain_) {
    swapChain_->Release();
    swapChain_ = nullptr;
  }
  if (context_) {
    context_->Release();
    context_ = nullptr;
  }
  if (device_) {
    device_->Release();
    device_ = nullptr;
  }
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (iconBig_) {
    DestroyIcon(iconBig_);
    iconBig_ = nullptr;
  }
  if (iconSmall_) {
    DestroyIcon(iconSmall_);
    iconSmall_ = nullptr;
  }
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) {
  if (msg == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    auto* self = static_cast<Window*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }

  auto* self =
      reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self) {
    self->HandleInputMessage(msg, wParam, lParam);
  }

  switch (msg) {
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_ERASEBKGND:
      return 1;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}
