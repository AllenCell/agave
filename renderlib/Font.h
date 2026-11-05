#pragma once

#include "stb/stb_truetype.h"

#include <cstdint>
#include <string>
#include <vector>

class AgaveFont
{
public:
  AgaveFont();
  ~AgaveFont();

  bool isLoaded() { return m_w > 0 && m_h > 0 && !m_textureData.empty(); }
  void load(const char* filename);
  void unload();

  // x and y will be updated to the next position to draw the next character.
  bool getBakedQuad(char char_index, float* x, float* y, stbtt_aligned_quad* q);

  float getStringWidth(std::string text);
  float getStringHeight(std::string text);

  uint32_t getTextureWidth() const { return m_w; }
  uint32_t getTextureHeight() const { return m_h; }
  // BE CAREFUL THIS IS ONLY FOR SHORT LIFETIME ACCESSES (e.g. for OpenGL texture creation/copy to gpu)
  const unsigned char* getTextureData() const { return m_textureData.data(); }

private:
  static constexpr char m_firstChar = 32;
  static constexpr size_t m_numChars = 96;
  stbtt_bakedchar m_cdata[m_numChars]; // ASCII 32..126 is 95 glyphs

  uint32_t m_w = 0;
  uint32_t m_h = 0;
  std::vector<unsigned char> m_textureData;
};
