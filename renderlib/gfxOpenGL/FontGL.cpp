#include "FontGL.h"

#include "Util.h"

#include <stdio.h>

GLuint
generateFontTexture(const Font& font)
{
  size_t w = font.getTextureWidth();
  size_t h = font.getTextureHeight();
  const unsigned char* temp_bitmap = font.getTextureData();

  // expand temp_bitmap to RGBA
  unsigned char* expanded_bitmap = new unsigned char[w * h * 4];
  for (size_t i = 0; i < w * h; ++i) {
    expanded_bitmap[i * 4 + 0] = 255;
    expanded_bitmap[i * 4 + 1] = 255;
    expanded_bitmap[i * 4 + 2] = 255;
    expanded_bitmap[i * 4 + 3] = temp_bitmap[i];
  }

  GLuint ftex = 0;
  glGenTextures(1, &ftex);
  glBindTexture(GL_TEXTURE_2D, ftex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, expanded_bitmap);
  check_gl("load font texture");
  // can free temp_bitmap at this point
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  check_gl("set filtering on font texture");
  delete[] expanded_bitmap;
  return ftex;
}

FontGL::FontGL()
{
  m_texID = 0;
}

FontGL::~FontGL()
{
  unload();
}

void
FontGL::load(const Font& font)
{
  if (m_texID != 0) {
    return;
  }
  m_texID = generateFontTexture(font);
}

void
FontGL::unload()
{
  glDeleteTextures(1, &m_texID);
  m_texID = 0;
}
