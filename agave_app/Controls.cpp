#include "Controls.h"

#include <QColorDialog>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtGlobal>

QColorPushButton::QColorPushButton(QWidget* pParent)
  : QPushButton(pParent)
  , m_Color(Qt::gray)
{
  setText("");
  // deal with a macos style bug which causes this button to misalign in some situations
  setAttribute(Qt::WA_LayoutUsesWidgetRect);
}

void
QColorPushButton::paintEvent(QPaintEvent* pPaintEvent)
{
  setText("");

  // QPushButton::paintEvent(pPaintEvent);

  QPainter painter(this);

  // Use the widget's full rectangle, not pPaintEvent->rect(). The event rect
  // is only the dirty sub-region (e.g. the strip newly exposed by scrolling),
  // and painting our rounded swatch into that strip alone leaves stale pixels
  // banding the rest of the button until the next full repaint. Qt will clip
  // our drawing to the actual dirty region automatically.
  QRect colorRectangle = rect();

  // Deflate it
  colorRectangle.adjust(m_Margin, m_Margin, -m_Margin, -m_Margin);

  // Use anti aliasing
  painter.setRenderHint(QPainter::Antialiasing);

  QPainterPath path;
  path.addRoundedRect(colorRectangle, m_Radius, m_Radius, Qt::AbsoluteSize);

  if (isEnabled() && !m_GradientStops.isEmpty()) {
    // Render the colormap gradient multiplied by the diffuse color, clipped
    // to the rounded rect.
    QLinearGradient gradient(colorRectangle.topLeft(), colorRectangle.topRight());
    gradient.setStops(m_GradientStops);

    painter.save();
    painter.setClipPath(path);
    painter.fillRect(colorRectangle, gradient);
    painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    painter.fillRect(colorRectangle, m_Color);
    painter.restore();
  } else {
    painter.fillPath(path, isEnabled() ? m_Color : Qt::lightGray);
  }

  QPen pen(isEnabled() ? QColor(25, 25, 25) : Qt::darkGray, 0.5);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);
}

void
QColorPushButton::mousePressEvent(QMouseEvent* pEvent)
{
  QColor lastColor = m_Color;
  QColorDialog colorDialog;
  colorDialog.setCustomColor(0, QColor(Qt::white));
  colorDialog.setCustomColor(1, QColor(Qt::red));
  colorDialog.setCustomColor(2, QColor(Qt::green));
  colorDialog.setCustomColor(3, QColor(Qt::blue));
  colorDialog.setCustomColor(4, QColor(Qt::cyan));
  colorDialog.setCustomColor(5, QColor(Qt::magenta));
  colorDialog.setCustomColor(6, QColor(Qt::yellow));

  connect(&colorDialog, SIGNAL(currentColorChanged(const QColor&)), this, SLOT(OnCurrentColorChanged(const QColor&)));

#ifdef __linux__
  colorDialog.setOption(QColorDialog::DontUseNativeDialog, true);
#endif
  colorDialog.setCurrentColor(m_Color);
  int result = colorDialog.exec();
  if (result == QDialog::Rejected) {
    OnCurrentColorChanged(lastColor);
  }

  disconnect(
    &colorDialog, SIGNAL(currentColorChanged(const QColor&)), this, SLOT(OnCurrentColorChanged(const QColor&)));
}

int
QColorPushButton::GetMargin() const
{
  return m_Margin;
}

void
QColorPushButton::SetMargin(const int& margin)
{
  m_Margin = margin;
  update();
}

int
QColorPushButton::GetRadius() const
{
  return m_Radius;
}

void
QColorPushButton::SetRadius(const int& radius)
{
  m_Radius = radius;
  update();
}

QColor
QColorPushButton::GetColor() const
{
  return m_Color;
}

void
QColorPushButton::SetColor(const QColor& color, bool blockSignals)
{
  this->blockSignals(blockSignals);

  QPalette pal = palette();
  pal.setColor(QPalette::Button, color);
  setPalette(pal);

  m_Color = color;
  update();
  if (!blockSignals) {
    emit currentColorChanged(m_Color);
  }

  this->blockSignals(false);
}

void
QColorPushButton::OnCurrentColorChanged(const QColor& color)
{
  SetColor(color, true);

  emit currentColorChanged(m_Color);
}

void
QColorPushButton::SetGradientStops(const QGradientStops& stops)
{
  m_GradientStops = stops;
  update();
}

