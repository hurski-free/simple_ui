#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace {

constexpr const char* kSolidVS = R"(
cbuffer ScreenCB : register(b0) {
  float2 screen_size;
  float2 padding;
};

struct VSIn {
  float2 pos : POSITION;
  float4 color : COLOR;
};

struct VSOut {
  float4 pos : SV_POSITION;
  float4 color : COLOR;
};

VSOut main(VSIn input) {
  VSOut output;
  float2 ndc;
  ndc.x = (input.pos.x / screen_size.x) * 2.0f - 1.0f;
  ndc.y = 1.0f - (input.pos.y / screen_size.y) * 2.0f;
  output.pos = float4(ndc, 0.0f, 1.0f);
  output.color = input.color;
  return output;
}
)";

constexpr const char* kSolidPS = R"(
struct PSIn {
  float4 pos : SV_POSITION;
  float4 color : COLOR;
};

float4 main(PSIn input) : SV_TARGET {
  return input.color;
}
)";

constexpr const char* kTextVS = R"(
cbuffer ScreenCB : register(b0) {
  float2 screen_size;
  float2 padding;
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
  VSOut output;
  float2 ndc;
  ndc.x = (input.pos.x / screen_size.x) * 2.0f - 1.0f;
  ndc.y = 1.0f - (input.pos.y / screen_size.y) * 2.0f;
  output.pos = float4(ndc, 0.0f, 1.0f);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}
)";

constexpr const char* kTextPS = R"(
Texture2D font_atlas : register(t0);
SamplerState font_sampler : register(s0);

struct PSIn {
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

float4 main(PSIn input) : SV_TARGET {
  float alpha = font_atlas.Sample(font_sampler, input.uv).r;
  return float4(input.color.rgb, input.color.a * alpha);
}
)";

constexpr const char* kImageVS = R"(
cbuffer ScreenCB : register(b0) {
  float2 screen_size;
  float2 padding;
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
  VSOut output;
  float2 ndc;
  ndc.x = (input.pos.x / screen_size.x) * 2.0f - 1.0f;
  ndc.y = 1.0f - (input.pos.y / screen_size.y) * 2.0f;
  output.pos = float4(ndc, 0.0f, 1.0f);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}
)";

constexpr const char* kImagePS = R"(
Texture2D image_tex : register(t0);
SamplerState image_sampler : register(s0);

struct PSIn {
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

float4 main(PSIn input) : SV_TARGET {
  return image_tex.Sample(image_sampler, input.uv) * input.color;
}
)";

constexpr const char* kShapeVS = R"(
cbuffer ScreenCB : register(b0) {
  float2 screen_size;
  float2 padding;
};

struct VSIn {
  float2 pos : POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
  float4 params : TEXCOORD1; // radius, width, height, kind
};

struct VSOut {
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
  float4 params : TEXCOORD1;
};

VSOut main(VSIn input) {
  VSOut output;
  float2 ndc;
  ndc.x = (input.pos.x / screen_size.x) * 2.0f - 1.0f;
  ndc.y = 1.0f - (input.pos.y / screen_size.y) * 2.0f;
  output.pos = float4(ndc, 0.0f, 1.0f);
  output.uv = input.uv;
  output.color = input.color;
  output.params = input.params;
  return output;
}
)";

// Analytic SDF shapes with screen-space AA via fwidth (smooth edges).
constexpr const char* kShapePS = R"(
struct PSIn {
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
  float4 params : TEXCOORD1;
};

float sdEllipse(float2 uv) {
  float2 p = (uv - 0.5f) * 2.0f;
  return length(p) - 1.0f;
}

float sdRoundedBox(float2 uv, float2 size, float radius) {
  float2 p = (uv - 0.5f) * size;
  float2 halfSize = size * 0.5f;
  float r = min(radius, min(halfSize.x, halfSize.y));
  float2 q = abs(p) - halfSize + r;
  return length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - r;
}

float4 main(PSIn input) : SV_TARGET {
  const float radius = input.params.x;
  const float2 size = input.params.yz;
  const float kind = input.params.w;

  float d = (kind < 0.5f)
                ? sdEllipse(input.uv)
                : sdRoundedBox(input.uv, size, radius);

  // ~1 pixel AA band; smoothstep avoids hard polygonal silhouette.
  float aa = max(fwidth(d), 1e-4f);
  float coverage = 1.0f - smoothstep(-aa, aa, d);
  return float4(input.color.rgb, input.color.a * coverage);
}
)";

}  // namespace

Renderer::~Renderer() {
  ReleaseResources();
}

bool Renderer::init(ID3D11Device* device) {
  if (!device) {
    return false;
  }

  ReleaseResources();
  device_ = device;
  device_->AddRef();

  if (!solid_shader_.create_shader(device_, kSolidVS, kSolidPS) ||
      !text_shader_.create_shader(device_, kTextVS, kTextPS) ||
      !image_shader_.create_shader(device_, kImageVS, kImagePS) ||
      !shape_shader_.create_shader(device_, kShapeVS, kShapePS)) {
    ReleaseResources();
    return false;
  }

  if (!CreateResources()) {
    ReleaseResources();
    return false;
  }

  initialized_ = true;
  return true;
}

