#pragma once

#include "../simple_ui.h"
#include "graphics/Shader.h"

class Window {
public:
  Window(const wchar_t* title, const ScreenSettings* screenSettings,
         const wchar_t* iconPath);
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  bool IsValid() const;
  bool ProcessMessages();
  void Clear(const Color& color);
  void Present();
  bool SetScreenMode(ScreenMode mode);
  ScreenMode GetScreenMode() const;

  // Preferred client size for Windowed (and stored for returning to Windowed).
  // When already Windowed, resizes the window and swap-chain immediately.
  bool SetScreenSize(int width, int height);
  int GetWindowedWidth() const;
  int GetWindowedHeight() const;

  bool SetMsaaSamples(int samples);
  int GetMsaaSamples() const;

  // Post-present brightness multiplier applied when blitting the offscreen
  // scene to the swap-chain backbuffer. 1.0 = unchanged. Clamped to >= 0.
  void SetBrightness(float brightness);
  float GetBrightness() const;

  HWND GetHwnd() const;
  ID3D11Device* GetDevice() const;
  ID3D11DeviceContext* GetContext() const;
  int GetWidth() const;
  int GetHeight() const;
  const MouseEvents& GetMouseEvents() const;
  const KeyboardEvents& GetKeyboardEvents() const;

private:
  bool InitWindow(const wchar_t* title, const wchar_t* iconPath);
  bool InitDirectX();
  bool InitBlitResources();
  bool CreateBackbufferRtv();
  bool CreateSceneTargets();
  void ReleaseSceneTargets();
  void ReleaseBlitResources();
  void BindSceneTarget();
  void BlitSceneToBackbuffer();
  bool ResizeSwapChain(UINT width, UINT height);
  void Cleanup();
  void BeginFrameInput();
  void HandleInputMessage(UINT msg, WPARAM wParam, LPARAM lParam);
  static bool RegisterWindowClass();
  static int NormalizeMsaaSamples(int samples);

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam);

  HWND hwnd_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  int windowed_width_ = 0;
  int windowed_height_ = 0;
  ScreenMode screenMode_ = ScreenMode::Windowed;
  int msaa_samples_ = 4;
  HICON iconBig_ = nullptr;
  HICON iconSmall_ = nullptr;

  MouseEvents mouse_{};
  KeyboardEvents keyboard_{};

  ID3D11Device* device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;
  IDXGISwapChain* swapChain_ = nullptr;

  // Swap-chain back buffer (final present target).
  ID3D11RenderTargetView* backbuffer_rtv_ = nullptr;

  // Offscreen scene color (may be MSAA). UI always renders here.
  ID3D11Texture2D* scene_tex_ = nullptr;
  ID3D11RenderTargetView* scene_rtv_ = nullptr;
  // Non-MSAA path: sample the scene texture directly after unbinding as RT.
  ID3D11ShaderResourceView* scene_srv_ = nullptr;
  // MSAA path: resolve into this texture, then sample for the blit.
  ID3D11Texture2D* resolve_tex_ = nullptr;
  ID3D11ShaderResourceView* resolve_srv_ = nullptr;

  Shader blit_shader_;
  ID3D11Buffer* blit_cb_ = nullptr;
  ID3D11SamplerState* blit_sampler_ = nullptr;
  ID3D11BlendState* blit_blend_ = nullptr;
  ID3D11RasterizerState* blit_raster_ = nullptr;

  // Linear RGB multiplier for the final blit (1.0 = identity).
  float brightness_ = 1.f;
};
