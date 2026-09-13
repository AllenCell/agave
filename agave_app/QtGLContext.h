#pragma once

#include "renderlib/gfxapi/IGLContext.h"

#include <QSurfaceFormat>

class QOffscreenSurface;
class QOpenGLContext;
class QThread;

class QtGLContext : public gfxApi::IGLContext
{
public:
  explicit QtGLContext(QOpenGLContext* context = nullptr);
  ~QtGLContext() override;

  static QSurfaceFormat defaultSurfaceFormat(bool enableDebug = false);
  static void setDefaultSurfaceFormat(bool enableDebug = false);

  bool create() override;
  bool isValid() const override;
  bool makeCurrent() override;
  void doneCurrent() override;

  void moveToThread(QThread* thread);

private:
  void destroy();

  bool m_ownContext = false;
  QOpenGLContext* m_context = nullptr;
  QOffscreenSurface* m_surface = nullptr;
};
