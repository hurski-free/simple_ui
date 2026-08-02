#include "FontAtlas.h"

#include <cmath>
#include <cstdio>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../../third_party/stb/stb_truetype.h"

namespace {

std::vector<char32_t> DefaultCodepoints() {
  std::vector<char32_t> cps;
  cps.reserve(200);

  // ASCII printable
  for (char32_t c = 32; c <= 126; ++c) {
    cps.push_back(c);
  }

  // Basic Cyrillic block (includes Yo)
  for (char32_t c = 0x0400; c <= 0x04FF; ++c) {
    cps.push_back(c);
  }

  // Common replacement glyph
  cps.push_back(0xFFFD);
  return cps;
}

bool LoadFile(const char* path, std::vector<unsigned char>& out) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return false;
  }
  const std::streamsize size = file.tellg();
  if (size <= 0) {
    return false;
  }
  out.resize(static_cast<size_t>(size));
  file.seekg(0, std::ios::beg);
  return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

int ChooseAtlasSize(int glyph_count, float size_px) {
  const float approx =
      std::sqrt(static_cast<float>(glyph_count)) * (size_px + 2.f);
  int size = 256;
  while (size < static_cast<int>(approx) + 64 && size < 4096) {
    size *= 2;
  }
  return size;
}

}  // namespace

bool FontAtlas::build(const char* ttf_path, float size_px) {
  const std::vector<char32_t> cps = DefaultCodepoints();
  return build(ttf_path, size_px, cps.data(), cps.size());
}

bool FontAtlas::build(const char* ttf_path, float size_px,
                      const char32_t* codepoints, size_t codepoint_count) {
  if (!ttf_path || size_px <= 0.f || !codepoints || codepoint_count == 0) {
    return false;
  }

  std::vector<unsigned char> ttf_data;
  if (!LoadFile(ttf_path, ttf_data)) {
    return false;
  }

  return BuildInternal(ttf_data, size_px, codepoints, codepoint_count);
}

bool FontAtlas::BuildInternal(const std::vector<unsigned char>& ttf_data,
                              float size_px, const char32_t* codepoints,
                              size_t codepoint_count) {
  stbtt_fontinfo font{};
  if (!stbtt_InitFont(&font, ttf_data.data(),
                      stbtt_GetFontOffsetForIndex(ttf_data.data(), 0))) {
    return false;
  }

  int atlas_size = ChooseAtlasSize(static_cast<int>(codepoint_count), size_px);

  std::vector<stbtt_packedchar> packed(codepoint_count);
  std::vector<int> codepoints_i(codepoint_count);
  for (size_t i = 0; i < codepoint_count; ++i) {
    codepoints_i[i] = static_cast<int>(codepoints[i]);
  }

  std::vector<stbtt_pack_range> ranges(1);
  ranges[0].font_size = size_px;
  ranges[0].first_unicode_codepoint_in_range = 0;
  ranges[0].array_of_unicode_codepoints = codepoints_i.data();
  ranges[0].num_chars = static_cast<int>(codepoint_count);
  ranges[0].chardata_for_range = packed.data();
  ranges[0].h_oversample = 1;
  ranges[0].v_oversample = 1;
  std::vector<std::uint8_t> pixels;
  bool packed_ok = false;

  for (int attempt = 0; attempt < 4 && !packed_ok; ++attempt) {
    pixels.assign(static_cast<size_t>(atlas_size) * atlas_size, 0);

    stbtt_pack_context pack{};
    if (!stbtt_PackBegin(&pack, pixels.data(), atlas_size, atlas_size, 0, 1,
                         nullptr)) {
      return false;
    }
    stbtt_PackSetSkipMissingCodepoints(&pack, 1);
    const int pack_result =
        stbtt_PackFontRanges(&pack, ttf_data.data(), 0, ranges.data(), 1);
    stbtt_PackEnd(&pack);

    if (pack_result) {
      packed_ok = true;
    } else {
      atlas_size *= 2;
      if (atlas_size > 4096) {
        return false;
      }
    }
  }

  if (!packed_ok) {
    return false;
  }

  const float scale = stbtt_ScaleForPixelHeight(&font, size_px);
  int ascent = 0;
  int descent = 0;
  int line_gap = 0;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

  glyphs_storage_.clear();
  glyphs_storage_.reserve(codepoint_count);
  glyphs_extra_.clear();
  bmp_index_.assign(0x10000, -1);

  auto store_glyph = [this](char32_t cp, const GlyphInfo& glyph) {
    const int idx = static_cast<int>(glyphs_storage_.size());
    glyphs_storage_.push_back(glyph);
    if (cp <= 0xFFFF) {
      bmp_index_[static_cast<size_t>(cp)] = idx;
    } else {
      glyphs_extra_[cp] = idx;
    }
  };

  for (size_t i = 0; i < codepoint_count; ++i) {
    const stbtt_packedchar& pc = packed[i];
    // Skip missing / unpacked glyphs
    if (pc.x1 <= pc.x0 && pc.y1 <= pc.y0 && pc.xadvance == 0.f) {
      continue;
    }

    GlyphInfo glyph{};
    glyph.u0 = static_cast<float>(pc.x0) / static_cast<float>(atlas_size);
    glyph.v0 = static_cast<float>(pc.y0) / static_cast<float>(atlas_size);
    glyph.u1 = static_cast<float>(pc.x1) / static_cast<float>(atlas_size);
    glyph.v1 = static_cast<float>(pc.y1) / static_cast<float>(atlas_size);
    glyph.width = static_cast<float>(pc.x1 - pc.x0);
    glyph.height = static_cast<float>(pc.y1 - pc.y0);
    glyph.offset_x = pc.xoff;
    glyph.offset_y = pc.yoff;
    glyph.advance_x = pc.xadvance;
    store_glyph(codepoints[i], glyph);
  }

  // Ensure space glyph exists for advance-only spacing
  if (bmp_index_[static_cast<size_t>(U' ')] < 0) {
    int advance = 0;
    stbtt_GetCodepointHMetrics(&font, ' ', &advance, nullptr);
    GlyphInfo space{};
    space.advance_x = static_cast<float>(advance) * scale;
    store_glyph(U' ', space);
  }

  size_px_ = size_px;
  atlas_width_ = atlas_size;
  atlas_height_ = atlas_size;
  ascent_ = static_cast<float>(ascent) * scale;
  descent_ = static_cast<float>(descent) * scale;
  line_gap_ = static_cast<float>(line_gap) * scale;
  pixels_ = std::move(pixels);
  return !glyphs_storage_.empty();
}