QColorSelector::QColorSelector(QWidget* pParent /*= NULL*/)
  : QFrame(pParent)
  , m_ColorButton()
  , m_ColorCombo()
{
  setLayout(&m_MainLayout);

  m_MainLayout.addWidget(&m_ColorButton, 0, 0, Qt::AlignLeft);
  //	m_MainLayout.addWidget(&m_ColorCombo, 0, 1);

  m_MainLayout.setContentsMargins(0, 0, 0, 0);

  m_ColorButton.setFixedWidth(30);

  QObject::connect(
    &m_ColorButton, SIGNAL(currentColorChanged(const QColor&)), this, SLOT(OnCurrentColorChanged(const QColor&)));
}

QColor
QColorSelector::GetColor() const
{
  return m_ColorButton.GetColor();
}

void
QColorSelector::SetColor(const QColor& color, bool blockSignals /*= false*/)
{
  m_ColorButton.SetColor(color, blockSignals);
}

void
QColorSelector::OnCurrentColorChanged(const QColor& color)
{
  emit currentColorChanged(color);
}

QDoubleSlider::QDoubleSlider(QWidget* pParent /*= NULL*/)
  : QSlider(pParent)
{
  connect(this, SIGNAL(valueChanged(int)), this, SLOT(setValue(int)));

  setSingleStep(1);

  setOrientation(Qt::Horizontal);
  setFocusPolicy(Qt::StrongFocus);
}

void
QDoubleSlider::setValue(int value)
{
  emit valueChanged((double)value / m_Multiplier);
}

void
QDoubleSlider::setValue(double value, bool blockSignals)
{
  QSlider::blockSignals(blockSignals);

  QSlider::setValue(static_cast<int>(value * m_Multiplier));

  if (!blockSignals)
    emit valueChanged(value);

  QSlider::blockSignals(false);
}

void
QDoubleSlider::setRange(double rmin, double rmax)
{
  QSlider::setRange(static_cast<int>(rmin * m_Multiplier), static_cast<int>(rmax * m_Multiplier));

  emit rangeChanged(rmin, rmax);
}

void
QDoubleSlider::setMinimum(double dmin)
{
  QSlider::setMinimum(static_cast<int>(dmin * m_Multiplier));

  emit rangeChanged(dminimum(), dmaximum());
}

double
QDoubleSlider::dminimum() const
{
  return QSlider::minimum() / m_Multiplier;
}

void
QDoubleSlider::setMaximum(double dmax)
{
  QSlider::setMaximum(static_cast<int>(dmax * m_Multiplier));

  emit rangeChanged(dminimum(), dmaximum());
}

double
QDoubleSlider::dmaximum() const
{
  return QSlider::maximum() / m_Multiplier;
}

double
QDoubleSlider::value() const
{
  int value = QSlider::value();
  return sliderPositionToValue(value);
}

double
QDoubleSlider::sliderPositionToValue(int pos) const
{
  return (double)pos / m_Multiplier;
}

void
QDoubleSlider::wheelEvent(QWheelEvent* event)
{
  if (!hasFocus()) {
    event->ignore();
    return;
  }
  QSlider::wheelEvent(event);
}

QDoubleSpinner::QDoubleSpinner(QWidget* pParent /*= NULL*/)
  : QDoubleSpinBox(pParent)
{
  setKeyboardTracking(false);
}

void
QDoubleSpinner::setValue(double value, bool blockSignals)
{
  this->blockSignals(blockSignals);

  QDoubleSpinBox::setValue(value);

  this->blockSignals(false);
}

void
QDoubleSpinner::wheelEvent(QWheelEvent* event)
{
  if (!hasFocus()) {
    event->ignore();
    return;
  }
  QDoubleSpinBox::wheelEvent(event);
}

QNumericSlider::QNumericSlider(QWidget* pParent /*= NULL*/)
  : QWidget(pParent)
  , m_slider()
  , m_spinner()
{
  setLayout(&m_layout);

  m_slider.setOrientation(Qt::Horizontal);
  m_slider.setFocusPolicy(Qt::StrongFocus);
  m_spinner.setDecimals(2);
  m_spinner.setFocusPolicy(Qt::StrongFocus);

  m_slider.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  m_spinner.setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);

  //  entire control is one single row.
  //  slider is 3/4, spinner is 1/4 of the width
  const int sliderratio = 4;
  m_layout.addWidget(&m_slider, 3, Qt::AlignVCenter);
  m_layout.addWidget(&m_spinner, 1, Qt::AlignVCenter);
  // m_layout.addWidget(&m_slider, 0, 0, 1, sliderratio - 1);
  // m_layout.addWidget(&m_spinner, 0, sliderratio - 1, 1, 1);

  m_layout.setContentsMargins(0, 0, 0, 0);

  // keep slider and spinner in sync
  QObject::connect(&m_slider, &QDoubleSlider::valueChanged, [this](double v) {
    this->m_spinner.blockSignals(true);
    this->m_spinner.setValue(v, true);
    this->m_spinner.blockSignals(false);
  });

  QObject::connect(&m_spinner, QOverload<double>::of(&QDoubleSpinner::valueChanged), [this](double v) {
    // Disabling the spinner only because the valueChanged handler might do a long operation when the spinner is
    // clicked.
    //
    // If the operation is "too long" then the mouserelease of the spinner will kick in after a certain timeout
    // and trigger one more increment.
    //
    // If the owner has disabled tracking, that's a good bet that the increments are
    // expected to be "too long".
    if (!this->m_slider.hasTracking()) {
      this->m_spinner.setEnabled(false);
    }
    this->m_slider.setValue(v);
    if (!this->m_slider.hasTracking()) {
      this->m_spinner.setEnabled(true);
    }
  });

  // only slider will emit the value...
  QObject::connect(&m_slider, SIGNAL(valueChanged(double)), this, SLOT(OnValueChanged(double)));
}

