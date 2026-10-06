#include "GestureGraphicsGL.h"

#include "gfxOpenGL/FontGL.h"
#include "gfxOpenGL/Util.h"
#include "gfxOpenGL/glsl/GLGuiShader.h"

// a vertex buffer that is automatically allocated and then deleted when it goes out of scope
ScopedGlVertexBuffer::ScopedGlVertexBuffer()
  : m_vertexArray(0)
  , m_buffer(0)
  , m_size(0)
{
}
void
ScopedGlVertexBuffer::create()
{
  glGenVertexArrays(1, &m_vertexArray);
  glBindVertexArray(m_vertexArray);

  glGenBuffers(1, &m_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, m_buffer);

  const size_t vtxStride = 9 * sizeof(GLfloat) + 1 * sizeof(GLuint);

  // xyz uv rgba s

  // specify position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vtxStride, (GLvoid*)nullptr);
  glEnableVertexAttribArray(0); // m_loc_vpos

  // specify uv attribute
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vtxStride, (GLvoid*)(3 * sizeof(GLfloat)));
  glEnableVertexAttribArray(1); // m_loc_vuv

  // specify color rgba attribute
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, vtxStride, (GLvoid*)(5 * sizeof(GLfloat)));
  glEnableVertexAttribArray(2); // m_loc_vcolor

  // specify selection id attribute
  glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, vtxStride, (GLvoid*)(9 * sizeof(GLfloat)));
  glEnableVertexAttribArray(3); // m_loc_vcode

  check_gl("create scoped gl vertex buffer");
}
void
ScopedGlVertexBuffer::updateDataAndBind(const void* data, size_t size)
{
  if (size > m_size) {
    m_size = size;
    glBindVertexArray(m_vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    check_gl("resized scoped gl vertex buffer data");
  } else {
    // no need to re-upload the data
    glBindVertexArray(m_vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
    check_gl("updated scoped gl vertex buffer data");
  }
}
ScopedGlVertexBuffer::~ScopedGlVertexBuffer()
{
  glBindVertexArray(0);
  glDeleteVertexArrays(1, &m_vertexArray);
  glDeleteBuffers(1, &m_buffer);
}

// a texture buffer that is automatically allocated and then deleted when it goes out of scope
ScopedGlTextureBuffer::ScopedGlTextureBuffer()
  : m_texture(0)
  , m_buffer(0)
  , m_size(0)
{
}
void
ScopedGlTextureBuffer::create()
{
  glGenBuffers(1, &m_buffer);
  glBindBuffer(GL_TEXTURE_BUFFER, m_buffer);

  glGenTextures(1, &m_texture);
  glBindTexture(GL_TEXTURE_BUFFER, m_texture);
  glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, m_buffer);
  check_gl("create scoped gl texture buffer");
}
void
ScopedGlTextureBuffer::updateDataAndBind(const void* data, size_t size)
{
  if (size > m_size) {
    m_size = size;
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    check_gl("resized scoped gl texture buffer data");
  } else {
    // no need to re-upload the data
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
    check_gl("updated scoped gl texture buffer data");
  }
}

ScopedGlTextureBuffer::~ScopedGlTextureBuffer()
{
  glDeleteTextures(1, &m_texture);
  glDeleteBuffers(1, &m_buffer);
}

namespace Pipeline {

// First I may draw any GUI geometry that I want to be depth-composited with the
// rest of the scene in viewport. This may be any supporting guide that needs to
// appear for the duration of some action and reveal intersections against the
// scene geometry.

// Draw something "in the scene". This has a limitation that we assume there is a
// single viewport.
static void
configure_3dDepthTested(SceneView& sceneView, Gesture::Graphics& graphics, IGuiShader* shader)
{
  glm::mat4 v(1.0);
  sceneView.camera.getViewMatrix(v);
  glm::mat4 p(1.0);
  sceneView.camera.getProjMatrix(p);

  shader->setProjMatrix(p * v);

  check_gl("set proj matrix");

  glEnable(GL_DEPTH_TEST);
  check_gl("enable depth test");
}

// The second pass is still about 3d geometry, only this time I want it to be
// drawn on top, without depth test. These two passes shares in common the same
// projection matrix as the rest of the scene. 3d manipulators shown earlier are examples.

// Overlay something "in the scene". This has a limitation that we assume there
// is a single viewport.
static void
configure_3dStacked(SceneView& sceneView, Gesture::Graphics& graphics, IGuiShader* shader)
{
  glm::mat4 v(1.0);
  sceneView.camera.getViewMatrix(v);
  glm::mat4 p(1.0);
  sceneView.camera.getProjMatrix(p);
  check_gl("PRE set proj matrix");

  shader->setProjMatrix(p * v);

  check_gl("set proj matrix");

  glDisable(GL_DEPTH_TEST);
  check_gl("disable depth test");
}

// The third pass is a 2d orthographic projection of screen space, where the
// coordinates are measured in pixels starting at the lower left corner of the
// screen. Here is where I draw buttons or other traditional GUI elements if you wish.

// Draw something in screen space without zbuffer.
static void
configure_2dScreen(SceneView& sceneView, Gesture::Graphics& graphics, IGuiShader* shader)
{
  auto p = glm::ortho((float)sceneView.viewport.region.lower.x,
                      (float)sceneView.viewport.region.upper.x,
                      (float)sceneView.viewport.region.lower.y,
                      (float)sceneView.viewport.region.upper.y,
                      1.0f,
                      -1.f);
  shader->setProjMatrix(p);
  check_gl("set proj matrix");

  glDisable(GL_DEPTH_TEST);
  check_gl("disable depth test");
}
} // namespace Pipeline

template<typename DrawBlock>
void
drawGestureCodes(const SelectionBuffer& selection, const SceneView::Viewport& viewport, DrawBlock drawSceneGeometry)
{
  // Backup
  GLenum last_framebuffer;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, (GLint*)&last_framebuffer);
  check_gl("get draw framebuffer");
  GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
  check_gl("is depth enabled");
  GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
  check_gl("is blend enabled");
  GLfloat last_clear_color[4];
  glGetFloatv(GL_COLOR_CLEAR_VALUE, last_clear_color);

  // Render to texture
  glBindFramebuffer(GL_FRAMEBUFFER, selection.frameBuffer);
  check_gl("bind selection framebuffer");
  {
    glViewport(viewport.region.lower.x, viewport.region.lower.y, viewport.region.upper.x, viewport.region.upper.y);
    glDisable(GL_BLEND);
    uint32_t clearcode = Gesture::Graphics::k_noSelectionCode;
    glClearColor(((clearcode >> 0) & 0xFF) / 255.0,
                 ((clearcode >> 8) & 0xFF) / 255.0,
                 ((clearcode >> 16) & 0xFF) / 255.0,
                 ((clearcode >> 24) & 0xFF) / 255.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    drawSceneGeometry();
  }

  // Restore
  glBindFramebuffer(GL_FRAMEBUFFER, last_framebuffer);
  check_gl("restore default framebuffer");
  if (last_enable_depth_test) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  check_gl("restore depth test state");
  if (last_enable_blend) {
    glEnable(GL_BLEND);
  } else {
    glDisable(GL_BLEND);
  }
  check_gl("restore blend enabled state");
  glClearColor(last_clear_color[0], last_clear_color[1], last_clear_color[2], last_clear_color[3]);
  check_gl("restore clear color");
}

void
RenderBuffer::destroy()
{
  if (frameBuffer == 0) {
    return;
  }
  glDeleteFramebuffers(1, &frameBuffer);
  glDeleteRenderbuffers(1, &depthRenderBuffer);
  glDeleteTextures(1, &renderedTexture);
  glDeleteTextures(1, &depthTexture);
  frameBuffer = 0;
  depthRenderBuffer = 0;
  renderedTexture = 0;
  resolution = glm::ivec2(0, 0);
}

bool
RenderBuffer::create(glm::ivec2 resolution, int samples)
{
  this->resolution = resolution;
  this->samples = samples;

  GLint last_framebuffer;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_framebuffer);

  glGenFramebuffers(1, &frameBuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

  if (samples == 0) {
    glGenTextures(1, &renderedTexture);
    // glCreateTextures(GL_TEXTURE_2D, 1, texturePtr);

    // "Bind" the newly created texture: all future texture functions will modify this texture
    glBindTexture(GL_TEXTURE_2D, renderedTexture);

    // Define the texture quality and zeroes its memory
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, resolution.x, resolution.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // We don't need texture filtering, but we need to specify some.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    // Set "renderedTexture" as our colour attachement #0
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, renderedTexture, 0);

    // The depth buffer
    glGenRenderbuffers(1, &depthRenderBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, resolution.x, resolution.y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderBuffer);
  } else {
    glGenTextures(1, &renderedTexture);
    glGenTextures(1, &depthTexture);
    // glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &renderedTexture);
    // glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &depthTexture);

    // "Bind" the newly created texture : all future texture functions will modify this texture
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, renderedTexture);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA, resolution.x, resolution.y, GL_TRUE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, renderedTexture, 0);

    // The depth buffer
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, depthTexture);
    glTexImage2DMultisample(
      GL_TEXTURE_2D_MULTISAMPLE, samples, GL_DEPTH32F_STENCIL8, resolution.x, resolution.y, GL_TRUE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, depthTexture, 0);
  }

  // Always check that our framebuffer is ok
  bool status = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
  check_glfb("renderbuffer for picking");

  glBindFramebuffer(GL_FRAMEBUFFER, last_framebuffer);
  return status;
}

