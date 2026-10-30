#include "ViewToolbar.h"

#include "renderlib/CCamera.h"

#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QPushButton>
#include <QSvgRenderer>
#include <QToolBar>
#include <QWidgetAction>
#include <QMenu>

ViewToolbar::ViewToolbar(QWidget* parent)
  : QWidget(parent)
{
  QHBoxLayout* toolbarLayout = new QHBoxLayout(this);
  toolbarLayout->setSpacing(1);
  toolbarLayout->setContentsMargins(4, 4, 4, 4);

  static const int spacing = 8;

  homeButton = new QPushButton(QIcon(), "", this);
  homeButton->setObjectName("homeBtn");
  homeButton->setToolTip(QString("<FONT>Reset view</FONT>"));
  homeButton->setStatusTip(tr("Reset the view"));
  homeButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  homeButton->adjustSize();
  homeButton->setFocusPolicy(Qt::NoFocus);
  toolbarLayout->addWidget(homeButton);

  frameViewButton = new QPushButton(QIcon(), "", this);
  frameViewButton->setObjectName("frameViewBtn");
  frameViewButton->setToolTip(QString("<FONT>Frame view</FONT>"));
  frameViewButton->setStatusTip(tr("Frame the current view"));
  frameViewButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  frameViewButton->adjustSize();
  frameViewButton->setFocusPolicy(Qt::NoFocus);
  toolbarLayout->addWidget(frameViewButton);

  toolbarLayout->addItem(new QSpacerItem(spacing, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));

  orthoViewButton = new DualIconButton(QString("<FONT>Switch to orthographic view</FONT>"),
                                       tr("Switch to orthographic view"),
                                       QString("<FONT>Switch to perspective view</FONT>"),
                                       tr("Switch to perspective view"),
                                       this);
  orthoViewButton->setObjectName("orthoViewBtn");
  orthoViewButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  orthoViewButton->adjustSize();
  orthoViewButton->setFocusPolicy(Qt::NoFocus);
  toolbarLayout->addWidget(orthoViewButton);

  topViewButton = new QPushButton(QIcon(), "", this);
  topViewButton->setObjectName("topViewBtn");
  topViewButton->setToolTip(QString("<FONT>Top view (-Y)</FONT>"));
  topViewButton->setStatusTip(tr("Set the view to look down the negative Y axis"));

  bottomViewButton = new QPushButton(QIcon(), "", this);
  bottomViewButton->setObjectName("bottomViewBtn");
  bottomViewButton->setToolTip(QString("<FONT>Bottom view (+Y)</FONT>"));
  bottomViewButton->setStatusTip(tr("Set the view to look down the positive Y axis"));

  frontViewButton = new QPushButton(QIcon(), "", this);
  frontViewButton->setObjectName("frontViewBtn");
  frontViewButton->setToolTip(QString("<FONT>Front view (-Z)</FONT>"));
  frontViewButton->setStatusTip(tr("Set the view to look down the negative Z axis"));

  backViewButton = new QPushButton(QIcon(), "", this);
  backViewButton->setObjectName("backViewBtn");
  backViewButton->setToolTip(QString("<FONT>Back view (+Z)</FONT>"));
  backViewButton->setStatusTip(tr("Set the view to look down the positive Z axis"));

  leftViewButton = new QPushButton(QIcon(), "", this);
  leftViewButton->setObjectName("leftViewBtn");
  leftViewButton->setToolTip(QString("<FONT>Left view (+X)</FONT>"));
  leftViewButton->setStatusTip(tr("Set the view to look down the positive X axis"));

  rightViewButton = new QPushButton(QIcon(), "", this);
  rightViewButton->setObjectName("rightViewBtn");
  rightViewButton->setToolTip(QString("<FONT>Right view (-X)</FONT>"));
  rightViewButton->setStatusTip(tr("Set the view to look down the negative X axis"));

  toolbarLayout->addItem(new QSpacerItem(spacing, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));

  axisHelperButton = new QPushButton(QIcon(), "", this);
  axisHelperButton->setObjectName("axisHelperBtn");
  axisHelperButton->setToolTip(QString("<FONT>Show/Hide axis helper</FONT>"));
  axisHelperButton->setStatusTip(tr("Show/Hide axis helper"));
  axisHelperButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  axisHelperButton->adjustSize();
  axisHelperButton->setFocusPolicy(Qt::NoFocus);
  toolbarLayout->addWidget(axisHelperButton);

  toolbarLayout->addItem(new QSpacerItem(spacing, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));

  QMenu* menu = new QMenu("QuickViews", this);
  menu->setObjectName("quickViewsMenu");
  menu->setAttribute(Qt::WA_TranslucentBackground);
  QToolBar* toolbar = new QToolBar();
  toolbar->setObjectName("quickViewsToolbar");
  toolbar->addWidget(rightViewButton);
  toolbar->addWidget(leftViewButton);
  toolbar->addWidget(frontViewButton);
  toolbar->addWidget(backViewButton);
  toolbar->addWidget(topViewButton);
  toolbar->addWidget(bottomViewButton);
  QWidgetAction* act = new QWidgetAction(toolbar);
  act->setDefaultWidget(toolbar);
  menu->addAction(act);

  axisViewButton = new QPushButton(this);
  axisViewButton->setObjectName("anyViewBtn");
  axisViewButton->setToolTip(QString("<FONT>Quick Views</FONT>"));
  axisViewButton->setStatusTip(tr("Quickly set an axis-aligned view"));
  axisViewButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
  axisViewButton->adjustSize();
  axisViewButton->setFocusPolicy(Qt::NoFocus);
  toolbarLayout->addWidget(axisViewButton);
  connect(axisViewButton, &QPushButton::clicked, [menu, this]() {
    menu->exec(axisViewButton->mapToGlobal(axisViewButton->rect().bottomLeft() + QPoint(0, 4)));
  });

#if 0
  toolbarLayout->addItem(new QSpacerItem(spacing, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));

  rotateButton = new QPushButton(QIcon(), "", this);
  rotateButton->setCheckable(true);
  rotateButton->setDisabled(true);
  rotateButton->setObjectName("rotateBtn");
  rotateButton->setToolTip(QString("<FONT>Rotate</FONT>"));
  rotateButton->setStatusTip(tr("Rotate the selected object"));
  rotateButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  rotateButton->adjustSize();
  rotateButton->setFocusPolicy(Qt::NoFocus);
  toolbarLayout->addWidget(rotateButton);

  translateButton = new QPushButton(QIcon(), "", this);
  translateButton->setCheckable(true);
  translateButton->setDisabled(true);
  translateButton->setObjectName("translateBtn");
  translateButton->setToolTip(QString("<FONT>Translate</FONT>"));
  translateButton->setStatusTip(tr("Translate the selected object"));
  translateButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  translateButton->adjustSize();
  translateButton->setFocusPolicy(Qt::NoFocus);
  toolbarLayout->addWidget(translateButton);
#endif

  toolbarLayout->addStretch();

  setLayout(toolbarLayout);
  setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
}

ViewToolbar::~ViewToolbar() {}

void
ViewToolbar::initFromCamera(const CCamera& camera)
{
  orthoViewButton->setState((camera.m_Projection == ProjectionMode::ORTHOGRAPHIC) ? 1 : 0);
}

void
ViewToolbar::setRendererMode(int rendererType)
{
  const bool is3d = rendererType < 2;
  const bool isSingleSlice = rendererType >= 2 && rendererType <= 4;

  // Slice views have a fixed orientation. Triple slice also has a fixed fitted
  // layout, while a single slice can still be returned to its fitted view.
  homeButton->setEnabled(is3d || isSingleSlice);
  frameViewButton->setEnabled(is3d || isSingleSlice);
  orthoViewButton->setEnabled(is3d);
  axisViewButton->setEnabled(is3d);
  axisHelperButton->setEnabled(is3d);
  topViewButton->setEnabled(is3d);
  bottomViewButton->setEnabled(is3d);
  frontViewButton->setEnabled(is3d);
  backViewButton->setEnabled(is3d);
  leftViewButton->setEnabled(is3d);
  rightViewButton->setEnabled(is3d);
}
