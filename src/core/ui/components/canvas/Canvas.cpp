#include "Canvas.h"

#include "../../../UiContext.h"

#include <algorithm>
#include <cmath>
#include <d3d11.h>

namespace {

int ToPixelSize(float v) {
  return std::max(1, static_cast<int>(std::lround(v)));
}

}  // namespace

Canvas::Canvas() {
  const CanvasStylePreset& p = ui_get_style_presets().canvas;
  width = p.width;
  height = p.height;
  tint = p.tint;
  clear_color = p.clear_color;
  clear_depth = p.clear_depth;
  style_base = p.style_base;
  style_hovered = p.style_hovered;
  style_active = p.style_active;
  style_disabled = p.style_disabled;
  draw_command_buffer_.resize(1);
}

Canvas::~Canvas() {
  release_targets();
}

int Canvas::PixelWidth() const {
  return ToPixelSize(width);
}

int Canvas::PixelHeight() const {
  return ToPixelSize(height);
}

void Canvas::get_layout_size(float& out_w, float& out_h) const {
  out_w = width;
  out_h = height;
}

void Canvas::release_targets() {
  if (drawing_) {
    drawing_ = false;
  }

  if (texture_id_ >= 0 && ui) {
    ui->renderer.unload_texture(texture_id_);
    texture_id_ = -1;
  } else {
    texture_id_ = -1;
  }

  if (color_rtv_) {
    color_rtv_->Release();
    color_rtv_ = nullptr;
  }
  if (color_srv_) {
    color_srv_->Release();
    color_srv_ = nullptr;
  }
  if (color_tex_) {
    color_tex_->Release();
    color_tex_ = nullptr;
  }
  if (depth_dsv_) {
    depth_dsv_->Release();
    depth_dsv_ = nullptr;
  }
  if (depth_tex_) {
    depth_tex_->Release();
    depth_tex_ = nullptr;
  }
  tex_w_ = 0;
  tex_h_ = 0;
}

bool Canvas::ensure_targets(UiContext* ctx) {
  if (!ctx || !ctx->window.IsValid() || !ctx->renderer.is_valid()) {
    return false;
  }
  ui = ctx;

  const int w = PixelWidth();
  const int h = PixelHeight();
  if (color_rtv_ && depth_dsv_ && texture_id_ >= 0 && tex_w_ == w &&
      tex_h_ == h) {
    return true;
  }

  release_targets();
  ui = ctx;

  ID3D11Device* device = ctx->window.GetDevice();
  if (!device) {
    return false;
  }

  D3D11_TEXTURE2D_DESC color_desc{};
  color_desc.Width = static_cast<UINT>(w);
  color_desc.Height = static_cast<UINT>(h);
  color_desc.MipLevels = 1;
  color_desc.ArraySize = 1;
  color_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  color_desc.SampleDesc.Count = 1;
  color_desc.Usage = D3D11_USAGE_DEFAULT;
  color_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

  HRESULT hr = device->CreateTexture2D(&color_desc, nullptr, &color_tex_);
  if (FAILED(hr)) {
    release_targets();
    return false;
  }
  hr = device->CreateRenderTargetView(color_tex_, nullptr, &color_rtv_);
  if (FAILED(hr)) {
    release_targets();
    return false;
  }
  hr = device->CreateShaderResourceView(color_tex_, nullptr, &color_srv_);
  if (FAILED(hr)) {
    release_targets();
    return false;
  }

  D3D11_TEXTURE2D_DESC depth_desc = color_desc;
  depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  hr = device->CreateTexture2D(&depth_desc, nullptr, &depth_tex_);
  if (FAILED(hr)) {
    release_targets();
    return false;
  }
  hr = device->CreateDepthStencilView(depth_tex_, nullptr, &depth_dsv_);
  if (FAILED(hr)) {
    release_targets();
    return false;
  }

  texture_id_ =
      ctx->renderer.register_texture(color_tex_, color_srv_, w, h);
  if (texture_id_ < 0) {
    release_targets();
    return false;
  }

  tex_w_ = w;
  tex_h_ = h;
  return true;
}

bool Canvas::begin_draw(UiContext* ctx, bool clear) {
  if (!ensure_targets(ctx) || !color_rtv_ || !depth_dsv_) {
    return false;
  }

  ID3D11DeviceContext* context = ctx->window.GetContext();
  if (!context) {
    return false;
  }

  // Color can't be bound as RT and SRV at the same time.
  ID3D11ShaderResourceView* null_srvs[8] = {};
  context->PSSetShaderResources(0, 8, null_srvs);

  context->OMSetRenderTargets(1, &color_rtv_, depth_dsv_);

  D3D11_VIEWPORT vp{};
  vp.Width = static_cast<float>(tex_w_);
  vp.Height = static_cast<float>(tex_h_);
  vp.MinDepth = 0.f;
  vp.MaxDepth = 1.f;
  context->RSSetViewports(1, &vp);

  if (clear) {
    const float c[] = {clear_color.r, clear_color.g, clear_color.b,
                       clear_color.a};
    context->ClearRenderTargetView(color_rtv_, c);
    context->ClearDepthStencilView(depth_dsv_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                   clear_depth, 0);
  }

  drawing_ = true;
  return true;
}

void Canvas::end_draw(UiContext* ctx) {
  drawing_ = false;
  if (!ctx) {
    return;
  }
  ID3D11DeviceContext* context = ctx->window.GetContext();
  if (context) {
    ID3D11RenderTargetView* null_rtv = nullptr;
    context->OMSetRenderTargets(1, &null_rtv, nullptr);
  }
  ctx->window.BindSceneTarget();
}

void Canvas::collect_draw(std::vector<DrawCommand*>& out, bool force_rebuild) {
  if (ui) {
    ensure_targets(ui);
  }
  if (force_rebuild || !static_draw) {
    build_draw_buffer();
    reset_draw_origin_after_build();
  }
  emit_draw_buffer(out);
}

void Canvas::build_draw_buffer() {
  if (draw_command_buffer_.empty()) {
    draw_command_buffer_.resize(1);
  }
  DrawCommand& cmd = draw_command_buffer_[0];
  cmd.type = DrawCommandType::Image;
  cmd.x = x;
  cmd.y = y;
  cmd.width = width;
  cmd.height = height;
  cmd.color = tint;
  cmd.texture_id = texture_id_;
  cmd.layer = layer;
  cmd.u0 = 0.f;
  cmd.v0 = 0.f;
  cmd.u1 = 1.f;
  cmd.v1 = 1.f;
  cmd.text.clear();
}