void
SelectionBuffer::clear()
{
  // Backup
  GLenum last_framebuffer;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, (GLint*)&last_framebuffer);
  GLfloat last_clear_color[4];
  glGetFloatv(GL_COLOR_CLEAR_VALUE, last_clear_color);

  // Render to texture
  glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
  {
    glViewport(0, 0, resolution.x, resolution.y);
    uint32_t clearcode = Gesture::Graphics::k_noSelectionCode;
    glClearColor(((clearcode >> 0) & 0xFF) / 255.0,
                 ((clearcode >> 8) & 0xFF) / 255.0,
                 ((clearcode >> 16) & 0xFF) / 255.0,
                 ((clearcode >> 24) & 0xFF) / 255.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  // Restore
  glBindFramebuffer(GL_FRAMEBUFFER, last_framebuffer);
  glClearColor(last_clear_color[0], last_clear_color[1], last_clear_color[2], last_clear_color[3]);
}

// YAGNI: With a small effort we could create dynamic passes that are
//        fully user configurable...
//
// Configure command lists.
static std::array<void (*)(SceneView&, Gesture::Graphics&, IGuiShader* shader), Gesture::Graphics::kNumCommandsLists>
  pipelineConfig = {
    Pipeline::configure_3dDepthTested,
    Pipeline::configure_3dStacked,
    Pipeline::configure_2dScreen,
    Pipeline::configure_3dStacked // underlay uses same config as stacked
  };

// the display arg is for draw vs pick
void
GestureRendererGL::drawGesture(bool display,
                               Gesture::Graphics& graphics,
                               SceneView& sceneView,
                               std::vector<int> sequenceOrder)
{
  if (sequenceOrder.empty()) {
    return;
  }
  shader->configure(display, this->glTextureId);

  for (int sequence : sequenceOrder) {
    if (!graphics.commands[sequence].empty()) {
      pipelineConfig[sequence](sceneView, graphics, shader.get());

      // YAGNI: Commands could be coalesced, setting state could be avoided
      //        if not changing... For now it seems we can draw at over 2000 Hz
      //        and no further optimization is required.
      for (Gesture::Graphics::CommandRange cmdr : graphics.commands[sequence]) {
        Gesture::Graphics::Command& cmd = cmdr.command;
        if (cmdr.end == -1)
          cmdr.end = graphics.verts.size();
        if (cmdr.begin >= cmdr.end)
          continue;

        if (cmd.command == Gesture::Graphics::PrimitiveType::kLines) {
          glLineWidth(cmd.thickness);
          check_gl("linewidth");
        }
        if (cmd.command == Gesture::Graphics::PrimitiveType::kPoints) {
          glPointSize(cmd.thickness);
          check_gl("pointsize");
        }
        if (cmd.doubleSided) {
          glDisable(GL_CULL_FACE);
        }
        GLenum mode = GL_TRIANGLES;
        switch (cmd.command) {
          case Gesture::Graphics::PrimitiveType::kLines:
            mode = GL_LINES;
            break;
          case Gesture::Graphics::PrimitiveType::kPoints:
            mode = GL_POINTS;
            break;
          case Gesture::Graphics::PrimitiveType::kTriangles:
            mode = GL_TRIANGLES;
            break;
          default:
            assert(false && "unsupported primitive type");
        }
        glDrawArrays(mode, cmdr.begin, cmdr.end - cmdr.begin);
        check_gl("drawarrays");
        if (cmd.doubleSided) {
          glEnable(GL_CULL_FACE);
        }
      }
    }
  }

  shader->cleanup();

  if (!graphics.stripRanges.empty()) {
    shaderLines->configure(display, this->glTextureId);
    GLint currentVertexArray;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &currentVertexArray);
    glBindVertexArray(thickLinesVertexArray);
    check_gl("bind vertex array for thicklines");
    for (int sequence : sequenceOrder) {
      pipelineConfig[sequence](sceneView, graphics, shaderLines.get());

      // now let's draw some strips, using stripRanges
      for (size_t i = 0; i < graphics.stripRanges.size(); ++i) {
        if ((int)graphics.stripProjections[i] != sequence) {
          continue;
        }

        const glm::ivec2& range = graphics.stripRanges[i];
        const float thickness = graphics.stripThicknesses[i];

        // we are drawing N-1 line segments, but the number of elements in the array is N+2
        // see GLThickLines for comments explaining the data layout and draw strategy
        GLsizei N = (GLsizei)(range.y - range.x) - 2;
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_BUFFER, texture_buffer->texture());
        glUniform1i(shaderLines->m_loc_stripVerts, 2);
        glUniform1i(shaderLines->m_loc_stripVertexOffset, range.x);
        glUniform1f(shaderLines->m_loc_thickness, thickness);
        glUniform2fv(shaderLines->m_loc_resolution, 1, glm::value_ptr(glm::vec2(sceneView.viewport.region.size())));
        check_gl("set strip uniforms");
        glDrawArrays(GL_TRIANGLES, 0, 6 * (N - 1));
        check_gl("thicklines drawarrays");
      }
    }
    shaderLines->cleanup();
    glBindVertexArray(currentVertexArray);
  }
  check_gl("disablevertexattribarray");
};