bool Renderer::register_font_atlas(const FontAtlas& atlas) {
  if (!initialized_ || !device_ || !atlas.is_valid()) {
    return false;
  }
  if (font_gpus_.find(&atlas) != font_gpus_.end()) {
    return true;
  }

  FontGpu gpu{};
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(atlas.atlas_width());
  desc.Height = static_cast<UINT>(atlas.atlas_height());
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = atlas.pixels();
  data.SysMemPitch = static_cast<UINT>(atlas.atlas_width());

  HRESULT hr = device_->CreateTexture2D(&desc, &data, &gpu.texture);
  if (FAILED(hr)) {
    return false;
  }

  hr = device_->CreateShaderResourceView(gpu.texture, nullptr, &gpu.srv);
  if (FAILED(hr)) {
    gpu.texture->Release();
    return false;
  }

  font_gpus_[&atlas] = gpu;
  return true;
}

void Renderer::unregister_font_atlas(const FontAtlas* atlas) {
  if (!atlas) {
    return;
  }
  auto it = font_gpus_.find(atlas);
  if (it == font_gpus_.end()) {
    return;
  }
  if (it->second.srv) {
    it->second.srv->Release();
  }
  if (it->second.texture) {
    it->second.texture->Release();
  }
  font_gpus_.erase(it);
  if (default_font_atlas_ == atlas) {
    default_font_atlas_ = nullptr;
  }
  if (active_font_atlas_ == atlas) {
    active_font_atlas_ = nullptr;
  }
  if (text_batch_atlas_ == atlas) {
    text_batch_atlas_ = nullptr;
  }
  if (bound_font_srv_ && font_gpus_.empty()) {
    bound_font_srv_ = nullptr;
  }
}

void Renderer::set_default_font_atlas(const FontAtlas* atlas) {
  default_font_atlas_ = atlas;
}

bool Renderer::is_valid() const {
  return initialized_;
}

bool Renderer::CreateResources() {
  D3D11_INPUT_ELEMENT_DESC solid_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  ID3DBlob* solid_blob = solid_shader_.vs_blob();
  if (!solid_blob) {
    return false;
  }

  HRESULT hr = device_->CreateInputLayout(
      solid_layout, ARRAYSIZE(solid_layout), solid_blob->GetBufferPointer(),
      solid_blob->GetBufferSize(), &solid_layout_);
  if (FAILED(hr)) {
    return false;
  }

  D3D11_INPUT_ELEMENT_DESC text_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  ID3DBlob* text_blob = text_shader_.vs_blob();
  if (!text_blob) {
    return false;
  }

  hr = device_->CreateInputLayout(text_layout, ARRAYSIZE(text_layout),
                                  text_blob->GetBufferPointer(),
                                  text_blob->GetBufferSize(), &text_layout_);
  if (FAILED(hr)) {
    return false;
  }

  ID3DBlob* image_blob = image_shader_.vs_blob();
  if (!image_blob) {
    return false;
  }
  hr = device_->CreateInputLayout(text_layout, ARRAYSIZE(text_layout),
                                  image_blob->GetBufferPointer(),
                                  image_blob->GetBufferSize(), &image_layout_);
  if (FAILED(hr)) {
    return false;
  }

  D3D11_INPUT_ELEMENT_DESC shape_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  ID3DBlob* shape_blob = shape_shader_.vs_blob();
  if (!shape_blob) {
    return false;
  }
  hr = device_->CreateInputLayout(shape_layout, ARRAYSIZE(shape_layout),
                                  shape_blob->GetBufferPointer(),
                                  shape_blob->GetBufferSize(), &shape_layout_);
  if (FAILED(hr)) {
    return false;
  }

  constexpr UINT kInitialSolidVerts = 6 * 1024;
  constexpr UINT kInitialTextVerts = 6 * 1024;
  constexpr UINT kInitialShapeVerts = 6 * 256;

  D3D11_BUFFER_DESC solid_vb{};
  solid_vb.ByteWidth = sizeof(SolidVertex) * kInitialSolidVerts;
  solid_vb.Usage = D3D11_USAGE_DYNAMIC;
  solid_vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  solid_vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = device_->CreateBuffer(&solid_vb, nullptr, &solid_vb_);
  if (FAILED(hr)) {
    return false;
  }
  solid_vb_capacity_ = kInitialSolidVerts;

  D3D11_BUFFER_DESC text_vb{};
  text_vb.ByteWidth = sizeof(TextVertex) * kInitialTextVerts;
  text_vb.Usage = D3D11_USAGE_DYNAMIC;
  text_vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  text_vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = device_->CreateBuffer(&text_vb, nullptr, &text_vb_);
  if (FAILED(hr)) {
    return false;
  }
  text_vb_capacity_ = kInitialTextVerts;

  D3D11_BUFFER_DESC image_vb = text_vb;
  hr = device_->CreateBuffer(&image_vb, nullptr, &image_vb_);
  if (FAILED(hr)) {
    return false;
  }

  D3D11_BUFFER_DESC shape_vb{};
  shape_vb.ByteWidth = sizeof(ShapeVertex) * kInitialShapeVerts;
  shape_vb.Usage = D3D11_USAGE_DYNAMIC;
  shape_vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  shape_vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = device_->CreateBuffer(&shape_vb, nullptr, &shape_vb_);
  if (FAILED(hr)) {
    return false;
  }
  shape_vb_capacity_ = kInitialShapeVerts;

  D3D11_BUFFER_DESC cb_desc{};
  cb_desc.ByteWidth = 16;
  cb_desc.Usage = D3D11_USAGE_DYNAMIC;
  cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = device_->CreateBuffer(&cb_desc, nullptr, &screen_cb_);
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
  hr = device_->CreateSamplerState(&samp, &font_sampler_);
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
  rast.ScissorEnable = TRUE;
  hr = device_->CreateRasterizerState(&rast, &raster_state_);
  return SUCCEEDED(hr);
}

