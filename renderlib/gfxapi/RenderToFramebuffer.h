#pragma once

#include "Framebuffer.h"
#include "IGestureRenderer.h"
#include "IRenderWindow.h"

namespace gfxApi {

void
renderToFramebuffer(Framebuffer& framebuffer,
                    IRenderWindow& renderer,
                    IGestureRenderer& gestureRenderer,
                    SceneView& sceneView,
                    Gesture::Graphics& graphics,
                    float backgroundAlpha);

} // namespace gfxApi