QSize
QNumericSlider::sizeHint() const
{
  return { 100, 20 };
}

void
QNumericSlider::OnValueChanged(double value)
{
  emit valueChanged(value);
}

double
QNumericSlider::value() const
{
  return m_spinner.value();
}

void
QNumericSlider::setValue(double value, bool blockSignals)
{
  // only forward the blocksignals flag for one of the two child controls.
  // the other will always block signalling
  m_spinner.setValue(value, true);
  m_slider.setValue(value, blockSignals);
}

void
QNumericSlider::setRange(double rmin, double rmax)
{
  m_slider.setRange(rmin, rmax);
  m_spinner.setRange(rmin, rmax);
}

void
QNumericSlider::setSingleStep(double val)
{
  m_spinner.setSingleStep(val);
}

void
QNumericSlider::setDecimals(int decimals)
{
  m_spinner.setDecimals(decimals);
}

void
QNumericSlider::setSuffix(const QString& s)
{
  m_spinner.setSuffix(s);
}

void
QNumericSlider::setTracking(bool enabled)
{
  m_slider.setTracking(enabled);
}

QIntSlider::QIntSlider(QWidget* pParent /*= NULL*/)
  : QWidget(pParent)
  , m_slider()
  , m_spinner()
{
  setLayout(&m_layout);

  m_slider.setOrientation(Qt::Horizontal);
  m_slider.setFocusPolicy(Qt::NoFocus);

  m_spinner.setKeyboardTracking(false);

  // entire control is one single row.
  // slider is 3/4, spinner is 1/4 of the width
  const int sliderratio = 4;
  m_layout.addWidget(&m_slider, 0, 0, 1, sliderratio - 1);
  m_layout.addWidget(&m_spinner, 0, sliderratio - 1, 1, 1);

  m_layout.setContentsMargins(0, 0, 0, 0);

  // keep slider and spinner in sync
  QObject::connect(&m_slider, QOverload<int>::of(&QSlider::sliderMoved), [this](int v) {
    this->m_spinner.blockSignals(true);
    this->m_spinner.setValue(v);
    this->m_spinner.blockSignals(false);
  });
  // note that m_slider's tracking state controls how often the valueChanged signal is emitted.
  QObject::connect(&m_slider, &QSlider::valueChanged, [this](int v) {
    this->m_spinner.blockSignals(true);
    this->m_spinner.setValue(v);
    this->m_spinner.blockSignals(false);
  });

  QObject::connect(&m_spinner, QOverload<int>::of(&QSpinBox::valueChanged), [this](int v) {
    // This used to disable the spinner around setValue, because a valueChanged
    // handler could run a long blocking operation and the spinner's mouse-release
    // auto-repeat would then fire an extra increment. The only slider that
    // disabled tracking was the time slider, and time steps no longer block --
    // they are queued on the loader thread -- so the workaround is gone.
    this->m_slider.setValue(v);
  });

  // only slider will emit the value...
  QObject::connect(&m_slider, SIGNAL(valueChanged(int)), this, SLOT(OnValueChanged(int)));
}

void
QIntSlider::OnValueChanged(int value)
{
  emit valueChanged(value);
}

void
QIntSlider::setSpinnerKeyboardTracking(bool tracking)
{
  m_spinner.setKeyboardTracking(tracking);
}

int
QIntSlider::value() const
{
  return m_spinner.value();
}

int
QIntSlider::maximum() const
{
  return m_spinner.maximum();
}