void Renderer::ReleaseFontTextures() {
  for (auto& pair : font_gpus_) {
    if (pair.second.srv) {
      pair.second.srv->Release();
    }
    if (pair.second.texture) {
      pair.second.texture->Release();
    }
  }
  font_gpus_.clear();
  default_font_atlas_ = nullptr;
  active_font_atlas_ = nullptr;
  text_batch_atlas_ = nullptr;
  bound_font_srv_ = nullptr;
}

void Renderer::ReleaseResources() {
  ReleaseFontTextures();
  ReleaseUserTextures();
  if (font_sampler_) {
    font_sampler_->Release();
    font_sampler_ = nullptr;
  }
  if (blend_state_) {
    blend_state_->Release();
    blend_state_ = nullptr;
  }
  if (raster_state_) {
    raster_state_->Release();
    raster_state_ = nullptr;
  }
  if (screen_cb_) {
    screen_cb_->Release();
    screen_cb_ = nullptr;
  }
  if (image_vb_) {
    image_vb_->Release();
    image_vb_ = nullptr;
  }
  if (shape_vb_) {
    shape_vb_->Release();
    shape_vb_ = nullptr;
  }
  if (text_vb_) {
    text_vb_->Release();
    text_vb_ = nullptr;
  }
  if (solid_vb_) {
    solid_vb_->Release();
    solid_vb_ = nullptr;
  }
  solid_vb_capacity_ = 0;
  text_vb_capacity_ = 0;
  shape_vb_capacity_ = 0;
  if (image_layout_) {
    image_layout_->Release();
    image_layout_ = nullptr;
  }
  if (shape_layout_) {
    shape_layout_->Release();
    shape_layout_ = nullptr;
  }
  if (text_layout_) {
    text_layout_->Release();
    text_layout_ = nullptr;
  }
  if (solid_layout_) {
    solid_layout_->Release();
    solid_layout_ = nullptr;
  }
  if (device_) {
    device_->Release();
    device_ = nullptr;
  }
  solid_batch_.clear();
  text_batch_.clear();
  shape_batch_.clear();
  initialized_ = false;
}

void Renderer::ReleaseUserTextures() {
  for (auto& pair : textures_) {
    if (pair.second.srv) {
      pair.second.srv->Release();
    }
    if (pair.second.texture) {
      pair.second.texture->Release();
    }
  }
  textures_.clear();
}

int Renderer::AllocTextureId() {
  return next_texture_id_++;
}

bool Renderer::UploadRgbaTexture(int width, int height,
                                 const unsigned char* rgba,
                                 TextureEntry& out) {
  if (!device_ || !rgba || width <= 0 || height <= 0) {
    return false;
  }

  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = rgba;
  data.SysMemPitch = static_cast<UINT>(width * 4);

  HRESULT hr = device_->CreateTexture2D(&desc, &data, &out.texture);
  if (FAILED(hr)) {
    return false;
  }
  hr = device_->CreateShaderResourceView(out.texture, nullptr, &out.srv);
  if (FAILED(hr)) {
    out.texture->Release();
    out.texture = nullptr;
    return false;
  }
  out.width = width;
  out.height = height;
  return true;
}

int Renderer::create_texture_rgba(int width, int height,
                                  const unsigned char* rgba) {
  if (!initialized_) {
    return -1;
  }
  TextureEntry entry;
  if (!UploadRgbaTexture(width, height, rgba, entry)) {
    return -1;
  }
  const int id = AllocTextureId();
  textures_[id] = entry;
  return id;
}

int Renderer::register_texture(ID3D11Texture2D* texture,
                               ID3D11ShaderResourceView* srv, int width,
                               int height) {
  if (!initialized_ || !texture || !srv || width <= 0 || height <= 0) {
    return -1;
  }
  texture->AddRef();
  srv->AddRef();
  TextureEntry entry;
  entry.texture = texture;
  entry.srv = srv;
  entry.width = width;
  entry.height = height;
  const int id = AllocTextureId();
  textures_[id] = entry;
  return id;
}

int Renderer::load_texture_file(const wchar_t* path) {
  if (!initialized_ || !path) {
    return -1;
  }

  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool co_init = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

  IWICImagingFactory* factory = nullptr;
  hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(&factory));
  if (FAILED(hr) || !factory) {
    if (co_init && hr != RPC_E_CHANGED_MODE) {
      CoUninitialize();
    }
    return -1;
  }

  IWICBitmapDecoder* decoder = nullptr;
  hr = factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                          WICDecodeMetadataCacheOnLoad,
                                          &decoder);
  if (FAILED(hr) || !decoder) {
    factory->Release();
    return -1;
  }

  IWICBitmapFrameDecode* frame = nullptr;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr) || !frame) {
    decoder->Release();
    factory->Release();
    return -1;
  }

  IWICFormatConverter* converter = nullptr;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr) || !converter) {
    frame->Release();
    decoder->Release();
    factory->Release();
    return -1;
  }

  hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return -1;
  }

  UINT w = 0;
  UINT h = 0;
  converter->GetSize(&w, &h);
  std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
  hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()),
                             pixels.data());

  converter->Release();
  frame->Release();
  decoder->Release();
  factory->Release();

  if (FAILED(hr)) {
    return -1;
  }

  return create_texture_rgba(static_cast<int>(w), static_cast<int>(h),
                             pixels.data());
}

