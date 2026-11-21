#pragma once

#include <glad/glad.h> // for gl types

#include "Font.h"

#include <string>

class FontGL
{
public:
  FontGL();
  ~FontGL();

  void load(const Font& font);
  void unload();

  GLuint getTextureID() const { return m_texID; }

private:
  GLuint m_texID;
};
