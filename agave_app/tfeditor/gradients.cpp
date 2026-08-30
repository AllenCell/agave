#include "gradients.h"

#include "Controls.h"
#include "qcustomplot.h"
#include "renderlib/Defines.h"
#include "renderlib/Logging.h"
#include "renderlib/MathUtil.h"

#include <algorithm>
#include <cmath>

std::vector<LutControlPoint>
gradientStopsToVector(QGradientStops& stops)
{
  std::vector<LutControlPoint> v;
  for (int i = 0; i < stops.size(); ++i) {
    v.push_back(LutControlPoint(stops.at(i).first, stops.at(i).second.alphaF()));
  }
  return v;
}

QGradientStops
vectorToGradientStops(std::vector<LutControlPoint>& v)
{
  QGradientStops stops;
  for (int i = 0; i < v.size(); ++i) {
    stops.push_back(
      QPair<qreal, QColor>(v[i].first, QColor::fromRgbF(v[i].second, v[i].second, v[i].second, v[i].second)));
  }
  return stops;
}

static void
bound_point(double x, double y, const QRectF& bounds, int lock, double& out_x, double& out_y)
{
  qreal left = bounds.left();
  qreal right = bounds.right();
  // notice top/bottom switch here.
  qreal bottom = bounds.top();
  qreal top = bounds.bottom();

  out_x = x;
  out_y = y;

  if (x <= left || (lock & GradientEditor::LockToLeft))
    out_x = left;
  else if (x >= right || (lock & GradientEditor::LockToRight))
    out_x = right;

  if (y >= top || (lock & GradientEditor::LockToTop))
    out_y = top;
  else if (y <= bottom || (lock & GradientEditor::LockToBottom))
    out_y = bottom;
}

static constexpr double SCATTERSIZE = 10.0;
// Grow the clickable area of control points beyond their drawn size so they are easier to grab.
static constexpr double SCATTER_PICK_PADDING = 4.0;
static constexpr double SCATTER_PICK_RADIUS = SCATTERSIZE / 2.0 + SCATTER_PICK_PADDING;
static constexpr double MIN_HISTOGRAM_BAR_HEIGHT_LOG = 0.001;
static constexpr double HISTOGRAM_Y_HEADROOM = 1.1;
static constexpr int MIN_BAR_HEIGHT_PIXELS = 2;

static double
getMinHistogramBarHeight(QCustomPlot* plot, bool logScale)
{
  double minBarHeight = logScale ? MIN_HISTOGRAM_BAR_HEIGHT_LOG : 0.0;
  if (!plot) {
    return minBarHeight;
  }

  auto* axisRect = plot->axisRect();
  if (!axisRect || !plot->yAxis2) {
    return minBarHeight;
  }

  // Compute the coordinate value that sits MIN_BAR_HEIGHT_PIXELS above the bar base.
  // In log mode, bars start at MIN_HISTOGRAM_BAR_HEIGHT_LOG; in linear mode they start at 0.
  double baseValue = logScale ? MIN_HISTOGRAM_BAR_HEIGHT_LOG : 0.0;
  double basePixel = plot->yAxis2->coordToPixel(baseValue);
  // pixel Y decreases upward, so subtract to go up
  double topCoord = plot->yAxis2->pixelToCoord(basePixel - MIN_BAR_HEIGHT_PIXELS);
  if (topCoord > baseValue) {
    minBarHeight = std::max(minBarHeight, topCoord);
  }

  return minBarHeight;
}

GradientEditor::GradientEditor(const Histogram& histogram, QWidget* parent)
  : QWidget(parent)
  , m_histogram(histogram)
{
  QVBoxLayout* vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(1);

  m_customPlot = new QCustomPlot(this);

  // yAxis is for intensity transfer function
  m_customPlot->yAxis->setVisible(true);
  m_customPlot->yAxis->setTicks(true);
  m_customPlot->yAxis->setTickLabels(true);
  m_customPlot->yAxis->grid()->setVisible(false);
  m_customPlot->yAxis->grid()->setSubGridVisible(false);

  // yAxis2 is for histogram
  m_customPlot->yAxis2->setVisible(true);
  m_customPlot->yAxis2->setTicks(true);
  m_customPlot->yAxis2->setTickLabels(false);
  m_customPlot->yAxis2->setSubTicks(false);
  m_customPlot->yAxis2->grid()->setVisible(true);
  m_customPlot->yAxis2->grid()->setSubGridVisible(false);
  m_customPlot->yAxis2->setRange(0.0, 1.0);
  m_customPlot->yAxis2->setScaleType(QCPAxis::stLinear);

  // first graph will be histogram
  m_histogramBars = new QCPBars(m_customPlot->xAxis, m_customPlot->yAxis2);
  QPalette pal = m_customPlot->palette();
  QColor histFillColor = pal.color(QPalette::Link).lighter(150);
  QBrush barBrush = m_histogramBars->brush();
  barBrush.setColor(histFillColor);
  m_histogramBars->setBrush(barBrush);
  m_histogramBars->setPen(Qt::NoPen);
  m_histogramBars->setWidthType(QCPBars::wtPlotCoords);

  m_histogramBars->setSelectable(QCP::stNone);

  // first added graph will the the piecewise linear transfer function
  m_customPlot->addGraph();
  m_customPlot->graph(0)->setPen(QPen(Qt::black));
  QPen scatterPen(Qt::black);
  scatterPen.setWidthF(1.0);
  m_customPlot->graph(0)->setScatterStyle(
    QCPScatterStyle(QCPScatterStyle::ssCircle, scatterPen, Qt::NoBrush, SCATTERSIZE));
  m_customPlot->graph(0)->setSelectable(QCP::stSingleData);

  //   give the axes some labels:
  m_customPlot->xAxis->setLabel("");
  m_customPlot->yAxis->setLabel("");

  // set axes ranges, so we see all data:
  m_customPlot->xAxis->setRange(histogram.getFilteredMin(), histogram.getFilteredMax());
  m_customPlot->xAxis->ticker()->setTickCount(4);
  m_customPlot->xAxis->ticker()->setTickOrigin(histogram.getDataMin());
  auto tickLabelFont = m_customPlot->xAxis->tickLabelFont();
  tickLabelFont.setPointSize((float)tickLabelFont.pointSize() * 0.75);
  m_customPlot->xAxis->setTickLabelFont(tickLabelFont);
  QPen penx = m_customPlot->xAxis->basePen();
  penx.setWidthF(1.0);
  m_customPlot->xAxis->setBasePen(penx);

  // increasing this will extend the Y axis up and down, so that the scatter handles are not clipped.
  static constexpr double AXIS_OFFSET_FRACTION = 0.0;
  m_customPlot->xAxis->setOffset(AXIS_OFFSET_FRACTION);

  m_customPlot->yAxis->setRange(0 - AXIS_OFFSET_FRACTION, 1 + AXIS_OFFSET_FRACTION);
  m_customPlot->yAxis->ticker()->setTickCount(1);
  tickLabelFont = m_customPlot->yAxis->tickLabelFont();
  tickLabelFont.setPointSize((float)tickLabelFont.pointSize() * 0.75);
  m_customPlot->yAxis->setTickLabelFont(tickLabelFont);
  QPen peny = m_customPlot->yAxis->basePen();
  peny.setWidthF(1.0);
  m_customPlot->yAxis->setBasePen(peny);

  m_customPlot->xAxis->grid()->setVisible(true);
  m_customPlot->xAxis->grid()->setSubGridVisible(true);
  m_customPlot->yAxis->grid()->setVisible(false);
  m_customPlot->yAxis->grid()->setSubGridVisible(false);

  m_customPlot->setInteractions(
    QCP::iRangeDrag | QCP::iRangeZoom |
    QCP::iSelectPlottables); // allow user to drag axis ranges with mouse, zoom with mouse wheel
  auto* axisRect = m_customPlot->axisRect();
  axisRect->setAutoMargins(QCP::msLeft | QCP::msRight | QCP::msBottom);
  axisRect->setMargins(QMargins(0, 12, 0, 0));
  axisRect->setMinimumMargins(QMargins(0, 12, 0, 0));
  axisRect->setRangeDrag(Qt::Horizontal);
  axisRect->setRangeZoom(Qt::Horizontal);

  m_customPlot->replot();

  connect(m_customPlot, &QCustomPlot::mousePress, this, &GradientEditor::onPlotMousePress);
  connect(m_customPlot, &QCustomPlot::mouseMove, this, &GradientEditor::onPlotMouseMove);
  connect(m_customPlot, &QCustomPlot::mouseRelease, this, &GradientEditor::onPlotMouseRelease);
  connect(m_customPlot, &QCustomPlot::mouseWheel, this, &GradientEditor::onPlotMouseWheel);
  connect(m_customPlot, &QCustomPlot::mouseDoubleClick, this, &GradientEditor::onPlotMouseDoubleClick);
  connect(m_customPlot->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged), this, [this](const QCPRange&) {
    this->updateHistogramForVisibleRange();
  });

  vbox->addWidget(m_customPlot);

  updateHistogramForVisibleRange();
}