void Renderer::unload_texture(int texture_id) {
  auto it = textures_.find(texture_id);
  if (it == textures_.end()) {
    return;
  }
  if (it->second.srv) {
    it->second.srv->Release();
  }
  if (it->second.texture) {
    it->second.texture->Release();
  }
  textures_.erase(it);
}

bool Renderer::get_texture_size(int texture_id, int* out_w, int* out_h) const {
  auto it = textures_.find(texture_id);
  if (it == textures_.end()) {
    return false;
  }
  if (out_w) {
    *out_w = it->second.width;
  }
  if (out_h) {
    *out_h = it->second.height;
  }
  return true;
}

bool Renderer::EnsureSolidCapacity(UINT vertex_count) {
  if (vertex_count <= solid_vb_capacity_ && solid_vb_) {
    return true;
  }

  UINT new_cap = solid_vb_capacity_ > 0 ? solid_vb_capacity_ : 6u;
  while (new_cap < vertex_count) {
    new_cap *= 2;
  }

  D3D11_BUFFER_DESC desc{};
  desc.ByteWidth = sizeof(SolidVertex) * new_cap;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  ID3D11Buffer* new_vb = nullptr;
  if (FAILED(device_->CreateBuffer(&desc, nullptr, &new_vb))) {
    return false;
  }

  if (solid_vb_) {
    solid_vb_->Release();
  }
  solid_vb_ = new_vb;
  solid_vb_capacity_ = new_cap;
  solid_pipeline_bound_ = false;
  return true;
}

bool Renderer::EnsureTextCapacity(UINT vertex_count) {
  if (vertex_count <= text_vb_capacity_ && text_vb_) {
    return true;
  }

  UINT new_cap = text_vb_capacity_ > 0 ? text_vb_capacity_ : 6u;
  while (new_cap < vertex_count) {
    new_cap *= 2;
  }

  D3D11_BUFFER_DESC desc{};
  desc.ByteWidth = sizeof(TextVertex) * new_cap;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  ID3D11Buffer* new_vb = nullptr;
  if (FAILED(device_->CreateBuffer(&desc, nullptr, &new_vb))) {
    return false;
  }

  if (text_vb_) {
    text_vb_->Release();
  }
  text_vb_ = new_vb;
  text_vb_capacity_ = new_cap;
  text_pipeline_bound_ = false;
  return true;
}

bool Renderer::EnsureShapeCapacity(UINT vertex_count) {
  if (vertex_count <= shape_vb_capacity_ && shape_vb_) {
    return true;
  }

  UINT new_cap = shape_vb_capacity_ > 0 ? shape_vb_capacity_ : 6u;
  while (new_cap < vertex_count) {
    new_cap *= 2;
  }

  D3D11_BUFFER_DESC desc{};
  desc.ByteWidth = sizeof(ShapeVertex) * new_cap;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  ID3D11Buffer* new_vb = nullptr;
  if (FAILED(device_->CreateBuffer(&desc, nullptr, &new_vb))) {
    return false;
  }

  if (shape_vb_) {
    shape_vb_->Release();
  }
  shape_vb_ = new_vb;
  shape_vb_capacity_ = new_cap;
  shape_pipeline_bound_ = false;
  return true;
}

void Renderer::BindSolidPipeline(ID3D11DeviceContext* context) {
  if (solid_pipeline_bound_) {
    return;
  }
  solid_shader_.bind(context);
  context->IASetInputLayout(solid_layout_);
  UINT stride = sizeof(SolidVertex);
  UINT offset = 0;
  context->IASetVertexBuffers(0, 1, &solid_vb_, &stride, &offset);
  ID3D11ShaderResourceView* null_srv = nullptr;
  context->PSSetShaderResources(0, 1, &null_srv);
  solid_pipeline_bound_ = true;
  text_pipeline_bound_ = false;
  image_pipeline_bound_ = false;
  shape_pipeline_bound_ = false;
  bound_font_srv_ = nullptr;
}

void Renderer::BindTextPipeline(ID3D11DeviceContext* context,
                                ID3D11ShaderResourceView* font_srv) {
  if (!text_pipeline_bound_) {
    text_shader_.bind(context);
    context->IASetInputLayout(text_layout_);
    UINT stride = sizeof(TextVertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &text_vb_, &stride, &offset);
    context->PSSetSamplers(0, 1, &font_sampler_);
    text_pipeline_bound_ = true;
    solid_pipeline_bound_ = false;
    image_pipeline_bound_ = false;
    shape_pipeline_bound_ = false;
  }
  if (bound_font_srv_ != font_srv) {
    context->PSSetShaderResources(0, 1, &font_srv);
    bound_font_srv_ = font_srv;
  }
}

void Renderer::BindShapePipeline(ID3D11DeviceContext* context) {
  if (shape_pipeline_bound_) {
    return;
  }
  shape_shader_.bind(context);
  context->IASetInputLayout(shape_layout_);
  UINT stride = sizeof(ShapeVertex);
  UINT offset = 0;
  context->IASetVertexBuffers(0, 1, &shape_vb_, &stride, &offset);
  ID3D11ShaderResourceView* null_srv = nullptr;
  context->PSSetShaderResources(0, 1, &null_srv);
  shape_pipeline_bound_ = true;
  solid_pipeline_bound_ = false;
  text_pipeline_bound_ = false;
  image_pipeline_bound_ = false;
  bound_font_srv_ = nullptr;
}