void
GestureRendererGL::draw(SceneView& sceneView, SelectionBuffer* selection, Gesture::Graphics& graphics)
{
  // Gesture draw spans across the entire window and it is not restricted to a single
  // viewport.
  if (graphics.verts.empty() && graphics.stripVerts.empty()) {
    graphics.clearCommands();

    // TODO: do this clear only once if verts empty on consecutive frames?
    // it would save some computation but this is really not a bottleneck here.
    if (selection) {
      selection->clear();
    }
    return;
  }

  // lazy init
  lazyInit(graphics);

  // Backup state
  float lineWidth;
  glGetFloatv(GL_LINE_WIDTH, &lineWidth);
  check_gl("get line width");
  float pointSize;
  glGetFloatv(GL_POINT_SIZE, &pointSize);
  check_gl("get point size");
  bool depthTest = glIsEnabled(GL_DEPTH_TEST);
  check_gl("is depth test enabled");

  glEnable(GL_CULL_FACE);

  // Draw UI and viewport manipulators
  {
    vertex_buffer->updateDataAndBind(graphics.verts.data(),
                                     graphics.verts.size() * sizeof(Gesture::Graphics::VertsCode));

    // buffer containing all the strip vertices
    texture_buffer->updateDataAndBind(graphics.stripVerts.data(),
                                      graphics.stripVerts.size() * sizeof(Gesture::Graphics::VertsCode));

    // Prepare a lambda to draw the Gesture commands. We'll run the draw function twice, once to
    // draw the GUI and once to draw the selection buffer data.
    // (display param is for draw vs pick)
    std::vector<int> sequenceOrder = {
      // Step 1: we draw any command that is depth-composited with the scene
      (int)Gesture::Graphics::CommandSequence::k3dDepthTested,
      // Step 2: we draw any command that is not depth composited but is otherwise using
      //         the same perspective projection
      (int)Gesture::Graphics::CommandSequence::k3dStacked,
      // Step 3: we draw anything that is just an overlay in screen space. Most of the UI
      //         elements go here.
      (int)Gesture::Graphics::CommandSequence::k2dScreen,
    };

    drawGesture(/*display*/ true, graphics, sceneView, sequenceOrder);

    // The last thing we draw is selection codes for next frame. This allows us
    // to know what is under the pointer cursor.
    if (selection) {
      drawGestureCodes(
        *selection, sceneView.viewport, [&]() { drawGesture(/*display*/ false, graphics, sceneView, sequenceOrder); });
    }

    glBindVertexArray(0);
  }

  // Restore state
  glLineWidth(lineWidth);
  check_gl("linewidth");
  glPointSize(pointSize);
  check_gl("pointsize");
  if (depthTest) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  check_gl("toggle depth test");

  graphics.clearCommands();
}

