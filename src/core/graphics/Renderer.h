#pragma once

#include "FontAtlas.h"
#include "Shader.h"

#include <d3d11.h>
#include <unordered_map>
#include <vector>

#include "../../simple_ui.h"

class Renderer {
public:
  Renderer() = default;
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  bool init(ID3D11Device* device);
  // Upload atlas pixels to GPU (idempotent if already registered).
  bool register_font_atlas(const FontAtlas& atlas);
  void unregister_font_atlas(const FontAtlas* atlas);
  void set_default_font_atlas(const FontAtlas* atlas);
  bool is_valid() const;

  int load_texture_file(const wchar_t* path);
  int create_texture_rgba(int width, int height, const unsigned char* rgba);
  void unload_texture(int texture_id);
  bool get_texture_size(int texture_id, int* out_w, int* out_h) const;

  void draw(ID3D11DeviceContext* context, int screen_width, int screen_height,
            const std::vector<DrawCommand*>& commands,
            bool commands_sorted = false);

private:
  struct SolidVertex {
    float x, y;
    float r, g, b, a;
  };

  struct TextVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
  };

  struct ShapeVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
    float corner_radius;
    float width;
    float height;
    float kind;
  };

  struct TextureEntry {
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int width = 0;
    int height = 0;
  };

  struct FontGpu {
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
  };

  bool CreateResources();
  void ReleaseResources();
  void ReleaseFontTextures();
  void ReleaseUserTextures();

  bool EnsureSolidCapacity(UINT vertex_count);
  bool EnsureTextCapacity(UINT vertex_count);
  bool EnsureShapeCapacity(UINT vertex_count);
  void FlushSolidBatch(ID3D11DeviceContext* context);
  void FlushTextBatch(ID3D11DeviceContext* context);
  void FlushShapeBatch(ID3D11DeviceContext* context);
  void FlushAllBatches(ID3D11DeviceContext* context);

  void AppendSolidRect(const DrawCommand& cmd);
  void AppendSdfShape(const DrawCommand& cmd);
  void DrawImageCommand(ID3D11DeviceContext* context, const DrawCommand& cmd);
  void AppendGlyphQuad(float x0, float y0, float x1, float y1, float u0,
                       float v0, float u1, float v1, const Color& color);
  void EmitTextLine(const wchar_t* text, size_t length, float pen_x,
                    float baseline, const Color& color, float scale);
  void EmitTextLine(const std::wstring& line, float pen_x, float baseline,
                    const Color& color, float scale);
  void EmitTextCommand(ID3D11DeviceContext* context, const DrawCommand& cmd);

  void BindSolidPipeline(ID3D11DeviceContext* context);
  void BindTextPipeline(ID3D11DeviceContext* context,
                        ID3D11ShaderResourceView* font_srv);
  void BindShapePipeline(ID3D11DeviceContext* context);
  void BindImagePipeline(ID3D11DeviceContext* context,
                         ID3D11ShaderResourceView* srv);
  const FontAtlas* ResolveAtlas(const DrawCommand& cmd) const;
  ID3D11ShaderResourceView* FontSrv(const FontAtlas* atlas) const;
  float GlyphAdvance(char32_t cp) const;
  float FontScaleForCommand(const DrawCommand& cmd) const;
  static bool NextCodepoint(const std::wstring& text, size_t& index,
                            char32_t& out_cp);
  static bool NextCodepoint(const wchar_t* text, size_t length, size_t& index,
                            char32_t& out_cp);
  static void GroupCommandsForBatching(std::vector<DrawCommand*>& commands);
  int AllocTextureId();
  bool UploadRgbaTexture(int width, int height, const unsigned char* rgba,
                         TextureEntry& out);

  ID3D11Device* device_ = nullptr;
  Shader solid_shader_;
  Shader text_shader_;
  Shader image_shader_;
  Shader shape_shader_;
  ID3D11InputLayout* solid_layout_ = nullptr;
  ID3D11InputLayout* text_layout_ = nullptr;
  ID3D11InputLayout* image_layout_ = nullptr;
  ID3D11InputLayout* shape_layout_ = nullptr;
  ID3D11Buffer* solid_vb_ = nullptr;
  ID3D11Buffer* text_vb_ = nullptr;
  ID3D11Buffer* image_vb_ = nullptr;
  ID3D11Buffer* shape_vb_ = nullptr;
  UINT solid_vb_capacity_ = 0;
  UINT text_vb_capacity_ = 0;
  UINT shape_vb_capacity_ = 0;
  ID3D11Buffer* screen_cb_ = nullptr;
  ID3D11BlendState* blend_state_ = nullptr;
  ID3D11RasterizerState* raster_state_ = nullptr;
  ID3D11SamplerState* font_sampler_ = nullptr;
  const FontAtlas* default_font_atlas_ = nullptr;
  const FontAtlas* active_font_atlas_ = nullptr;
  const FontAtlas* text_batch_atlas_ = nullptr;
  bool initialized_ = false;

  std::unordered_map<const FontAtlas*, FontGpu> font_gpus_;

  std::vector<SolidVertex> solid_batch_;
  std::vector<TextVertex> text_batch_;
  std::vector<ShapeVertex> shape_batch_;
  bool solid_pipeline_bound_ = false;
  bool text_pipeline_bound_ = false;
  bool image_pipeline_bound_ = false;
  bool shape_pipeline_bound_ = false;
  ID3D11ShaderResourceView* bound_font_srv_ = nullptr;

  std::unordered_map<int, TextureEntry> textures_;
  int next_texture_id_ = 1;
};