void Renderer::BindImagePipeline(ID3D11DeviceContext* context,
                                 ID3D11ShaderResourceView* srv) {
  image_shader_.bind(context);
  context->IASetInputLayout(image_layout_);
  context->PSSetShaderResources(0, 1, &srv);
  context->PSSetSamplers(0, 1, &font_sampler_);
  UINT stride = sizeof(TextVertex);
  UINT offset = 0;
  context->IASetVertexBuffers(0, 1, &image_vb_, &stride, &offset);
  image_pipeline_bound_ = true;
  solid_pipeline_bound_ = false;
  text_pipeline_bound_ = false;
  shape_pipeline_bound_ = false;
  bound_font_srv_ = nullptr;
}

void Renderer::DrawImageCommand(ID3D11DeviceContext* context,
                                const DrawCommand& cmd) {
  auto it = textures_.find(cmd.texture_id);
  if (it == textures_.end() || !it->second.srv) {
    return;
  }

  FlushAllBatches(context);

  const float x0 = cmd.x;
  const float y0 = cmd.y;
  const float x1 = cmd.x + cmd.width;
  const float y1 = cmd.y + cmd.height;
  const Color& c = cmd.color;
  TextVertex quad[6] = {
      {x0, y0, cmd.u0, cmd.v0, c.r, c.g, c.b, c.a},
      {x1, y0, cmd.u1, cmd.v0, c.r, c.g, c.b, c.a},
      {x0, y1, cmd.u0, cmd.v1, c.r, c.g, c.b, c.a},
      {x1, y0, cmd.u1, cmd.v0, c.r, c.g, c.b, c.a},
      {x1, y1, cmd.u1, cmd.v1, c.r, c.g, c.b, c.a},
      {x0, y1, cmd.u0, cmd.v1, c.r, c.g, c.b, c.a},
  };

  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(image_vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    return;
  }
  std::memcpy(mapped.pData, quad, sizeof(quad));
  context->Unmap(image_vb_, 0);

  BindImagePipeline(context, it->second.srv);
  context->Draw(6, 0);
}