void
QIntSlider::setValue(int value, bool blockSignals)
{
  // only forward the blocksignals flag for one of the two child controls.
  // the other will always block signalling
  m_spinner.blockSignals(true);
  m_slider.blockSignals(blockSignals);
  m_spinner.setValue(value);
  m_slider.setValue(value);
  m_spinner.blockSignals(false);
  m_slider.blockSignals(false);
  if (!blockSignals) {
    emit valueChanged(value);
  }
}

void
QIntSlider::setRange(int rmin, int rmax)
{
  m_slider.setRange(rmin, rmax);
  m_spinner.setRange(rmin, rmax);
}

void
QIntSlider::setSingleStep(int val)
{
  m_spinner.setSingleStep(val);
}

void
QIntSlider::setSuffix(const QString& s)
{
  m_spinner.setSuffix(s);
}

void
QIntSlider::setTickPosition(QSlider::TickPosition position)
{
  m_slider.setTickPosition(position);
}

void
QIntSlider::setTickInterval(int ti)
{
  m_slider.setTickInterval(ti);
}

void
QIntSlider::setTracking(bool enabled)
{
  m_slider.setTracking(enabled);
}

void
AgaveFormLayout::addRow(const QString& label, QWidget* widget)
{
  int row = rowCount();
  auto* labelWidget = new QLabel(label);
  labelWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  labelWidget->setToolTip(widget->toolTip());
  labelWidget->setStatusTip(widget->statusTip());
  addWidget(labelWidget, row, 0, Qt::AlignLeft);
  addWidget(widget, row, 1);
}

void
AgaveFormLayout::addRow(const QString& label, QLayout* layout)
{
  int row = rowCount();
  auto* labelWidget = new QLabel(label);
  labelWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  addWidget(labelWidget, row, 0, Qt::AlignLeft);
  addLayout(layout, row, 1);
}

void
AgaveFormLayout::addRow(QWidget* section)
{
  int row = rowCount();
  addWidget(section, row, 0, 1, 2);
}

QFormLayout*
Controls::createFormLayout(QWidget* parent)
{
  QFormLayout* layout = new QFormLayout(parent);
  initFormLayout(*layout);
  return layout;
}

void
Controls::initFormLayout(QFormLayout& layout)
{
  layout.setRowWrapPolicy(QFormLayout::DontWrapRows);
  layout.setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  layout.setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
  layout.setLabelAlignment(Qt::AlignRight);
}

AgaveFormLayout*
Controls::createAgaveFormLayout(QWidget* parent)
{
  AgaveFormLayout* layout = new AgaveFormLayout(parent);
  // basically keep the label column a fixed width and let the widget column grow
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(1, 100);
  return layout;
}

static void
applyTipsToWidget(QWidget* w, const QString& toolTip, const QString& statusTip)
{
  if (!w) {
    return;
  }
  if (!toolTip.isEmpty()) {
    w->setToolTip(toolTip);
  }
  if (!statusTip.isEmpty()) {
    w->setStatusTip(statusTip);
  }
}

void
Controls::addFormRow(QFormLayout* layout,
                     const QString& labelText,
                     QWidget* field,
                     const QString& toolTip,
                     const QString& statusTip)
{
  layout->addRow(labelText, field);
  applyTipsToWidget(field, toolTip, statusTip);
  applyTipsToWidget(layout->labelForField(field), toolTip, statusTip);
}

void
Controls::addFormRow(QFormLayout* layout,
                     const QString& labelText,
                     QLayout* fieldLayout,
                     const QString& toolTip,
                     const QString& statusTip)
{
  layout->addRow(labelText, fieldLayout);
  applyTipsToWidget(layout->labelForField(fieldLayout), toolTip, statusTip);
}

void
Controls::addFormRow(AgaveFormLayout* layout,
                     const QString& labelText,
                     QWidget* field,
                     const QString& toolTip,
                     const QString& statusTip)
{
  // Apply to the field first so AgaveFormLayout::addRow's existing tooltip-copy
  // logic also picks it up, then explicitly apply to the label as well.
  applyTipsToWidget(field, toolTip, statusTip);
  layout->addRow(labelText, field);
  QLayoutItem* item = layout->itemAtPosition(layout->rowCount() - 1, 0);
  applyTipsToWidget(item ? item->widget() : nullptr, toolTip, statusTip);
}

void
Controls::addFormRow(AgaveFormLayout* layout,
                     const QString& labelText,
                     QLayout* fieldLayout,
                     const QString& toolTip,
                     const QString& statusTip)
{
  layout->addRow(labelText, fieldLayout);
  QLayoutItem* item = layout->itemAtPosition(layout->rowCount() - 1, 0);
  applyTipsToWidget(item ? item->widget() : nullptr, toolTip, statusTip);
}
