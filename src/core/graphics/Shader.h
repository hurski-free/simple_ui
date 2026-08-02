#pragma once

#include <d3d11.h>

class Shader {
public:
  Shader() = default;
  ~Shader();

  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;

  // Compile VS + PS from HLSL source strings.
  bool create_shader(ID3D11Device* device, const char* vertex_source,
                     const char* pixel_source, const char* vs_entry = "main",
                     const char* ps_entry = "main");

  // Compile a geometry shader from an HLSL source string.
  bool create_geometry_shader(ID3D11Device* device, const char* geometry_source,
                              const char* entry = "main");

  void bind(ID3D11DeviceContext* context) const;
  bool is_valid() const;

  ID3D11VertexShader* vertex_shader() const;
  ID3D11PixelShader* pixel_shader() const;
  ID3D11GeometryShader* geometry_shader() const;
  ID3DBlob* vs_blob() const;

private:
  static bool Compile(const char* source, const char* entry, const char* target,
                      ID3DBlob** out_blob);
  void ReleaseAll();

  ID3D11VertexShader* vs_ = nullptr;
  ID3D11PixelShader* ps_ = nullptr;
  ID3D11GeometryShader* gs_ = nullptr;
  ID3DBlob* vs_blob_ = nullptr;
};
