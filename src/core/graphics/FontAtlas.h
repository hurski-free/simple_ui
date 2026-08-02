#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct GlyphInfo {
  // Atlas UVs in [0..1]
  float u0 = 0.f;
  float v0 = 0.f;
  float u1 = 0.f;
  float v1 = 0.f;

  // Glyph bitmap size in pixels
  float width = 0.f;
  float height = 0.f;

  // Offset from pen position to the glyph bitmap top-left
  float offset_x = 0.f;
  float offset_y = 0.f;

  // Pen advance along X after this glyph
  float advance_x = 0.f;
};

// Bitmap atlas for one TTF/OTF at a fixed pixel size.
// Create separate FontAtlas instances for different sizes to keep glyphs sharp.
class FontAtlas {
public:
  FontAtlas() = default;
  ~FontAtlas() = default;

  FontAtlas(const FontAtlas&) = delete;
  FontAtlas& operator=(const FontAtlas&) = delete;

  // Build atlas from .ttf/.otf. size_px is the font pixel height.
  // Default charset: printable ASCII + basic Cyrillic block.
  bool build(const char* ttf_path, float size_px);

  // Same as build, with an explicit UTF-32 codepoint list.
  bool build(const char* ttf_path, float size_px,
             const char32_t* codepoints, size_t codepoint_count);

  bool is_valid() const;

  float size_px() const;
  int atlas_width() const;
  int atlas_height() const;

  // R8 row-major pixels, size atlas_width * atlas_height
  const std::uint8_t* pixels() const;
  const std::vector<std::uint8_t>& pixel_buffer() const;

  float ascent() const;
  float descent() const;
  float line_gap() const;
  float line_height() const;

  const GlyphInfo* find_glyph(char32_t codepoint) const;

  // Text width in pixels (supports BMP + UTF-16 surrogate pairs).
  float measure_width(const wchar_t* text) const;
  float measure_width(const wchar_t* text, size_t length) const;
  float measure_width(const std::wstring& text) const;

  // Caret/cluster index (UTF-16 code units) closest to the given X offset.
  size_t index_at_x(const wchar_t* text, float x) const;
  size_t index_at_x(const wchar_t* text, size_t length, float x) const;
  size_t index_at_x(const std::wstring& text, float x) const;

private:
  bool BuildInternal(const std::vector<unsigned char>& ttf_data, float size_px,
                     const char32_t* codepoints, size_t codepoint_count);

  float size_px_ = 0.f;
  int atlas_width_ = 0;
  int atlas_height_ = 0;
  float ascent_ = 0.f;
  float descent_ = 0.f;
  float line_gap_ = 0.f;

  std::vector<std::uint8_t> pixels_;
  // Dense storage; bmp_index_[cp] -> glyphs_storage_ index for BMP, else -1.
  std::vector<GlyphInfo> glyphs_storage_;
  std::vector<int> bmp_index_;
  std::unordered_map<char32_t, int> glyphs_extra_;
};
