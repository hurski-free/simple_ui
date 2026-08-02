#include "Shader.h"

#include <cstring>
#include <d3dcompiler.h>

Shader::~Shader() {
  ReleaseAll();
}

bool Shader::Compile(const char* source, const char* entry, const char* target,
                     ID3DBlob** out_blob) {
  if (!source || !entry || !target || !out_blob) {
    return false;
  }

  ID3DBlob* errors = nullptr;
  const HRESULT hr = D3DCompile(
      source, strlen(source), nullptr, nullptr, nullptr, entry, target,
      D3DCOMPILE_ENABLE_STRICTNESS, 0, out_blob, &errors);

  if (FAILED(hr)) {
    if (errors) {
      errors->Release();
    }
    return false;
  }

  if (errors) {
    errors->Release();
  }
  return true;
}

bool Shader::create_shader(ID3D11Device* device, const char* vertex_source,
                           const char* pixel_source, const char* vs_entry,
                           const char* ps_entry) {
  if (!device || !vertex_source || !pixel_source) {
    return false;
  }

  if (vs_) {
    vs_->Release();
    vs_ = nullptr;
  }
  if (ps_) {
    ps_->Release();
    ps_ = nullptr;
  }
  if (vs_blob_) {
    vs_blob_->Release();
    vs_blob_ = nullptr;
  }

  ID3DBlob* vs_blob = nullptr;
  ID3DBlob* ps_blob = nullptr;

  if (!Compile(vertex_source, vs_entry, "vs_5_0", &vs_blob)) {
    return false;
  }
  if (!Compile(pixel_source, ps_entry, "ps_5_0", &ps_blob)) {
    vs_blob->Release();
    return false;
  }

  HRESULT hr = device->CreateVertexShader(vs_blob->GetBufferPointer(),
                                          vs_blob->GetBufferSize(), nullptr,
                                          &vs_);
  if (FAILED(hr)) {
    vs_blob->Release();
    ps_blob->Release();
    return false;
  }

  hr = device->CreatePixelShader(ps_blob->GetBufferPointer(),
                                 ps_blob->GetBufferSize(), nullptr, &ps_);
  ps_blob->Release();
  if (FAILED(hr)) {
    vs_->Release();
    vs_ = nullptr;
    vs_blob->Release();
    return false;
  }

  vs_blob_ = vs_blob;
  return true;
}

bool Shader::create_geometry_shader(ID3D11Device* device,
                                    const char* geometry_source,
                                    const char* entry) {
  if (!device || !geometry_source) {
    return false;
  }

  if (gs_) {
    gs_->Release();
    gs_ = nullptr;
  }

  ID3DBlob* gs_blob = nullptr;
  if (!Compile(geometry_source, entry, "gs_5_0", &gs_blob)) {
    return false;
  }

  const HRESULT hr = device->CreateGeometryShader(
      gs_blob->GetBufferPointer(), gs_blob->GetBufferSize(), nullptr, &gs_);
  gs_blob->Release();
  return SUCCEEDED(hr);
}

void Shader::bind(ID3D11DeviceContext* context) const {
  if (!context) {
    return;
  }
  context->VSSetShader(vs_, nullptr, 0);
  context->PSSetShader(ps_, nullptr, 0);
  context->GSSetShader(gs_, nullptr, 0);
}

bool Shader::is_valid() const {
  return vs_ != nullptr && ps_ != nullptr;
}

ID3D11VertexShader* Shader::vertex_shader() const {
  return vs_;
}

ID3D11PixelShader* Shader::pixel_shader() const {
  return ps_;
}

ID3D11GeometryShader* Shader::geometry_shader() const {
  return gs_;
}

ID3DBlob* Shader::vs_blob() const {
  return vs_blob_;
}

void Shader::ReleaseAll() {
  if (vs_) {
    vs_->Release();
    vs_ = nullptr;
  }
  if (ps_) {
    ps_->Release();
    ps_ = nullptr;
  }
  if (gs_) {
    gs_->Release();
    gs_ = nullptr;
  }
  if (vs_blob_) {
    vs_blob_->Release();
    vs_blob_ = nullptr;
  }
}
