#include "Font.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#include <stdio.h>

static const int s_textureSize = 512;

AgaveFont::AgaveFont() {}

AgaveFont::~AgaveFont()
{
  unload();
}

void
AgaveFont::load(const char* filename)
{
  FILE* file = fopen(filename, "rb");
  if (!file) {
    return;
  }

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  unsigned char* buffer = (unsigned char*)malloc(size);
  fread(buffer, 1, size, file);
  fclose(file);

  // this is the texture data
  unsigned char temp_bitmap[s_textureSize * s_textureSize];
  stbtt_BakeFontBitmap(buffer,
                       0,
                       32.0,
                       temp_bitmap,
                       s_textureSize,
                       s_textureSize,
                       m_firstChar,
                       m_numChars,
                       m_cdata); // no guarantee this fits!
  free(buffer);

  m_w = s_textureSize;
  m_h = s_textureSize;
  m_textureData.resize(m_w * m_h);
  for (int i = 0; i < m_w * m_h; ++i) {
    m_textureData[i] = temp_bitmap[i];
  }
}

void
AgaveFont::unload()
{
  m_textureData.clear();
  m_w = 0;
  m_h = 0;
}

bool
AgaveFont::getBakedQuad(char char_index, float* x, float* y, stbtt_aligned_quad* q)
{
  if (char_index >= m_firstChar && char_index < (m_firstChar + m_numChars)) {
    stbtt_GetBakedQuad(
      m_cdata, s_textureSize, s_textureSize, char_index - m_firstChar, x, y, q, 1); // 1=opengl & d3d10+,0=d3d9
    // because our coordinate system has 0,0 at bottom left, we need to fixup these y coordinates
    size_t offset = char_index - m_firstChar;
    const stbtt_bakedchar* b = m_cdata + offset;

    float height = q->y1 - q->y0;
    float h0 = q->y0;
    // this is our base line?
    float h1 = q->y1;
    q->y0 = h1 - 2.0 * b->yoff - height;
    q->y1 = h0 - 2.0 * b->yoff - height;
    return true;
  }
  return false;
}

float
AgaveFont::getStringWidth(std::string stext)
{
  const char* text = stext.c_str();
  float xpos = 0;
  float ypos = 0;
  float width = 0;
  while (*text) {
    if (*text >= m_firstChar && *text < (m_firstChar + m_numChars)) {
      // if this ever proves inaccurate, consider using stbtt_GetBakedQuad for height/width info
      size_t offset = *text - m_firstChar;
      const stbtt_bakedchar* b = m_cdata + offset;
      width += b->xadvance;
    }
    ++text;
  }
  return width;
}

float
AgaveFont::getStringHeight(std::string stext)
{
  const char* text = stext.c_str();
  float xpos = 0;
  float ypos = 0;
  float height = 0;
  while (*text) {
    if (*text >= m_firstChar && *text < (m_firstChar + m_numChars)) {
      // if this ever proves inaccurate, consider using stbtt_GetBakedQuad for height/width info
      size_t offset = *text - m_firstChar;
      const stbtt_bakedchar* b = m_cdata + offset;
      height = std::max(height, std::abs((float)b->y1 - (float)b->y0));
    }
    ++text;
  }
  return height;
}