void
GradientEditor::updateHistogramForVisibleRange()
{
  updateHistogramBarGraph();
  updateHistogramYAxisRange();
  m_customPlot->replot(QCustomPlot::rpQueuedReplot);
}

void
GradientEditor::updateHistogramBarGraph()
{
  if (!m_customPlot) {
    return;
  }

  QCPRange xRange = m_customPlot->xAxis->range();
  double visibleMin = xRange.lower;
  double visibleMax = xRange.upper;
  // not sure why this might happen but just in case
  if (visibleMin > visibleMax) {
    std::swap(visibleMin, visibleMax);
  }

  // because we work with uint16_t data, let's put our bins on integer boundaries
  // this makes the graph display cleaner and is conceptually easier to debug
  visibleMin = std::max(visibleMin, static_cast<double>(m_histogram.getDataMin()));
  visibleMax = std::min(visibleMax, static_cast<double>(m_histogram.getDataMax()));
  visibleMin = std::floor(visibleMin);
  visibleMax = std::ceil(visibleMax);
  // not sure why this might happen but just in case
  if (visibleMin > visibleMax) {
    std::swap(visibleMin, visibleMax);
  }

  size_t numBins = m_histogram.getNumDisplayBins();
  if (numBins == 0) {
    return;
  }

  // if the x range is small enough, we can use fewer bins
  double visibleRange = visibleMax - visibleMin;
  if (visibleRange < static_cast<double>(numBins)) {
    size_t maxBinsForRange = static_cast<size_t>(std::floor(std::max(visibleRange, 0.0))) + 1;
    numBins = std::max<size_t>(1, std::min(numBins, maxBinsForRange));
  }

  m_visibleHistogramBins =
    m_histogram.computeForDisplay(static_cast<float>(visibleMin), static_cast<float>(visibleMax), numBins);

  double binSize = 0.0;
  if (numBins > 1) {
    binSize = (visibleMax - visibleMin) / static_cast<double>(numBins - 1);
  }
  if (binSize <= 0.0) {
    binSize = 1.0;
  }
  m_histogramBars->setWidth(binSize);

  uint32_t modalCount = 0;
  for (uint32_t count : m_visibleHistogramBins) {
    modalCount = std::max(modalCount, count);
  }

  QVector<double> keyData;
  QVector<double> valueData;
  double minBarHeight = getMinHistogramBarHeight(m_customPlot, m_histogramLogScale);
  for (size_t i = 0; i < numBins; ++i) {
    keyData << visibleMin + static_cast<double>(i) * binSize;
    uint32_t count = m_visibleHistogramBins[i];
    if (count == 0) {
      // Zero bins are clamped in log mode to avoid log(0).
      valueData << (m_histogramLogScale ? minBarHeight : 0.0);
    } else {
      // Nonzero bins get at least the minimum height
      // to be visible at all.
      double normalizedHeight = static_cast<double>(count) / static_cast<double>(std::max<uint32_t>(modalCount, 1));
      valueData << std::max(normalizedHeight, minBarHeight);
    }
  }
  m_histogramBars->setData(keyData, valueData);
}

void
GradientEditor::updateHistogramYAxisRange()
{
  if (!m_customPlot) {
    return;
  }

  size_t numBins = m_visibleHistogramBins.size();
  if (numBins == 0) {
    return;
  }

  // at least 1 to avoid division by 0 later.
  uint32_t modalCount = 1;
  for (uint32_t count : m_visibleHistogramBins) {
    modalCount = std::max(modalCount, count);
  }

  double maxVisible = 0.0;
  double minBarHeight = getMinHistogramBarHeight(m_customPlot, m_histogramLogScale);
  for (size_t i = 0; i < numBins; ++i) {
    uint32_t count = m_visibleHistogramBins[i];
    double value = 0.0;
    if (count == 0) {
      value = m_histogramLogScale ? minBarHeight : 0.0;
    } else {
      double normalizedHeight = static_cast<double>(count) / static_cast<double>(modalCount);
      value = std::max(normalizedHeight, minBarHeight);
    }
    maxVisible = std::max(maxVisible, value);
  }

  if (maxVisible <= 0.0) {
    maxVisible = m_histogramLogScale ? minBarHeight : 1.0;
  }

  if (m_histogramLogScale) {
    double lower = MIN_HISTOGRAM_BAR_HEIGHT_LOG;
    double upper = std::max(maxVisible * HISTOGRAM_Y_HEADROOM, lower * HISTOGRAM_Y_HEADROOM);
    m_customPlot->yAxis2->setRange(lower, upper);
  } else {
    double upper = std::max(maxVisible * HISTOGRAM_Y_HEADROOM, 0.0);
    m_customPlot->yAxis2->setRange(0.0, upper);
  }
}

void
GradientEditor::setHistogram(const Histogram& histogram)
{
  m_histogram = histogram;

  updateHistogramForVisibleRange();
}

