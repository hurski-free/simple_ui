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

  bool SetMsaaSamples(int samples);
  int GetMsaaSamples() const;

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

  // Offscreen scene color (may be MSAA). Future post-effects sample resolve_srv_.
  ID3D11Texture2D* scene_tex_ = nullptr;
  ID3D11RenderTargetView* scene_rtv_ = nullptr;
  ID3D11Texture2D* resolve_tex_ = nullptr;  // non-MSAA when msaa > 1
  ID3D11ShaderResourceView* resolve_srv_ = nullptr;

  Shader blit_shader_;
  ID3D11SamplerState* blit_sampler_ = nullptr;
  ID3D11BlendState* blit_blend_ = nullptr;
  ID3D11RasterizerState* blit_raster_ = nullptr;
};