void
GestureRendererGL::lazyInit(Gesture::Graphics& graphics)
{
  if (!shader.get()) {
    shader.reset(new GLGuiShader());
  }
  if (!shaderLines.get()) {
    shaderLines.reset(new GLThickLinesShader());
  }
  if (!font.get()) {
    font.reset(new FontGL());
    font->load(graphics.font);

    // Currently gesture.graphics only supports one global texture for all draw commands.
    // This is safe for now because the font texture is the only one needed.
    // In future, if e.g. tool buttons need texture images, then we have to
    // attach the texture id with the draw command.
    glTextureId = font->getTextureID();
  }
  if (!vertex_buffer.get()) {
    vertex_buffer.reset(new ScopedGlVertexBuffer());
    vertex_buffer->create();
  }
  if (!texture_buffer.get()) {
    texture_buffer.reset(new ScopedGlTextureBuffer());
    texture_buffer->create();
  }
  if (thickLinesVertexArray == 0) {
    glGenVertexArrays(1, &thickLinesVertexArray);
  }
}

void
GestureRendererGL::drawUnderlay(SceneView& sceneView, SelectionBuffer* selection, Gesture::Graphics& graphics)
{
  // Gesture draw spans across the entire window and it is not restricted to a single
  // viewport.

  if (graphics.verts.empty() && graphics.stripVerts.empty()) {
    return;
  }

  // lazy init
  lazyInit(graphics);

  // Backup state
  float lineWidth;
  glGetFloatv(GL_LINE_WIDTH, &lineWidth);
  check_gl("get line width");
  float pointSize;
  glGetFloatv(GL_POINT_SIZE, &pointSize);
  check_gl("get point size");
  bool depthTest = glIsEnabled(GL_DEPTH_TEST);
  check_gl("is depth test enabled");

  glEnable(GL_CULL_FACE);

  // Draw UI and viewport manipulators
  {
    vertex_buffer->updateDataAndBind(graphics.verts.data(),
                                     graphics.verts.size() * sizeof(Gesture::Graphics::VertsCode));

    // buffer containing all the strip vertices
    texture_buffer->updateDataAndBind(graphics.stripVerts.data(),
                                      graphics.stripVerts.size() * sizeof(Gesture::Graphics::VertsCode));

    // Prepare a lambda to draw the Gesture commands. We'll run the lambda twice, once to
    // draw the GUI and once to draw the selection buffer data.
    // (display var is for draw vs pick)
    std::vector<int> sequenceOrder = { (int)Gesture::Graphics::CommandSequence::k3dStackedUnderlay };

    drawGesture(/*display*/ true, graphics, sceneView, sequenceOrder);

    // The last thing we draw is selection codes for next frame. This allows us
    // to know what is under the pointer cursor.
    if (selection) {
      drawGestureCodes(
        *selection, sceneView.viewport, [&]() { drawGesture(/*display*/ false, graphics, sceneView, sequenceOrder); });
    }

    glBindVertexArray(0);
  }

  // Restore state
  glLineWidth(lineWidth);
  check_gl("linewidth");
  glPointSize(pointSize);
  check_gl("pointsize");
  if (depthTest) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  check_gl("toggle depth test");
}