bool FontAtlas::is_valid() const {
  return !pixels_.empty() && atlas_width_ > 0 && atlas_height_ > 0 &&
         size_px_ > 0.f;
}

float FontAtlas::size_px() const {
  return size_px_;
}

int FontAtlas::atlas_width() const {
  return atlas_width_;
}

int FontAtlas::atlas_height() const {
  return atlas_height_;
}

const std::uint8_t* FontAtlas::pixels() const {
  return pixels_.data();
}

const std::vector<std::uint8_t>& FontAtlas::pixel_buffer() const {
  return pixels_;
}

float FontAtlas::ascent() const {
  return ascent_;
}

float FontAtlas::descent() const {
  return descent_;
}

float FontAtlas::line_gap() const {
  return line_gap_;
}

float FontAtlas::line_height() const {
  return ascent_ - descent_ + line_gap_;
}

const GlyphInfo* FontAtlas::find_glyph(char32_t codepoint) const {
  if (codepoint <= 0xFFFF) {
    if (bmp_index_.size() <= static_cast<size_t>(codepoint)) {
      return nullptr;
    }
    const int idx = bmp_index_[static_cast<size_t>(codepoint)];
    if (idx < 0 || static_cast<size_t>(idx) >= glyphs_storage_.size()) {
      return nullptr;
    }
    return &glyphs_storage_[static_cast<size_t>(idx)];
  }
  const auto it = glyphs_extra_.find(codepoint);
  if (it == glyphs_extra_.end()) {
    return nullptr;
  }
  const int idx = it->second;
  if (idx < 0 || static_cast<size_t>(idx) >= glyphs_storage_.size()) {
    return nullptr;
  }
  return &glyphs_storage_[static_cast<size_t>(idx)];
}

float FontAtlas::measure_width(const std::wstring& text) const {
  return measure_width(text.c_str(), text.size());
}

float FontAtlas::measure_width(const wchar_t* text) const {
  if (!text) {
    return 0.f;
  }
  size_t n = 0;
  while (text[n]) {
    ++n;
  }
  return measure_width(text, n);
}

float FontAtlas::measure_width(const wchar_t* text, size_t length) const {
  if (!text || length == 0 || !is_valid()) {
    return 0.f;
  }

  float width = 0.f;
  for (size_t i = 0; i < length;) {
    char32_t cp = static_cast<char32_t>(text[i]);
    size_t units = 1;
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < length) {
      const char32_t low = static_cast<char32_t>(text[i + 1]);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        units = 2;
      }
    }

    const GlyphInfo* glyph = find_glyph(cp);
    if (!glyph) {
      glyph = find_glyph(U'?');
    }
    if (glyph) {
      width += glyph->advance_x;
    }
    i += units;
  }
  return width;
}

size_t FontAtlas::index_at_x(const std::wstring& text, float x) const {
  return index_at_x(text.c_str(), text.size(), x);
}

size_t FontAtlas::index_at_x(const wchar_t* text, float x) const {
  if (!text) {
    return 0;
  }
  size_t n = 0;
  while (text[n]) {
    ++n;
  }
  return index_at_x(text, n, x);
}

size_t FontAtlas::index_at_x(const wchar_t* text, size_t length, float x) const {
  if (!text || length == 0 || !is_valid()) {
    return 0;
  }
  if (x <= 0.f) {
    return 0;
  }

  float width = 0.f;
  size_t index = 0;
  while (index < length) {
    const size_t start = index;
    char32_t cp = static_cast<char32_t>(text[index]);
    size_t units = 1;
    if (cp >= 0xD800 && cp <= 0xDBFF && index + 1 < length) {
      const char32_t low = static_cast<char32_t>(text[index + 1]);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        units = 2;
      }
    }

    const GlyphInfo* glyph = find_glyph(cp);
    if (!glyph) {
      glyph = find_glyph(U'?');
    }
    const float adv = glyph ? glyph->advance_x : 0.f;

    // Snap to nearest edge (before/after this glyph).
    if (x < width + adv * 0.5f) {
      return start;
    }

    width += adv;
    index += units;
  }
  return index;
}