void Renderer::FlushSolidBatch(ID3D11DeviceContext* context) {
  if (solid_batch_.empty()) {
    return;
  }

  const UINT count = static_cast<UINT>(solid_batch_.size());
  if (!EnsureSolidCapacity(count)) {
    solid_batch_.clear();
    return;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(solid_vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    solid_batch_.clear();
    return;
  }
  std::memcpy(mapped.pData, solid_batch_.data(),
              sizeof(SolidVertex) * count);
  context->Unmap(solid_vb_, 0);

  BindSolidPipeline(context);
  context->Draw(count, 0);
  solid_batch_.clear();
}

void Renderer::FlushTextBatch(ID3D11DeviceContext* context) {
  if (text_batch_.empty()) {
    return;
  }

  ID3D11ShaderResourceView* srv = FontSrv(text_batch_atlas_);
  if (!srv) {
    text_batch_.clear();
    text_batch_atlas_ = nullptr;
    return;
  }

  const UINT count = static_cast<UINT>(text_batch_.size());
  if (!EnsureTextCapacity(count)) {
    text_batch_.clear();
    text_batch_atlas_ = nullptr;
    return;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(text_vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    text_batch_.clear();
    text_batch_atlas_ = nullptr;
    return;
  }
  std::memcpy(mapped.pData, text_batch_.data(), sizeof(TextVertex) * count);
  context->Unmap(text_vb_, 0);

  BindTextPipeline(context, srv);
  context->Draw(count, 0);
  text_batch_.clear();
  text_batch_atlas_ = nullptr;
}

void Renderer::FlushAllBatches(ID3D11DeviceContext* context) {
  FlushSolidBatch(context);
  FlushShapeBatch(context);
  FlushTextBatch(context);
}

void Renderer::FlushShapeBatch(ID3D11DeviceContext* context) {
  if (shape_batch_.empty()) {
    return;
  }

  const UINT count = static_cast<UINT>(shape_batch_.size());
  if (!EnsureShapeCapacity(count)) {
    shape_batch_.clear();
    return;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(shape_vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    shape_batch_.clear();
    return;
  }
  std::memcpy(mapped.pData, shape_batch_.data(), sizeof(ShapeVertex) * count);
  context->Unmap(shape_vb_, 0);

  BindShapePipeline(context);
  context->Draw(count, 0);
  shape_batch_.clear();
}

void Renderer::AppendSolidRect(const DrawCommand& cmd) {
  const float x0 = cmd.x;
  const float y0 = cmd.y;
  const float x1 = cmd.x + cmd.width;
  const float y1 = cmd.y + cmd.height;
  const Color& c = cmd.color;

  const size_t base = solid_batch_.size();
  solid_batch_.resize(base + 6);
  solid_batch_[base + 0] = {x0, y0, c.r, c.g, c.b, c.a};
  solid_batch_[base + 1] = {x1, y0, c.r, c.g, c.b, c.a};
  solid_batch_[base + 2] = {x0, y1, c.r, c.g, c.b, c.a};
  solid_batch_[base + 3] = {x1, y0, c.r, c.g, c.b, c.a};
  solid_batch_[base + 4] = {x1, y1, c.r, c.g, c.b, c.a};
  solid_batch_[base + 5] = {x0, y1, c.r, c.g, c.b, c.a};
}

void Renderer::AppendSdfShape(const DrawCommand& cmd) {
  if (cmd.width <= 0.f || cmd.height <= 0.f || cmd.color.a <= 0.f) {
    return;
  }

  // Expand quad slightly so the AA fringe is not clipped at the edge.
  constexpr float kPad = 1.5f;
  const float x0 = cmd.x - kPad;
  const float y0 = cmd.y - kPad;
  const float x1 = cmd.x + cmd.width + kPad;
  const float y1 = cmd.y + cmd.height + kPad;
  const float u0 = -kPad / cmd.width;
  const float v0 = -kPad / cmd.height;
  const float u1 = 1.f + kPad / cmd.width;
  const float v1 = 1.f + kPad / cmd.height;
  const Color& c = cmd.color;
  const float kind =
      (cmd.type == DrawCommandType::RoundedRect) ? 1.f : 0.f;
  const float radius = cmd.corner_radius;

  const size_t base = shape_batch_.size();
  shape_batch_.resize(base + 6);
  auto set = [&](size_t i, float x, float y, float u, float v) {
    shape_batch_[base + i] = {x, y, u, v, c.r, c.g, c.b, c.a, radius,
                              cmd.width, cmd.height, kind};
  };
  set(0, x0, y0, u0, v0);
  set(1, x1, y0, u1, v0);
  set(2, x0, y1, u0, v1);
  set(3, x1, y0, u1, v0);
  set(4, x1, y1, u1, v1);
  set(5, x0, y1, u0, v1);
}

void Renderer::AppendGlyphQuad(float x0, float y0, float x1, float y1,
                               float u0, float v0, float u1, float v1,
                               const Color& color) {
  const size_t base = text_batch_.size();
  text_batch_.resize(base + 6);
  text_batch_[base + 0] = {x0, y0, u0, v0, color.r, color.g, color.b, color.a};
  text_batch_[base + 1] = {x1, y0, u1, v0, color.r, color.g, color.b, color.a};
  text_batch_[base + 2] = {x0, y1, u0, v1, color.r, color.g, color.b, color.a};
  text_batch_[base + 3] = {x1, y0, u1, v0, color.r, color.g, color.b, color.a};
  text_batch_[base + 4] = {x1, y1, u1, v1, color.r, color.g, color.b, color.a};
  text_batch_[base + 5] = {x0, y1, u0, v1, color.r, color.g, color.b, color.a};
}

bool Renderer::NextCodepoint(const std::wstring& text, size_t& index,
                             char32_t& out_cp) {
  return NextCodepoint(text.c_str(), text.size(), index, out_cp);
}

bool Renderer::NextCodepoint(const wchar_t* text, size_t length, size_t& index,
                             char32_t& out_cp) {
  if (!text || index >= length) {
    return false;
  }

  out_cp = static_cast<char32_t>(text[index++]);
  if (out_cp >= 0xD800 && out_cp <= 0xDBFF && index < length) {
    const char32_t low = static_cast<char32_t>(text[index]);
    if (low >= 0xDC00 && low <= 0xDFFF) {
      out_cp = 0x10000 + ((out_cp - 0xD800) << 10) + (low - 0xDC00);
      ++index;
    }
  }
  return true;
}

float Renderer::GlyphAdvance(char32_t cp) const {
  if (!active_font_atlas_) {
    return 0.f;
  }
  const GlyphInfo* glyph = active_font_atlas_->find_glyph(cp);
  if (!glyph) {
    glyph = active_font_atlas_->find_glyph(U'?');
  }
  return glyph ? glyph->advance_x : 0.f;
}

const FontAtlas* Renderer::ResolveAtlas(const DrawCommand& cmd) const {
  if (cmd.font_atlas && cmd.font_atlas->is_valid()) {
    return cmd.font_atlas;
  }
  if (default_font_atlas_ && default_font_atlas_->is_valid()) {
    return default_font_atlas_;
  }
  return nullptr;
}

ID3D11ShaderResourceView* Renderer::FontSrv(const FontAtlas* atlas) const {
  if (!atlas) {
    return nullptr;
  }
  const auto it = font_gpus_.find(atlas);
  if (it == font_gpus_.end()) {
    return nullptr;
  }
  return it->second.srv;
}

float Renderer::FontScaleForCommand(const DrawCommand& cmd) const {
  const FontAtlas* atlas = ResolveAtlas(cmd);
  if (!atlas || atlas->size_px() <= 0.f) {
    return 1.f;
  }
  if (cmd.font_size <= 0.f) {
    return 1.f;
  }
  return cmd.font_size / atlas->size_px();
}

void Renderer::EmitTextLine(const std::wstring& line, float pen_x,
                            float baseline, const Color& color, float scale) {
  EmitTextLine(line.c_str(), line.size(), pen_x, baseline, color, scale);
}

void Renderer::EmitTextLine(const wchar_t* text, size_t length, float pen_x,
                            float baseline, const Color& color, float scale) {
  if (!text || length == 0 || !active_font_atlas_) {
    return;
  }
  float cursor_x = pen_x;
  size_t i = 0;
  char32_t cp = 0;

  while (NextCodepoint(text, length, i, cp)) {
    const GlyphInfo* glyph = active_font_atlas_->find_glyph(cp);
    if (!glyph) {
      glyph = active_font_atlas_->find_glyph(U'?');
    }
    if (!glyph) {
      continue;
    }

    if (glyph->width > 0.f && glyph->height > 0.f) {
      const float x0 = cursor_x + glyph->offset_x * scale;
      const float y0 = baseline + glyph->offset_y * scale;
      const float x1 = x0 + glyph->width * scale;
      const float y1 = y0 + glyph->height * scale;
      AppendGlyphQuad(x0, y0, x1, y1, glyph->u0, glyph->v0, glyph->u1,
                      glyph->v1, color);
    }

    cursor_x += glyph->advance_x * scale;
  }
}

void Renderer::EmitTextCommand(ID3D11DeviceContext* context,
                               const DrawCommand& cmd) {
  const FontAtlas* atlas = ResolveAtlas(cmd);
  if (!atlas || cmd.text.empty() || !FontSrv(atlas)) {
    return;
  }

  if (!text_batch_.empty() && text_batch_atlas_ != atlas) {
    FlushTextBatch(context);
  }

  active_font_atlas_ = atlas;
  text_batch_atlas_ = atlas;

  const Color& c = cmd.color;
  const float scale = FontScaleForCommand(cmd);
  const float line_height = atlas->line_height() * scale;
  const float text_height = (atlas->ascent() - atlas->descent()) * scale;
  const float ascent = atlas->ascent() * scale;

  auto baseline_for_block = [&](float block_height) {
    if (cmd.text_align == TextAlign::LeftMiddle ||
        cmd.text_align == TextAlign::Center) {
      return cmd.y + (cmd.height - block_height) * 0.5f + ascent;
    }
    return cmd.y + ascent;
  };

  auto emit_plain_lines = [&](bool honor_width_wrap) {
    std::vector<std::wstring> lines;
    std::wstring line;
    float line_width = 0.f;
    size_t i = 0;
    char32_t cp = 0;

    auto push_line = [&]() {
      lines.push_back(line);
      line.clear();
      line_width = 0.f;
    };

    while (NextCodepoint(cmd.text, i, cp)) {
      if (cp == U'\n') {
        push_line();
        continue;
      }
      const float advance = GlyphAdvance(cp) * scale;
      if (honor_width_wrap && !line.empty() &&
          line_width + advance > cmd.width && cmd.width > 0.f) {
        const bool is_space = (cp == U' ' || cp == U'\t');
        if (is_space) {
          push_line();
          continue;
        }
        push_line();
      }
      if (cp <= 0xFFFF) {
        line.push_back(static_cast<wchar_t>(cp));
      } else {
        const char32_t payload = cp - 0x10000;
        line.push_back(static_cast<wchar_t>(0xD800 + (payload >> 10)));
        line.push_back(static_cast<wchar_t>(0xDC00 + (payload & 0x3FF)));
      }
      line_width += advance;
      if (honor_width_wrap && line.size() == 1 && advance > cmd.width &&
          cmd.width > 0.f) {
        push_line();
      }
    }
    if (!line.empty() || lines.empty()) {
      push_line();
    }

    const float block_h =
        lines.empty()
            ? text_height
            : text_height +
                  line_height * static_cast<float>(lines.size() - 1);
    float baseline = baseline_for_block(block_h);

    for (const std::wstring& ln : lines) {
      float pen_x = cmd.x;
      if (cmd.text_align == TextAlign::Center) {
        const float w = atlas->measure_width(ln) * scale;
        pen_x = cmd.x + (cmd.width - w) * 0.5f;
      }
      EmitTextLine(ln, pen_x, baseline, c, scale);
      baseline += line_height;
    }
  };

  if (!cmd.wrap) {
    if (cmd.text.find(L'\n') == std::wstring::npos) {
      const float text_width = atlas->measure_width(cmd.text) * scale;
      float pen_x = cmd.x;
      if (cmd.text_align == TextAlign::Center) {
        pen_x = cmd.x + (cmd.width - text_width) * 0.5f;
      }
      const float baseline = baseline_for_block(text_height);
      EmitTextLine(cmd.text, pen_x, baseline, c, scale);
      return;
    }
    emit_plain_lines(false);
    return;
  }

  emit_plain_lines(true);
}

void Renderer::GroupCommandsForBatching(std::vector<DrawCommand*>& commands) {
  // Within each clip run AND layer, group by pipeline type so Rect/Text/Shape
  // don't alternate and flush every widget. Flushing on layer change preserves
  // Scene layer order (e.g. Canvas Image under Button Rect).
  std::vector<DrawCommand*> rects;
  std::vector<DrawCommand*> shapes;
  std::vector<DrawCommand*> texts;
  std::vector<DrawCommand*> images;
  std::vector<DrawCommand*> rebuilt;
  rebuilt.reserve(commands.size());
  rects.reserve(commands.size());
  shapes.reserve(32);
  texts.reserve(commands.size());
  images.reserve(8);

  bool have_layer = false;
  int current_layer = 0;

  auto flush_buckets = [&]() {
    // Keep Text runs grouped by atlas pointer so the GPU stays on one SRV.
    std::stable_sort(texts.begin(), texts.end(),
                     [](const DrawCommand* a, const DrawCommand* b) {
                       const FontAtlas* fa = a ? a->font_atlas : nullptr;
                       const FontAtlas* fb = b ? b->font_atlas : nullptr;
                       return fa < fb;
                     });
    rebuilt.insert(rebuilt.end(), rects.begin(), rects.end());
    rebuilt.insert(rebuilt.end(), shapes.begin(), shapes.end());
    rebuilt.insert(rebuilt.end(), texts.begin(), texts.end());
    rebuilt.insert(rebuilt.end(), images.begin(), images.end());
    rects.clear();
    shapes.clear();
    texts.clear();
    images.clear();
  };

  for (DrawCommand* cmd : commands) {
    if (!cmd) {
      continue;
    }
    if (cmd->type == DrawCommandType::PushClip ||
        cmd->type == DrawCommandType::PopClip) {
      flush_buckets();
      have_layer = false;
      rebuilt.push_back(cmd);
      continue;
    }
    if (!have_layer || cmd->layer != current_layer) {
      flush_buckets();
      current_layer = cmd->layer;
      have_layer = true;
    }
    switch (cmd->type) {
      case DrawCommandType::Rect:
        rects.push_back(cmd);
        break;
      case DrawCommandType::Circle:
      case DrawCommandType::RoundedRect:
        shapes.push_back(cmd);
        break;
      case DrawCommandType::Text:
        texts.push_back(cmd);
        break;
      case DrawCommandType::Image:
        images.push_back(cmd);
        break;
      default:
        flush_buckets();
        have_layer = false;
        rebuilt.push_back(cmd);
        break;
    }
  }
  flush_buckets();
  commands.swap(rebuilt);
}

void Renderer::draw(ID3D11DeviceContext* context, int screen_width,
                    int screen_height,
                    const std::vector<DrawCommand*>& commands,
                    bool commands_sorted) {
  if (!initialized_ || !context || screen_width <= 0 || screen_height <= 0 ||
      commands.empty()) {
    return;
  }

  std::vector<DrawCommand*> sorted_storage;
  const std::vector<DrawCommand*>* list = &commands;
  if (!commands_sorted) {
    sorted_storage = commands;
    std::stable_sort(sorted_storage.begin(), sorted_storage.end(),
                     [](const DrawCommand* a, const DrawCommand* b) {
                       const int la = a ? a->layer : 0;
                       const int lb = b ? b->layer : 0;
                       return la < lb;
                     });
    list = &sorted_storage;
  } else {
    sorted_storage = commands;
    list = &sorted_storage;
  }
  GroupCommandsForBatching(sorted_storage);

  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(screen_cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    return;
  }
  float screen[4] = {static_cast<float>(screen_width),
                     static_cast<float>(screen_height), 0.f, 0.f};
  std::memcpy(mapped.pData, screen, sizeof(screen));
  context->Unmap(screen_cb_, 0);

  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetConstantBuffers(0, 1, &screen_cb_);
  if (raster_state_) {
    context->RSSetState(raster_state_);
  }

  const float blend_factor[4] = {0, 0, 0, 0};
  context->OMSetBlendState(blend_state_, blend_factor, 0xffffffff);

  solid_batch_.clear();
  text_batch_.clear();
  shape_batch_.clear();
  if (solid_batch_.capacity() < 256) {
    solid_batch_.reserve(256);
  }
  if (text_batch_.capacity() < 512) {
    text_batch_.reserve(512);
  }
  if (shape_batch_.capacity() < 128) {
    shape_batch_.reserve(128);
  }
  solid_pipeline_bound_ = false;
  text_pipeline_bound_ = false;
  image_pipeline_bound_ = false;
  shape_pipeline_bound_ = false;
  text_batch_atlas_ = nullptr;
  bound_font_srv_ = nullptr;
  active_font_atlas_ = nullptr;

  struct ClipRect {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
  };

  std::vector<ClipRect> clip_stack;
  auto apply_clip = [&](const ClipRect& r) {
    D3D11_RECT rect{r.left, r.top, r.right, r.bottom};
    context->RSSetScissorRects(1, &rect);
  };

  const ClipRect full{0, 0, screen_width, screen_height};
  apply_clip(full);

  for (DrawCommand* cmd_ptr : *list) {
    if (!cmd_ptr) {
      continue;
    }
    const DrawCommand& cmd = *cmd_ptr;

    if (cmd.type == DrawCommandType::PushClip) {
      FlushAllBatches(context);

      ClipRect next;
      next.left = static_cast<LONG>(cmd.x);
      next.top = static_cast<LONG>(cmd.y);
      next.right = static_cast<LONG>(cmd.x + cmd.width);
      next.bottom = static_cast<LONG>(cmd.y + cmd.height);

      const ClipRect& parent = clip_stack.empty() ? full : clip_stack.back();
      next.left = std::max(next.left, parent.left);
      next.top = std::max(next.top, parent.top);
      next.right = std::min(next.right, parent.right);
      next.bottom = std::min(next.bottom, parent.bottom);
      if (next.right < next.left) {
        next.right = next.left;
      }
      if (next.bottom < next.top) {
        next.bottom = next.top;
      }

      clip_stack.push_back(next);
      apply_clip(next);
      continue;
    }

    if (cmd.type == DrawCommandType::PopClip) {
      FlushAllBatches(context);
      if (!clip_stack.empty()) {
        clip_stack.pop_back();
      }
      apply_clip(clip_stack.empty() ? full : clip_stack.back());
      continue;
    }

    if (cmd.type == DrawCommandType::Text) {
      if (!solid_batch_.empty()) {
        FlushSolidBatch(context);
      }
      if (!shape_batch_.empty()) {
        FlushShapeBatch(context);
      }
      EmitTextCommand(context, cmd);
    } else if (cmd.type == DrawCommandType::Rect) {
      if (!text_batch_.empty()) {
        FlushTextBatch(context);
      }
      if (!shape_batch_.empty()) {
        FlushShapeBatch(context);
      }
      AppendSolidRect(cmd);
    } else if (cmd.type == DrawCommandType::Circle ||
               cmd.type == DrawCommandType::RoundedRect) {
      if (!text_batch_.empty()) {
        FlushTextBatch(context);
      }
      if (!solid_batch_.empty()) {
        FlushSolidBatch(context);
      }
      AppendSdfShape(cmd);
    } else if (cmd.type == DrawCommandType::Image) {
      DrawImageCommand(context, cmd);
    }
  }

  FlushAllBatches(context);
  apply_clip(full);
}