uint32_t
selectionRGB8ToCode(const uint8_t* rgba)
{
  // ignores 4th component (== 0)
  uint32_t code = (uint32_t(rgba[0]) << 0) | (uint32_t(rgba[1]) << 8) | (uint32_t(rgba[2]) << 16);
  return code == 0xffffff ? Gesture::Graphics::k_noSelectionCode : code;
}

bool
GestureRendererGL::pick(SelectionBuffer& selection,
                        const Gesture::Input& input,
                        const SceneView::Viewport& viewport,
                        uint32_t& selectionCode)
{
  // Prepare a region in raster space

  SceneView::Viewport::Region region;
  {
    glm::ivec2 pixel = viewport.toRaster(input.cursorPos);

    // Grow the click position by some pixels to improve usability. Ideally
    // this should be a configurable parameter to improve accessibility.
    constexpr int kClickRadius = 7; //< in pixels
    region.extend(pixel - glm::ivec2(kClickRadius));
    region.extend(pixel + glm::ivec2(kClickRadius));
  }

  // Render on the whole framebuffer, complete from the lower left corner to the upper right
  SceneView::Viewport::Region viewRegion(viewport.region.lower, viewport.region.upper - glm::ivec2(1));

  // Crop selection with view in order to feed GL draw a valid region.
  region = SceneView::Viewport::Region::intersect(region, viewRegion);

  // if the intersection is empty, return no selection
  if (region.empty()) {
    selectionCode = Gesture::Graphics::k_noSelectionCode;
    return false;
  }

  // Frame buffer resolution should be correct, check just in case.
  if (selection.resolution != viewport.region.size()) {
    selectionCode = Gesture::Graphics::k_noSelectionCode;
    return false;
  }

  uint32_t entry = Gesture::Graphics::k_noSelectionCode;

  // Each selection code has a priority, lower values means higher priority.
  // I pick region around the cursor, the size of which is a arbitrary.
  // Depending on the purpose of an app, the size of the region should be
  // dictated by accessibility guidelines. The purpose of the region is to
  // allow to select thin elements, without having to be precise. I wouldn’t
  // want to draw thick “Lego Duplo” like lines just to be able to select
  // them. I do like the visual elegance of thin lines. At the same time,
  // selection codes do need a priority, I cannot just pick the code in the
  // nearest non-empty pixel.

  // Render to texture
  GLint last_framebuffer;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &last_framebuffer);
  check_gl("get last framebuffer");
  glBindFramebuffer(GL_FRAMEBUFFER, selection.frameBuffer);
  {
    // LOG_DEBUG << "Picking viewRegion " << viewRegion.lower.x << " " << viewRegion.lower.y << " " <<
    // viewRegion.upper.x
    //           << " " << viewRegion.upper.y;
    // LOG_DEBUG << "Picking region " << region.lower.x << " " << region.lower.y << " " << region.upper.x << " "
    //           << region.upper.y;
    glViewport(viewRegion.lower.x, viewRegion.lower.y, viewRegion.upper.x + 1, viewRegion.upper.y + 1);

    glm::ivec2 regionSize = region.size() + glm::ivec2(1);
    size_t size = size_t(regionSize.x) * size_t(regionSize.y);
    if (size) {
      // Read pixels over a region. What we read is an 32 bits unsigned partitioned into 8 bits
      // RGB values... at least until we figure out how to do it better.
      // If selection region is small, work on stack memory, otherwise allocate.
      uint8_t valuesLocalBuffer[1024 * 4];
      uint8_t* values = (size <= 1024 ? valuesLocalBuffer : (uint8_t*)malloc(size * 4));
      glReadPixels(region.lower.x, region.lower.y, regionSize.x, regionSize.y, GL_RGBA, GL_UNSIGNED_BYTE, values);
      check_gl("readpixels");

      // Search the click area for the lowest selection code. Lower code means
      // higher selection priority.
      for (uint8_t* rgba = values; rgba < (values + size * 4); rgba += 4) {
        uint32_t code = selectionRGB8ToCode(rgba);
        if (code != Gesture::Graphics::k_noSelectionCode) {
          if (code < entry) {
            entry = code;
          }
        }
      }

      if (values != valuesLocalBuffer) {
        free(values);
      }
    }
  }
  // Restore previous framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, last_framebuffer);
  check_gl("restore framebuffer");

  // if (entry < Gesture::Graphics::k_noSelectionCode) {
  //   LOG_DEBUG << "Selection: " << entry;
  // }
  selectionCode = entry;
  return entry != Gesture::Graphics::k_noSelectionCode;
}

GestureRendererGL::GestureRendererGL() {}

GestureRendererGL::~GestureRendererGL()
{
  // Destroy OpenGL resources
  vertex_buffer.reset();
  texture_buffer.reset();
  if (thickLinesVertexArray) {
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &thickLinesVertexArray);
    thickLinesVertexArray = 0;
  }
  shader.reset();
  shaderLines.reset();
  font.reset();
}