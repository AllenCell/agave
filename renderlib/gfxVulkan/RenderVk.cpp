#include "RenderVk.h"

#include "Logging.h"
#include "RenderSettings.h"

namespace gfxvulkan {

RenderVk::RenderVk(Backend& backend, RenderSettings* renderSettings)
  : m_backend(backend)
  , m_renderSettings(renderSettings)
  , m_status(new CStatus)
{
}

RenderVk::~RenderVk() = default;

void
RenderVk::initialize(uint32_t w, uint32_t h)
{
  resize(w, h);
  m_status->SetRenderBegin();
}

void
RenderVk::resize(uint32_t w, uint32_t h)
{
  m_w = w;
  m_h = h;
  if (m_renderSettings) {
    m_renderSettings->SetNoIterations(0);
  }
}

void
RenderVk::getSize(uint32_t& w, uint32_t& h)
{
  w = m_w;
  h = m_h;
}

void
RenderVk::render(const CCamera& camera)
{
  (void)camera;
  (void)m_backend;
  logUnimplementedOnce();
}

void
RenderVk::renderTo(const CCamera& camera, gfxApi::Framebuffer* fbo)
{
  (void)camera;
  logUnimplementedOnce();
  if (fbo) {
    fbo->clear(backgroundClearColor());
  }
}

void
RenderVk::cleanUpResources()
{
}

RenderSettings&
RenderVk::renderSettings()
{
  return *m_renderSettings;
}

Scene*
RenderVk::scene()
{
  return m_scene;
}

void
RenderVk::setScene(Scene* s)
{
  m_scene = s;
}

gfxApi::ClearColor
RenderVk::backgroundClearColor() const
{
  if (!m_scene) {
    return {};
  }
  return { m_scene->m_material.m_backgroundColor[0],
           m_scene->m_material.m_backgroundColor[1],
           m_scene->m_material.m_backgroundColor[2],
           1.0f };
}

void
RenderVk::logUnimplementedOnce()
{
  if (m_loggedUnimplemented) {
    return;
  }
  LOG_WARNING << "gfxvulkan::RenderVk: Vulkan volume rendering pipeline is not implemented yet";
  m_status->SetStatisticChanged("Vulkan", "Renderer", "pipeline not implemented");
  m_loggedUnimplemented = true;
}

} // namespace gfxvulkan