void
GradientEditor::setYAxisLogScale(bool enabled)
{
  if (!m_customPlot) {
    return;
  }

  m_histogramLogScale = enabled;
  m_customPlot->yAxis->setVisible(true);
  m_customPlot->yAxis->setTicks(true);
  m_customPlot->yAxis->setTickLabels(true);
  m_customPlot->yAxis->grid()->setVisible(false);
  m_customPlot->yAxis->grid()->setSubGridVisible(false);
  m_customPlot->yAxis2->setVisible(true);
  m_customPlot->yAxis2->setTicks(true);
  m_customPlot->yAxis2->setTickLabels(false);
  m_customPlot->yAxis2->setSubTicks(false);
  m_customPlot->yAxis2->grid()->setVisible(true);
  m_customPlot->yAxis2->grid()->setSubGridVisible(false);
  m_customPlot->yAxis2->setScaleType(enabled ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
  updateHistogramForVisibleRange();
}

void
GradientEditor::changeEvent(QEvent* event)
{
  // This might be too many event types to check, but ThemeChange only seems to work on the QMainWindow.
  // At least on Windows, changing dark mode to light mode incurs StyleChange and PaletteChange events too.
  if (event->type() == QEvent::ThemeChange || event->type() == QEvent::ApplicationPaletteChange ||
      event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
    // check for dark or light mode
    auto sh = QGuiApplication::styleHints();
    auto colorScheme = sh->colorScheme();
    QColor plotLineColor;
    QColor backgroundColor;
    QColor barsColor;
    QColor gridColor;
    QColor subgridColor;
    if (colorScheme == Qt::ColorScheme::Dark) {
      barsColor = Qt::magenta;
      barsColor.setAlphaF(0.5);
      plotLineColor = this->palette().color(QPalette::Text);
      backgroundColor = this->palette().color(QPalette::Window);
      gridColor = plotLineColor.darker(150);
      gridColor.setAlphaF(0.75);
      subgridColor = plotLineColor.darker(170);
      subgridColor.setAlphaF(0.5);
    } else if (colorScheme == Qt::ColorScheme::Light) {
      barsColor = Qt::magenta;
      barsColor.setAlphaF(0.25);
      plotLineColor = this->palette().color(QPalette::Text);
      backgroundColor = this->palette().color(QPalette::Window);
      gridColor = plotLineColor.lighter(150);
      gridColor.setAlphaF(0.75);
      subgridColor = plotLineColor.lighter(170);
      subgridColor.setAlphaF(0.5);
    }
    m_customPlot->graph(0)->setPen(QPen(plotLineColor));
    QPen scatterPen(plotLineColor);
    scatterPen.setWidthF(1.0);
    m_customPlot->graph(0)->setScatterStyle(
      QCPScatterStyle(QCPScatterStyle::ssCircle, scatterPen, Qt::NoBrush, SCATTERSIZE));
    m_customPlot->setBackground(QBrush(backgroundColor));
    m_customPlot->axisRect();

    QPen basepen = m_customPlot->xAxis->basePen();
    basepen.setColor(plotLineColor);
    m_customPlot->xAxis->setBasePen(basepen);
    m_customPlot->yAxis->setBasePen(basepen);
    m_customPlot->yAxis2->setBasePen(basepen);

    QPen gridpen = m_customPlot->xAxis->grid()->pen();
    gridpen.setColor(gridColor);
    m_customPlot->xAxis->grid()->setPen(gridpen);
    m_customPlot->yAxis->grid()->setPen(gridpen);
    m_customPlot->yAxis2->grid()->setPen(gridpen);
    QPen subgridpen = m_customPlot->xAxis->grid()->subGridPen();
    subgridpen.setColor(subgridColor);
    m_customPlot->xAxis->grid()->setSubGridPen(subgridpen);
    m_customPlot->yAxis->grid()->setSubGridPen(subgridpen);
    m_customPlot->yAxis2->grid()->setSubGridPen(subgridpen);
    m_customPlot->xAxis->grid()->setAntialiasedSubGrid(true);
    m_customPlot->yAxis->grid()->setAntialiasedSubGrid(true);
    m_customPlot->yAxis2->grid()->setAntialiasedSubGrid(true);

    QPen axisTickPen = m_customPlot->xAxis->tickPen();
    axisTickPen.setColor(plotLineColor);
    m_customPlot->xAxis->setTickPen(axisTickPen);
    m_customPlot->yAxis->setTickPen(axisTickPen);
    m_customPlot->yAxis2->setTickPen(axisTickPen);
    m_customPlot->xAxis->setTickLabelColor(plotLineColor);
    m_customPlot->yAxis->setTickLabelColor(plotLineColor);
    m_customPlot->yAxis2->setTickLabelColor(plotLineColor);
    QPen axisSubTickPen = m_customPlot->xAxis->subTickPen();
    axisSubTickPen.setColor(plotLineColor);
    m_customPlot->xAxis->setSubTickPen(axisSubTickPen);
    m_customPlot->yAxis->setSubTickPen(axisSubTickPen);
    m_customPlot->yAxis2->setSubTickPen(axisSubTickPen);

    m_histogramBars->setPen(Qt::NoPen); // QPen(barsColor));
    m_histogramBars->setBrush(QBrush(barsColor));

    m_customPlot->replot();
  }
  QWidget::changeEvent(event);
}

void
GradientEditor::onPlotMousePress(QMouseEvent* event)
{
  // in custom mode, any click is either ON a point or creating a new point?
  bool isCustomMode = (m_currentEditMode == GradientEditMode::CUSTOM);
  bool isMinMaxMode = (m_currentEditMode == GradientEditMode::MINMAX);
  bool isWindowLevelMode = (m_currentEditMode == GradientEditMode::WINDOW_LEVEL);
  bool isPercentileMode = (m_currentEditMode == GradientEditMode::PERCENTILE);
  bool isInteractiveMode = isCustomMode || isMinMaxMode || isWindowLevelMode || isPercentileMode;

  if (!isInteractiveMode) {
    return;
  }

  // let's look to see if a data point was clicked.

  int indexOfDataPoint = -1;
  double dist = 1E+9;

  auto graph = m_customPlot->graph(0);
  for (int n = 0; n < (graph->data()->size()); n++) {
    // get xy of each data pt in pixels. compare with the pick radius.
    // first hit wins.
    double x = (graph->data()->begin() + n)->key;
    double y = (graph->data()->begin() + n)->value;
    double px = m_customPlot->xAxis->coordToPixel(x);
    double py = m_customPlot->yAxis->coordToPixel(y);
    double dx = (px - (double)event->pos().x());
    double dy = (py - (double)event->pos().y());
    dist = sqrt(dx * dx + dy * dy);
    if (dist < SCATTER_PICK_RADIUS) {
      indexOfDataPoint = n;
      // remember dist!
      break;
    }
  }

  if (event->button() == Qt::LeftButton) {

    // if we didn't click on a point, then we could add a point (only in custom mode):
    if (indexOfDataPoint == -1 && isCustomMode) {
      // this checks to see if user clicked along the line anywhere close.
      QCPGraph* plottable = m_customPlot->plottableAt<QCPGraph>(event->pos(), true, &indexOfDataPoint);
      if (plottable != nullptr && indexOfDataPoint > -1) {
        // create a new point at x, y
        double x = m_customPlot->xAxis->pixelToCoord(event->pos().x());
        double y = m_customPlot->yAxis->pixelToCoord(event->pos().y());
        // find first point above x to know the index?
        for (int n = 0; n < (graph->data()->size()); n++) {
          // get xy of each data pt in pixels. compare with scattersize.
          // first hit wins.
          double xn = (graph->data()->begin() + n)->key;
          if (x < xn) {
            // the index of x will be n.
            indexOfDataPoint = n;
            break;
          }
        }
        m_locks.insert(indexOfDataPoint, 0);
        graph->addData(x, y);
        graph->data()->sort();
        m_customPlot->replot();
      }
    }

    if (indexOfDataPoint > -1) {
      // In MINMAX, Window/Level, and Percentile modes, only allow dragging of second and third points (threshold
      // points)
      if (isMinMaxMode || isWindowLevelMode || isPercentileMode) {
        int dataSize = graph->data()->size();
        if (indexOfDataPoint != 1 && indexOfDataPoint != 2) {
          // Not the second or third point (threshold points), don't allow dragging
          return;
        }
      }

      m_isDraggingPoint = true;
      m_currentPointIndex = indexOfDataPoint;
      // turn off axis dragging while we are dragging a point
      m_customPlot->axisRect()->setRangeDrag((Qt::Orientations)0);
      // swallow this event so it doesn't propagate to the plot
      event->accept();
    }
  } else if (event->button() == Qt::RightButton) {
    // Only allow point deletion in custom mode
    if (indexOfDataPoint >= 0 && isCustomMode) {
      if (m_locks[indexOfDataPoint] == 0) {
        m_locks.remove(indexOfDataPoint);
        // remove data pt from plot
        graph->data()->remove((graph->data()->begin() + indexOfDataPoint)->key);

        // update the stuff because we did something
        emit gradientStopsChanged(this->buildStopsFromPlot());
        m_customPlot->replot();
        event->accept();
      }
    }
  }
}
void
GradientEditor::onPlotMouseMove(QMouseEvent* event)
{
  bool isCustomMode = (m_currentEditMode == GradientEditMode::CUSTOM);
  bool isMinMaxMode = (m_currentEditMode == GradientEditMode::MINMAX);
  bool isWindowLevelMode = (m_currentEditMode == GradientEditMode::WINDOW_LEVEL);
  bool isPercentileMode = (m_currentEditMode == GradientEditMode::PERCENTILE);
  bool isInteractiveMode = isCustomMode || isMinMaxMode || isWindowLevelMode || isPercentileMode;

  if (!isInteractiveMode) {
    return;
  }

  if (m_isDraggingPoint && m_currentPointIndex >= 0) {
    if (event->buttons() & Qt::LeftButton) {
      auto graph = m_customPlot->graph(0);
      double evx = m_customPlot->xAxis->pixelToCoord(event->pos().x());
      double evy = m_customPlot->yAxis->pixelToCoord(event->pos().y());

      // Handle threshold-based modes (MINMAX, Window/Level, Percentile) differently
      if (isMinMaxMode || isWindowLevelMode || isPercentileMode) {
        // In threshold-based modes, keep Y value constant and only allow horizontal movement
        double originalY = (graph->data()->begin() + m_currentPointIndex)->value;
        evy = originalY; // Keep Y constant

        static constexpr double OVERLAP_EPSILON = 0.001;
        // Apply additional constraints for min/max threshold points
        if (m_currentPointIndex == 1) {
          // This is the min threshold point - don't let it go past the max threshold point
          if (graph->data()->size() > 2) {
            double maxThresholdX = (graph->data()->begin() + 2)->key;
            evx = std::min(evx, maxThresholdX - OVERLAP_EPSILON); // Small epsilon to prevent overlap
          }
          // Also don't let it go before the first fixed point
          if (graph->data()->size() > 0) {
            double firstPointX = (graph->data()->begin())->key;
            evx = std::max(evx, firstPointX + OVERLAP_EPSILON);
          }
        } else if (m_currentPointIndex == 2) {
          // This is the max threshold point - don't let it go past the min threshold point
          if (graph->data()->size() > 1) {
            double minThresholdX = (graph->data()->begin() + 1)->key;
            evx = std::max(evx, minThresholdX + OVERLAP_EPSILON); // Small epsilon to prevent overlap
          }
          // Also don't let it go past the last fixed point
          if (graph->data()->size() > 3) {
            double lastPointX = (graph->data()->begin() + 3)->key;
            evx = std::min(evx, lastPointX - OVERLAP_EPSILON);
          }
        }
      }

      // see hoverpoints.cpp.
      // this will make sure we don't move past locked edges in the bounding rectangle.
      double px = evx, py = evy;
      bound_point(evx,
                  evy,
                  // we really want clipRect() here?  to capture zoomed region
                  QRectF(m_histogram.getDataMin(), 0.0f, m_histogram.getDataMax() - m_histogram.getDataMin(), 1.0f),
                  m_locks.at(m_currentPointIndex),
                  px,
                  py);

      // if we are dragging a point then move it
      (graph->data()->begin() + m_currentPointIndex)->value = py;
      (graph->data()->begin() + m_currentPointIndex)->key = px;

      // In MINMAX mode, don't sort - keep points in their fixed positions
      if (!isMinMaxMode) {
        // The point may have moved past other points, so sort,
        // and account for current point index possibly changing.
        // TODO should we always sort on every move? Or can we tell if we crossed another point?
        graph->data().data()->sort();
        // find new index of current point
        // find first point above x to know the index?
        int indexOfDataPoint = m_currentPointIndex;
        for (int n = 0; n < (graph->data()->size()); n++) {
          // get xy of each data pt in pixels. compare with scattersize.
          // first hit wins.
          double xn = (graph->data()->begin() + n)->key;
          if (px == xn) {
            // the index of x will be n.
            indexOfDataPoint = n;
            break;
          }
        }
        if (indexOfDataPoint != m_currentPointIndex) {
          m_currentPointIndex = indexOfDataPoint;
        }
      }

      // In threshold-based modes, emit interactivePointsChanged for real-time slider updates
      // Use the second and third points (indices 1 and 2) as the threshold points
      if ((isMinMaxMode || isWindowLevelMode || isPercentileMode) && graph->data()->size() >= 4) {
        double minThresholdX = (graph->data()->begin() + 1)->key;
        double maxThresholdX = (graph->data()->begin() + 2)->key;
        // Pass m_currentPointIndex so the receiver only rewrites the side that was actually dragged.
        emit interactivePointsChanged(minThresholdX, maxThresholdX, m_currentPointIndex);
      }

      // emit( DataChanged() );

      emit gradientStopsChanged(this->buildStopsFromPlot());

      m_customPlot->replot(QCustomPlot::rpQueuedReplot);
    }
  }
}

QGradientStops
GradientEditor::buildStopsFromPlot()
{
  // build up coords from the customplot into the form of gradient stops
  QGradientStops stops;

  auto graph = m_customPlot->graph(0);
  for (int n = 0; n < (graph->data()->size()); n++) {
    auto dataIter = graph->data()->begin() + n;
    double x = dataIter->key;
    // skip duplicates?
    if (n + 1 < graph->data()->size() && x == graph->data()->at(n + 1)->key)
      continue;

    // rescale x to 0-1 range.
    x = (x - m_histogram.getDataMin()) / (m_histogram.getDataMax() - m_histogram.getDataMin());
    double y = dataIter->value;

    QColor color = QColor::fromRgbF(y, y, y, y);
    if (x > 1.0) {
      LOG_ERROR << "control point x greater than 1";
      return stops;
    }

    stops << QGradientStop(x, color);
  }
  return stops;
}

void
GradientEditor::onPlotMouseRelease(QMouseEvent* event)
{
  Q_UNUSED(event);
  if (m_currentEditMode != GradientEditMode::CUSTOM && m_currentEditMode != GradientEditMode::MINMAX &&
      m_currentEditMode != GradientEditMode::WINDOW_LEVEL && m_currentEditMode != GradientEditMode::PERCENTILE) {
    return;
  }
  // if we were dragging a point then stop
  m_isDraggingPoint = false;
  m_currentPointIndex = -1;
  // re-enable axis dragging
  m_customPlot->axisRect()->setRangeDrag(Qt::Horizontal);
}

void
GradientEditor::onPlotMouseDoubleClick(QMouseEvent* event)
{
  // double click should reset zoom
  this->m_customPlot->rescaleAxes();
  this->m_customPlot->replot();
}

void
GradientEditor::onPlotMouseWheel(QWheelEvent* event)
{
  Q_UNUSED(event);
}

inline static bool
x_less_than(const QPointF& p1, const QPointF& p2)
{
  return p1.x() < p2.x();
}

inline static bool
controlpoint_x_less_than(const LutControlPoint& p1, const LutControlPoint& p2)
{
  return p1.first < p2.first;
}

QGradientStops
pointsToGradientStops(QPolygonF points)
{
  QGradientStops stops;
  std::sort(points.begin(), points.end(), x_less_than);

  for (int i = 0; i < points.size(); ++i) {
    qreal x = points.at(i).x();
    if (i + 1 < points.size() && x == points.at(i + 1).x())
      continue;
    float pixelvalue = points.at(i).y();
    // TODO future: let each point have a full RGBA color and use a color picker to assign it via dbl
    // click or some other means

    QColor color = QColor::fromRgbF(pixelvalue, pixelvalue, pixelvalue, pixelvalue);
    if (x > 1) {
      return stops;
    }

    stops << QGradientStop(x, color);
  }
  return stops;
}

void
GradientEditor::set_shade_points(const QPolygonF& points, QCustomPlot* plot, const Histogram& histogram)
{
  if (points.size() < 2) {
    return;
  }

  QGradientStops stops = pointsToGradientStops(points);

  m_locks.clear();
  if (!points.empty()) {
    m_locks.resize(points.size());
    m_locks.fill(0);
  }
  m_locks[0] = GradientEditor::LockToLeft;
  m_locks[points.size() - 1] = GradientEditor::LockToRight;

  QVector<double> x, y;
  for (int i = 0; i < points.size(); ++i) {
    // incoming points x values are in 0-1 range which is normalized to histogram data range
    float dx = histogram.getDataMin() + points.at(i).x() * (histogram.getDataMax() - histogram.getDataMin());
    x << dx;
    y << points.at(i).y();
  }
  plot->graph(0)->setData(x, y);
  plot->replot(QCustomPlot::rpQueuedReplot);
}

void
GradientEditor::setControlPoints(const std::vector<LutControlPoint>& points)
{
  QPolygonF pts_alpha;

  for (auto p : points) {
    pts_alpha << QPointF(p.first, p.second);
  }

  set_shade_points(pts_alpha, m_customPlot, m_histogram);
}

void
GradientEditor::wheelEvent(QWheelEvent* event)
{
  // wheel does nothing here!
  event->ignore();
}

GradientWidget::GradientWidget(const Histogram& histogram, GradientData* dataObject, QWidget* parent)
  : QWidget(parent)
  , m_histogram(histogram)
  , m_gradientData(dataObject)
{
  QVBoxLayout* mainGroupLayout = new QVBoxLayout(this);

  m_editor = new GradientEditor(m_histogram, this);
  m_editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_editor->setMinimumHeight(100);
  QHBoxLayout* editorRowLayout = new QHBoxLayout();
  editorRowLayout->setContentsMargins(0, 0, 0, 0);
  editorRowLayout->setSpacing(4);
  editorRowLayout->addWidget(m_editor, 1);

  QVBoxLayout* editorButtonLayout = new QVBoxLayout();
  editorButtonLayout->setContentsMargins(0, 8, 0, 0);
  editorButtonLayout->setSpacing(2);

  copyButton = new QToolButton(this);
  QIcon copyIcon = QIcon::fromTheme(QIcon::ThemeIcon::EditCopy);
  if (!copyIcon.isNull()) {
    copyButton->setIcon(copyIcon);
  } else {
    copyButton->setText("C");
  }
  copyButton->setToolTip(tr("Copy control points"));
  copyButton->setAutoRaise(true);
  copyButton->setFixedSize(20, 20);

  pasteButton = new QToolButton(this);
  QIcon pasteIcon = QIcon::fromTheme(QIcon::ThemeIcon::EditPaste);
  if (!pasteIcon.isNull()) {
    pasteButton->setIcon(pasteIcon);
  } else {
    pasteButton->setText("P");
  }
  pasteButton->setToolTip(tr("Paste control points"));
  pasteButton->setAutoRaise(true);
  pasteButton->setFixedSize(20, 20);

  yScaleButton = new QToolButton(this);
  QIcon logIcon = QIcon::fromTheme("view-logarithmic");
  if (!logIcon.isNull()) {
    yScaleButton->setIcon(logIcon);
    yScaleButton->setFixedSize(20, 20);
  } else {
    yScaleButton->setText("Log");
    int textWidth = yScaleButton->fontMetrics().horizontalAdvance(yScaleButton->text()) + 4;
    yScaleButton->setFixedSize(std::max(20, textWidth), 20);
  }
  yScaleButton->setToolTip(tr("Toggle log Y scale"));
  yScaleButton->setAutoRaise(true);
  yScaleButton->setCheckable(true);

  editorButtonLayout->addWidget(copyButton);
  editorButtonLayout->addWidget(pasteButton);
  editorButtonLayout->addSpacing(12);
  editorButtonLayout->addWidget(yScaleButton);
  editorButtonLayout->addStretch(1);

  editorRowLayout->addLayout(editorButtonLayout);
  mainGroupLayout->addLayout(editorRowLayout);
  mainGroupLayout->setStretch(0, 3);

  auto* sectionLayout = Controls::createAgaveFormLayout();

  QButtonGroup* btnGroup = new QButtonGroup(this);
  QPushButton* minMaxButton = new QPushButton("Min/Max");
  minMaxButton->setToolTip(tr("Min/Max"));
  minMaxButton->setStatusTip(tr("Choose Min/Max mode"));
  QPushButton* windowLevelButton = new QPushButton("Wnd/Lvl");
  windowLevelButton->setToolTip(tr("Window/Level"));
  windowLevelButton->setStatusTip(tr("Choose Window/Level mode"));
  QPushButton* isoButton = new QPushButton("Iso");
  isoButton->setToolTip(tr("Isovalue"));
  isoButton->setStatusTip(tr("Choose Isovalue mode"));
  QPushButton* pctButton = new QPushButton("Pct");
  pctButton->setToolTip(tr("Histogram Percentiles"));
  pctButton->setStatusTip(tr("Choose Histogram percentiles mode"));
  QPushButton* customButton = new QPushButton("Custom");
  customButton->setToolTip(tr("Custom"));
  customButton->setStatusTip(tr("Choose Custom editing mode"));

  static const int WINDOW_LEVEL_BTNID = 1;
  static const int ISO_BTNID = 2;
  static const int PCT_BTNID = 3;
  static const int CUSTOM_BTNID = 4;
  static const int MINMAX_BTNID = 5;
  static std::map<int, GradientEditMode> btnIdToGradientMode = { { WINDOW_LEVEL_BTNID, GradientEditMode::WINDOW_LEVEL },
                                                                 { ISO_BTNID, GradientEditMode::ISOVALUE },
                                                                 { PCT_BTNID, GradientEditMode::PERCENTILE },
                                                                 { MINMAX_BTNID, GradientEditMode::MINMAX },
                                                                 { CUSTOM_BTNID, GradientEditMode::CUSTOM } };
  static std::map<GradientEditMode, int> gradientModeToBtnId = { { GradientEditMode::WINDOW_LEVEL, WINDOW_LEVEL_BTNID },
                                                                 { GradientEditMode::ISOVALUE, ISO_BTNID },
                                                                 { GradientEditMode::PERCENTILE, PCT_BTNID },
                                                                 { GradientEditMode::MINMAX, MINMAX_BTNID },
                                                                 { GradientEditMode::CUSTOM, CUSTOM_BTNID } };
  static std::map<int, int> btnIdToStackedPage = {
    { WINDOW_LEVEL_BTNID, 1 }, { ISO_BTNID, 2 }, { PCT_BTNID, 3 }, { MINMAX_BTNID, 0 }, { CUSTOM_BTNID, 4 }
  };
  btnGroup->addButton(minMaxButton, MINMAX_BTNID);
  btnGroup->addButton(windowLevelButton, WINDOW_LEVEL_BTNID);
  btnGroup->addButton(isoButton, ISO_BTNID);
  btnGroup->addButton(pctButton, PCT_BTNID);
  btnGroup->addButton(customButton, CUSTOM_BTNID);
  QHBoxLayout* hbox = new QHBoxLayout();
  hbox->setSpacing(0);
  hbox->setContentsMargins(0, 0, 0, 0);

  int initialButtonId = WINDOW_LEVEL_BTNID;
  GradientEditMode m = m_gradientData->m_activeMode;
  initialButtonId = gradientModeToBtnId[m];

  for (auto btn : btnGroup->buttons()) {
    btn->setCheckable(true);
    // set checked state initially.
    int btnid = btnGroup->id(btn);
    if (btnid == initialButtonId) {
      btn->setChecked(true);
    }
    hbox->addWidget(btn);
  }
  mainGroupLayout->addLayout(hbox);

  QWidget* firstPageWidget = new QWidget;
  auto* section0Layout = Controls::createAgaveFormLayout();
  section0Layout->setContentsMargins(0, 0, 0, 0);
  firstPageWidget->setLayout(section0Layout);

  QWidget* secondPageWidget = new QWidget;
  auto* section1Layout = Controls::createAgaveFormLayout();
  section1Layout->setContentsMargins(0, 0, 0, 0);
  secondPageWidget->setLayout(section1Layout);

  QWidget* thirdPageWidget = new QWidget;
  auto* section2Layout = Controls::createAgaveFormLayout();
  section2Layout->setContentsMargins(0, 0, 0, 0);
  thirdPageWidget->setLayout(section2Layout);

  QWidget* fourthPageWidget = new QWidget;
  auto* section3Layout = Controls::createAgaveFormLayout();
  section3Layout->setContentsMargins(0, 0, 0, 0);
  fourthPageWidget->setLayout(section3Layout);

  QWidget* fifthPageWidget = new QWidget;
  auto* section4Layout = Controls::createAgaveFormLayout();
  section4Layout->setContentsMargins(0, 0, 0, 0);
  fifthPageWidget->setLayout(section4Layout);

  QStackedLayout* stackedLayout = new QStackedLayout(mainGroupLayout);
  stackedLayout->addWidget(firstPageWidget);
  stackedLayout->addWidget(secondPageWidget);
  stackedLayout->addWidget(thirdPageWidget);
  stackedLayout->addWidget(fourthPageWidget);
  stackedLayout->addWidget(fifthPageWidget);

  int initialStackedPageIndex = btnIdToStackedPage[initialButtonId];
  stackedLayout->setCurrentIndex(initialStackedPageIndex);
  // if this is not custom mode, then disable the gradient editor
  // m_editor->setEditable(m == GradientEditMode::CUSTOM);
  m_editor->setEditMode(m);

  connect(btnGroup,
          QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
          [this, btnGroup, stackedLayout](QAbstractButton* button) {
            int id = btnGroup->id(button);
            GradientEditMode modeToSet = btnIdToGradientMode[id];
            // if mode is not changing, we are done.
            if (modeToSet == this->m_gradientData->m_activeMode) {
              return;
            }
            this->m_gradientData->m_activeMode = modeToSet;

            stackedLayout->setCurrentIndex(btnIdToStackedPage[id]);

            // if this is not custom mode, then disable the gradient editor
            // m_editor->setEditable(modeToSet == GradientEditMode::CUSTOM);
            m_editor->setEditMode(modeToSet);

            this->forceDataUpdate();
            this->updateCopyPasteButtons();
          });

  minu16Slider = new QIntSlider();
  minu16Slider->setRange(m_histogram.getDataMin(), m_histogram.getDataMax());
  minu16Slider->setSingleStep(1);
  minu16Slider->setValue(m_gradientData->m_minu16);
  Controls::addFormRow(section0Layout,
                       "Min u16",
                       minu16Slider,
                       tr("Set minimum 16-bit intensity value"),
                       tr("Minimum 16-bit intensity value"));
  maxu16Slider = new QIntSlider();
  maxu16Slider->setRange(m_histogram.getDataMin(), m_histogram.getDataMax());
  maxu16Slider->setSingleStep(1);
  maxu16Slider->setValue(m_gradientData->m_maxu16);
  Controls::addFormRow(section0Layout,
                       "Max u16",
                       maxu16Slider,
                       tr("Set maximum 16-bit intensity value"),
                       tr("Maximum 16-bit intensity value"));
  connect(minu16Slider, &QIntSlider::valueChanged, [this](int i) {
    this->m_gradientData->m_minu16 = i;
    this->onSetMinMax(i, this->m_gradientData->m_maxu16);
  });
  connect(maxu16Slider, &QIntSlider::valueChanged, [this](int i) {
    this->m_gradientData->m_maxu16 = i;
    this->onSetMinMax(this->m_gradientData->m_minu16, i);
  });

  windowSlider = new QNumericSlider();
  windowSlider->setRange(0.0, 1.0);
  windowSlider->setSingleStep(0.01);
  windowSlider->setDecimals(3);
  windowSlider->setValue(m_gradientData->m_window);
  Controls::addFormRow(section1Layout, "Window", windowSlider, tr("Set size of range of intensities"), tr("Window"));
  levelSlider = new QNumericSlider();
  levelSlider->setRange(0.0, 1.0);
  levelSlider->setSingleStep(0.01);
  levelSlider->setDecimals(3);
  levelSlider->setValue(m_gradientData->m_level);
  Controls::addFormRow(section1Layout, "Level", levelSlider, tr("Set level of mid intensity"), tr("Level"));
  connect(windowSlider, &QNumericSlider::valueChanged, [this](double d) {
    this->m_gradientData->m_window = d;
    this->onSetWindowLevel(d, levelSlider->value());
  });
  connect(levelSlider, &QNumericSlider::valueChanged, [this](double d) {
    this->m_gradientData->m_level = d;
    this->onSetWindowLevel(windowSlider->value(), d);
  });

  isovalueSlider = new QNumericSlider();
  isovalueSlider->setRange(0.0, 1.0);
  isovalueSlider->setSingleStep(0.01);
  isovalueSlider->setDecimals(3);
  isovalueSlider->setValue(m_gradientData->m_isovalue);
  Controls::addFormRow(section2Layout, "Isovalue", isovalueSlider, tr("Set Isovalue"), tr("Isovalue"));
  isorangeSlider = new QNumericSlider();
  isorangeSlider->setRange(0.0, 1.0);
  isorangeSlider->setSingleStep(0.01);
  isorangeSlider->setDecimals(3);
  isorangeSlider->setValue(m_gradientData->m_isorange);
  Controls::addFormRow(
    section2Layout, "Iso-range", isorangeSlider, tr("Set range above and below isovalue"), tr("Isovalue range"));
  connect(isovalueSlider, &QNumericSlider::valueChanged, [this](double d) {
    this->m_gradientData->m_isovalue = d;
    this->onSetIsovalue(d, isorangeSlider->value());
  });
  connect(isorangeSlider, &QNumericSlider::valueChanged, [this](double d) {
    this->m_gradientData->m_isorange = d;
    this->onSetIsovalue(isovalueSlider->value(), d);
  });

  pctLowSlider = new QNumericSlider();
  pctLowSlider->setRange(0.0, 1.0);
  pctLowSlider->setSingleStep(0.01);
  pctLowSlider->setDecimals(4);
  pctLowSlider->setValue(m_gradientData->m_pctLow);
  Controls::addFormRow(section3Layout, "Pct Min", pctLowSlider, tr("Set bottom percentile"), tr("Low percentile"));
  pctHighSlider = new QNumericSlider();
  pctHighSlider->setRange(0.0, 1.0);
  pctHighSlider->setSingleStep(0.01);
  pctHighSlider->setDecimals(4);
  pctHighSlider->setValue(m_gradientData->m_pctHigh);
  Controls::addFormRow(section3Layout, "Pct Max", pctHighSlider, tr("Set top percentile"), tr("High percentile"));
  connect(pctLowSlider, &QNumericSlider::valueChanged, [this](double d) {
    this->m_gradientData->m_pctLow = d;
    this->onSetHistogramPercentiles(d, pctHighSlider->value());
  });
  connect(pctHighSlider, &QNumericSlider::valueChanged, [this](double d) {
    this->m_gradientData->m_pctHigh = d;
    this->onSetHistogramPercentiles(pctLowSlider->value(), d);
  });

  mainGroupLayout->addLayout(sectionLayout);
  mainGroupLayout->addStretch(1);

  connect(m_editor, &GradientEditor::gradientStopsChanged, this, &GradientWidget::onGradientStopsChanged);
  connect(m_editor, &GradientEditor::interactivePointsChanged, this, &GradientWidget::onInteractivePointsChanged);
  connect(copyButton, &QToolButton::clicked, this, &GradientWidget::onCopyControlPoints);
  connect(pasteButton, &QToolButton::clicked, this, &GradientWidget::onPasteControlPoints);
  connect(yScaleButton, &QToolButton::toggled, this, &GradientWidget::onToggleYAxisScale);

  forceDataUpdate();
  updateCopyPasteButtons();
}

void
GradientWidget::setHistogram(const Histogram& histogram)
{
  // m_gradientData is already up-to-date with respect to the new histogram.
  //
  // UI refresh: update the editor's histogram bars, refit the
  // MINMAX slider range to the new data range, and re-sync all sliders to
  // m_gradientData. Then forceDataUpdate() redraws the editor curve and
  // emits a fresh LUT against the new histogram (necessary for PERCENTILE
  // mode to adapt to the new distribution).
  m_histogram = histogram;
  m_editor->setHistogram(histogram);

  const int dataMin = m_histogram.getDataMin();
  const int dataMax = m_histogram.getDataMax();

  {
    QSignalBlocker b1(minu16Slider);
    QSignalBlocker b2(maxu16Slider);
    QSignalBlocker b3(windowSlider);
    QSignalBlocker b4(levelSlider);
    QSignalBlocker b5(isovalueSlider);
    QSignalBlocker b6(isorangeSlider);
    QSignalBlocker b7(pctLowSlider);
    QSignalBlocker b8(pctHighSlider);

    minu16Slider->setRange(dataMin, dataMax);
    minu16Slider->setValue(m_gradientData->m_minu16);
    maxu16Slider->setRange(dataMin, dataMax);
    maxu16Slider->setValue(m_gradientData->m_maxu16);

    windowSlider->setValue(m_gradientData->m_window);
    levelSlider->setValue(m_gradientData->m_level);
    isovalueSlider->setValue(m_gradientData->m_isovalue);
    isorangeSlider->setValue(m_gradientData->m_isorange);
    pctLowSlider->setValue(m_gradientData->m_pctLow);
    pctHighSlider->setValue(m_gradientData->m_pctHigh);
  }

  forceDataUpdate();
}

void
GradientWidget::forceDataUpdate()
{
  GradientEditMode mode = this->m_gradientData->m_activeMode;

  switch (mode) {
    case GradientEditMode::WINDOW_LEVEL:
      this->onSetWindowLevel(this->m_gradientData->m_window, this->m_gradientData->m_level);
      break;
    case GradientEditMode::ISOVALUE:
      this->onSetIsovalue(this->m_gradientData->m_isovalue, this->m_gradientData->m_isorange);
      break;
    case GradientEditMode::PERCENTILE:
      this->onSetHistogramPercentiles(this->m_gradientData->m_pctLow, this->m_gradientData->m_pctHigh);
      break;
    case GradientEditMode::MINMAX:
      this->onSetMinMax(this->m_gradientData->m_minu16, this->m_gradientData->m_maxu16);
      break;
    case GradientEditMode::CUSTOM: {
      m_editor->setControlPoints(this->m_gradientData->m_customControlPoints);
      QGradientStops stops = vectorToGradientStops(this->m_gradientData->m_customControlPoints);
      emit gradientStopsChanged(stops);
    } break;
    default:
      LOG_ERROR << "Bad gradient editor mode";
      break;
  }
}

void
GradientWidget::updateCopyPasteButtons()
{
  if (!copyButton || !pasteButton) {
    return;
  }

  GradientEditMode mode = m_gradientData->m_activeMode;
  bool allowCopy = mode != GradientEditMode::CUSTOM;
  bool allowPaste = m_hasMinMaxClipboard;

  copyButton->setEnabled(allowCopy);
  pasteButton->setEnabled(allowPaste);
}

void
GradientWidget::onToggleYAxisScale(bool enabled)
{
  if (!m_editor) {
    return;
  }

  m_editor->setYAxisLogScale(enabled);
}

void
GradientWidget::onCopyControlPoints()
{
  GradientEditMode mode = m_gradientData->m_activeMode;
  if (mode == GradientEditMode::CUSTOM) {
    return;
  }

  std::pair<float, float> minMax;
  bool hasMinMax = m_gradientData->getMinMax(m_histogram, &minMax);
  if (!hasMinMax) {
    return;
  }

  m_clipboardMinIntensity = minMax.first;
  m_clipboardMaxIntensity = minMax.second;
  m_hasMinMaxClipboard = (m_clipboardMinIntensity < m_clipboardMaxIntensity);
  updateCopyPasteButtons();
}

void
GradientWidget::onPasteControlPoints()
{
  if (!m_hasMinMaxClipboard) {
    return;
  }

  float dataMin = static_cast<float>(m_histogram.getDataMin());
  float dataMax = static_cast<float>(m_histogram.getDataMax());
  float dataRange = dataMax - dataMin;
  if (dataRange <= 0.0f) {
    return;
  }

  float minIntensity = std::max(m_clipboardMinIntensity, dataMin);
  float maxIntensity = std::min(m_clipboardMaxIntensity, dataMax);
  if (minIntensity >= maxIntensity) {
    return;
  }

  GradientEditMode mode = m_gradientData->m_activeMode;
  if (mode == GradientEditMode::CUSTOM) {
    float relativeMin = (minIntensity - dataMin) / dataRange;
    float relativeMax = (maxIntensity - dataMin) / dataRange;
    relativeMin = std::max(relativeMin, 0.0f);
    relativeMax = std::min(relativeMax, 1.0f);

    std::vector<LutControlPoint> points;
    points.push_back({ 0.0f, 0.0f });
    points.push_back({ relativeMin, 0.0f });
    points.push_back({ relativeMax, 1.0f });
    points.push_back({ 1.0f, 1.0f });

    m_gradientData->m_customControlPoints = points;
    m_editor->setControlPoints(points);
    emit gradientStopsChanged(vectorToGradientStops(points));
    updateCopyPasteButtons();
    return;
  }

  if (mode == GradientEditMode::MINMAX) {
    uint16_t minu16 = static_cast<uint16_t>(std::clamp(minIntensity, 0.0f, 65535.0f));
    uint16_t maxu16 = static_cast<uint16_t>(std::clamp(maxIntensity, 0.0f, 65535.0f));
    m_gradientData->m_minu16 = minu16;
    m_gradientData->m_maxu16 = maxu16;
    onSetMinMax(minu16, maxu16);

    if (minu16Slider) {
      minu16Slider->blockSignals(true);
      minu16Slider->setValue(minu16);
      minu16Slider->blockSignals(false);
    }
    if (maxu16Slider) {
      maxu16Slider->blockSignals(true);
      maxu16Slider->setValue(maxu16);
      maxu16Slider->blockSignals(false);
    }
  } else if (mode == GradientEditMode::WINDOW_LEVEL) {
    float relativeMin = (minIntensity - dataMin) / dataRange;
    float relativeMax = (maxIntensity - dataMin) / dataRange;
    relativeMin = std::max(relativeMin, 0.0f);
    relativeMax = std::min(relativeMax, 1.0f);
    float window = relativeMax - relativeMin;
    float level = (relativeMax + relativeMin) * 0.5f;
    m_gradientData->m_window = window;
    m_gradientData->m_level = level;
    onSetWindowLevel(window, level);

    if (windowSlider) {
      windowSlider->blockSignals(true);
      windowSlider->setValue(window);
      windowSlider->blockSignals(false);
    }
    if (levelSlider) {
      levelSlider->blockSignals(true);
      levelSlider->setValue(level);
      levelSlider->blockSignals(false);
    }
  } else if (mode == GradientEditMode::PERCENTILE) {
    uint16_t minu16 = static_cast<uint16_t>(std::clamp(minIntensity, 0.0f, 65535.0f));
    uint16_t maxu16 = static_cast<uint16_t>(std::clamp(maxIntensity, 0.0f, 65535.0f));
    float pctLow = 0.0f;
    float pctHigh = 1.0f;
    m_histogram.computePercentile(minu16, pctLow);
    m_histogram.computePercentile(maxu16, pctHigh);
    m_gradientData->m_pctLow = pctLow;
    m_gradientData->m_pctHigh = pctHigh;
    onSetHistogramPercentiles(pctLow, pctHigh);

    if (pctLowSlider) {
      pctLowSlider->blockSignals(true);
      pctLowSlider->setValue(pctLow);
      pctLowSlider->blockSignals(false);
    }
    if (pctHighSlider) {
      pctHighSlider->blockSignals(true);
      pctHighSlider->setValue(pctHigh);
      pctHighSlider->blockSignals(false);
    }
  } else if (mode == GradientEditMode::ISOVALUE) {
    float relativeMin = (minIntensity - dataMin) / dataRange;
    float relativeMax = (maxIntensity - dataMin) / dataRange;
    relativeMin = std::max(relativeMin, 0.0f);
    relativeMax = std::min(relativeMax, 1.0f);
    float isovalue = (relativeMax + relativeMin) * 0.5f;
    float isorange = relativeMax - relativeMin;
    m_gradientData->m_isovalue = isovalue;
    m_gradientData->m_isorange = isorange;
    onSetIsovalue(isovalue, isorange);

    if (isovalueSlider) {
      isovalueSlider->blockSignals(true);
      isovalueSlider->setValue(isovalue);
      isovalueSlider->blockSignals(false);
    }
    if (isorangeSlider) {
      isorangeSlider->blockSignals(true);
      isorangeSlider->setValue(isorange);
      isorangeSlider->blockSignals(false);
    }
  }

  updateCopyPasteButtons();
}

void
GradientWidget::onGradientStopsChanged(const QGradientStops& stops)
{
  // update the data stored in m_gradientData
  // depending on our mode:
  if (m_gradientData->m_activeMode == GradientEditMode::CUSTOM) {
    m_gradientData->m_customControlPoints.clear();
    for (int i = 0; i < stops.size(); ++i) {
      m_gradientData->m_customControlPoints.push_back(LutControlPoint(stops.at(i).first, stops.at(i).second.alphaF()));
    }
    std::sort(m_gradientData->m_customControlPoints.begin(),
              m_gradientData->m_customControlPoints.end(),
              controlpoint_x_less_than);
    emit gradientStopsChanged(stops);
  } else if (m_gradientData->m_activeMode == GradientEditMode::WINDOW_LEVEL ||
             m_gradientData->m_activeMode == GradientEditMode::PERCENTILE ||
             m_gradientData->m_activeMode == GradientEditMode::MINMAX) {
    // Threshold modes: do NOT update m_gradientData or sliders here. That is done by
    // onInteractivePointsChanged with knowledge of which handle was dragged, so we don't
    // corrupt the un-dragged side via a lossy float / percentile round-trip and don't
    // re-trigger set_shade_points mid-drag (which would visually move the un-dragged
    // handle). Just forward the stops so the renderer's LUT updates live during the drag.
    emit gradientStopsChanged(stops);
  } else if (m_gradientData->m_activeMode == GradientEditMode::ISOVALUE) {
    // extract isovalue and range from the stops
    std::vector<LutControlPoint> points = gradientStopsToVector(const_cast<QGradientStops&>(stops));
    if (points.size() < 2) {
      return;
    }
    std::sort(points.begin(), points.end(), controlpoint_x_less_than);
    float low = points[0].first;
    float high = points[1].first;
    float isovalue = (high + low) * 0.5f;
    float isorange = high - low;
    m_gradientData->m_isovalue = isovalue;
    m_gradientData->m_isorange = isorange;

    // update the sliders to match:
    isovalueSlider->setValue(isovalue);
    isorangeSlider->setValue(isorange);
  }
}

void
GradientWidget::onSetHistogramPercentiles(float pctLow, float pctHigh)
{
  float window, level;
  m_histogram.computeWindowLevelFromPercentiles(pctLow, pctHigh, window, level);
  this->onSetWindowLevel(window, level);
}

void
GradientWidget::onSetWindowLevel(float window, float level)
{
  std::vector<LutControlPoint> points;
  static const float epsilon = 0.000001f;
  window = std::max(window, epsilon);
  float lowEnd = level - window * 0.5f;
  float highEnd = level + window * 0.5f;
  if (lowEnd <= 0.0f) {
    float val = -lowEnd / (highEnd - lowEnd);
    points.push_back({ 0.0f, val });
  } else {
    points.push_back({ 0.0f, 0.0f });
    points.push_back({ lowEnd, 0.0f });
  }
  if (highEnd >= 1.0f) {
    float val = (1.0f - lowEnd) / (highEnd - lowEnd);
    points.push_back({ 1.0f, val });
  } else {
    points.push_back({ highEnd, 1.0f });
    points.push_back({ 1.0f, 1.0f });
  }
  m_editor->setControlPoints(points);
  emit gradientStopsChanged(vectorToGradientStops(points));
}

void
GradientWidget::onSetMinMax(uint16_t minu16, uint16_t maxu16)
{
  // these need to be relative to the data range of the channel, not absolute!
  float relativeMin = normalizeInt(minu16, m_histogram.getDataMin(), m_histogram.getDataMax());
  float relativeMax = normalizeInt(maxu16, m_histogram.getDataMin(), m_histogram.getDataMax());
  relativeMin = std::max(relativeMin, 0.0f);
  relativeMax = std::min(relativeMax, 1.0f);
  if (relativeMin >= relativeMax) {
    LOG_ERROR << "Min value is greater than or equal to max value: " << minu16 << " >= " << maxu16
              << ", datarange=" << m_histogram.dataRange();
    return;
  }
  float window = relativeMax - relativeMin;
  float level = (relativeMax + relativeMin) / 2.0f;
  this->onSetWindowLevel(window, level);
}

void
GradientWidget::onSetIsovalue(float isovalue, float width)
{
  std::vector<LutControlPoint> points;
  float lowEnd = isovalue - width * 0.5f;
  float highEnd = isovalue + width * 0.5f;
  static const float epsilon = 0.00001f;
  points.push_back({ std::min(0.0f, lowEnd - 2.0f * epsilon), 0.0f });
  points.push_back({ lowEnd - epsilon, 0.0f });
  points.push_back({ lowEnd + epsilon, 1.0f });
  points.push_back({ highEnd - epsilon, 1.0f });
  points.push_back({ highEnd + epsilon, 0.0f });
  points.push_back({ std::max(highEnd + 2.0f * epsilon, 1.0f), 0.0f });
  m_editor->setControlPoints(points);
  emit gradientStopsChanged(vectorToGradientStops(points));
}

void
GradientWidget::onInteractivePointsChanged(float minIntensity, float maxIntensity, int draggedIndex)
{
  // Only update the side that was actually dragged.
  // This is to avoid doing unnecessary updating of values that shouldn't be changing anyway.
  // The math to update the values could actually cause rounding errors which cause drift.

  // draggedIndex: 1 = low/min threshold, 2 = high/max threshold.
  const bool draggingLow = (draggedIndex == 1);
  const bool draggingHigh = (draggedIndex == 2);
  if (!draggingLow && !draggingHigh) {
    return;
  }

  const float dataMin = (float)m_histogram.getDataMin();
  const float dataMax = (float)m_histogram.getDataMax();

  if (m_gradientData->m_activeMode == GradientEditMode::MINMAX) {
    // clamp the dragged value to the data range
    float clamped = std::max(dataMin, std::min(draggingLow ? minIntensity : maxIntensity, dataMax));
    uint16_t value = static_cast<uint16_t>(std::round(clamped));
    // update the dragged side's value in m_gradientData.
    if (draggingLow) {
      value = std::min(value, m_gradientData->m_maxu16);
      m_gradientData->m_minu16 = value;
    } else {
      value = std::max(value, m_gradientData->m_minu16);
      m_gradientData->m_maxu16 = value;
    }
    // pick which slider to update based on which side is being dragged
    auto slider = draggingLow ? minu16Slider : maxu16Slider;
    if (slider) {
      slider->blockSignals(true);
      slider->setValue(value);
      slider->blockSignals(false);
    }
  } else if (m_gradientData->m_activeMode == GradientEditMode::WINDOW_LEVEL) {
    // window/level are coupled: derive the un-dragged endpoint from the stored window/level
    // so it does NOT pick up float round-trip drift from the editor.
    const float storedWindow = m_gradientData->m_window;
    const float storedLevel = m_gradientData->m_level;

    float relMin = storedLevel - 0.5f * storedWindow;
    float relMax = storedLevel + 0.5f * storedWindow;

    const float range = std::max(1.0f, dataMax - dataMin);
    if (draggingLow) {
      relMin = (std::max(dataMin, std::min(minIntensity, dataMax)) - dataMin) / range;
    } else {
      relMax = (std::max(dataMin, std::min(maxIntensity, dataMax)) - dataMin) / range;
    }
    if (relMax < relMin) {
      std::swap(relMin, relMax);
    }
    const float window = relMax - relMin;
    const float level = 0.5f * (relMax + relMin);
    m_gradientData->m_window = window;
    m_gradientData->m_level = level;
    if (windowSlider) {
      windowSlider->blockSignals(true);
      windowSlider->setValue(window);
      windowSlider->blockSignals(false);
    }
    if (levelSlider) {
      levelSlider->blockSignals(true);
      levelSlider->setValue(level);
      levelSlider->blockSignals(false);
    }
  } else if (m_gradientData->m_activeMode == GradientEditMode::PERCENTILE) {
    if (m_histogram.getPixelCount() == 0) {
      return;
    }

    float clamped = std::max(dataMin, std::min(draggingLow ? minIntensity : maxIntensity, dataMax));
    uint16_t value = static_cast<uint16_t>(std::round(clamped));
    float pct = draggingLow ? 0.0f : 1.0f;
    if (value <= m_histogram.getDataMin()) {
      pct = 0.0f;
    } else if (value >= m_histogram.getDataMax()) {
      pct = 1.0f;
    } else {
      m_histogram.computePercentile(value, pct);
    }
    if (draggingLow) {
      m_gradientData->m_pctLow = pct;
    } else {
      m_gradientData->m_pctHigh = pct;
    }
    auto slider = draggingLow ? pctLowSlider : pctHighSlider;
    slider->blockSignals(true);
    slider->setValue(pct);
    slider->blockSignals(false);
  }
}
