#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGradient>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyledItemDelegate>

class DualIconButton : public QPushButton
{
  Q_OBJECT
  Q_PROPERTY(int state READ getState)

private:
  QString tooltip1;
  QString tooltip2;
  QString statustip1;
  QString statustip2;
  int state{ 0 };

public:
  DualIconButton(const QString& tooltip1,
                 const QString& statustip1,
                 const QString& tooltip2,
                 const QString& statustip2,
                 QWidget* parent = nullptr)
    : tooltip1(tooltip1)
    , tooltip2(tooltip2)
    , statustip1(statustip1)
    , statustip2(statustip2)
    , QPushButton(QIcon(), "", parent)
  {
    setToolTip(tooltip1);
    setStatusTip(statustip1);
    connect(this, &QPushButton::clicked, this, &DualIconButton::toggleIcon);
  }

  void toggleIcon() { setState(1 - state); }

  int getState() const { return state; }
  void setState(int s)
  {
    state = s;
    if (state == 1) {
      setToolTip(tooltip2);
      setStatusTip(statustip2);
    } else {
      setToolTip(tooltip1);
      setStatusTip(statustip1);
    }
    style()->polish(this);
  }
};

class QColorPushButton : public QPushButton
{
  Q_OBJECT

public:
  QColorPushButton(QWidget* pParent = nullptr);

  void paintEvent(QPaintEvent* pPaintEvent) override;
  void mousePressEvent(QMouseEvent* pEvent) override;

  int GetMargin() const;
  void SetMargin(const int& Margin);
  int GetRadius() const;
  void SetRadius(const int& Radius);
  QColor GetColor() const;
  void SetColor(const QColor& Color, bool BlockSignals = false);

  // Optional colormap gradient drawn behind the diffuse color. When set, the
  // swatch is rendered as the gradient multiplied by the diffuse color. Pass
  // an empty stop list to disable the gradient and render a solid swatch.
  void SetGradientStops(const QGradientStops& stops);

private slots:
  void OnCurrentColorChanged(const QColor& Color);

signals:
  void currentColorChanged(const QColor&);

private:
  int m_Margin{ 4 };
  int m_Radius{ 4 };
  QColor m_Color;
  QGradientStops m_GradientStops;
};

class QColorSelector : public QFrame
{
  Q_OBJECT

public:
  QColorSelector(QWidget* pParent = nullptr);

  //	virtual QSize sizeHint() const;

  QColor GetColor() const;
  void SetColor(const QColor& Color, bool BlockSignals = false);

private slots:
  void OnCurrentColorChanged(const QColor& Color);

signals:
  void currentColorChanged(const QColor&);

private:
  QGridLayout m_MainLayout;
  QColorPushButton m_ColorButton;
  QComboBox m_ColorCombo;
};

class QDoubleSlider : public QSlider
{
  Q_OBJECT

public:
  QDoubleSlider(QWidget* pParent = nullptr);

  void setRange(double Min, double Max);
  void setMinimum(double Min);
  double dminimum() const;
  void setMaximum(double Max);
  double dmaximum() const;
  double value() const;

  double sliderPositionToValue(int pos) const;

public slots:
  void setValue(int value);
  void setValue(double Value, bool BlockSignals = false);

private slots:

signals:
  void valueChanged(double Value);
  void rangeChanged(double Min, double Max);

private:
  double m_Multiplier{ 10000.0 };

protected:
  void wheelEvent(QWheelEvent* pEvent) override;
};

class QDoubleSpinner : public QDoubleSpinBox
{
  Q_OBJECT

public:
  QDoubleSpinner(QWidget* pParent = nullptr);

  void setValue(double Value, bool BlockSignals = false);

protected:
  void wheelEvent(QWheelEvent* pEvent) override;
};

class QNumericSlider : public QWidget
{
  Q_OBJECT
public:
  QNumericSlider(QWidget* pParent = nullptr);

  QSize sizeHint() const override;

  double value() const;
  void setValue(double value, bool BlockSignals = false);
  void setRange(double rmin, double rmax);
  void setSingleStep(double val);
  void setDecimals(int decimals);
  void setSuffix(const QString&);

  void setTracking(bool enable);

private slots:
  void OnValueChanged(double value);

signals:
  void valueChanged(double value);

private:
  QHBoxLayout m_layout;
  QDoubleSpinner m_spinner;
  QDoubleSlider m_slider;
};

class MySpinBoxWithEnter : public QSpinBox
{
  Q_OBJECT
public:
  void keyPressEvent(QKeyEvent* event) override
  {
    if (event->key() == Qt::Key_Return) {
      // remove focus
      clearFocus();
      // editingFinished();
    } else {
      QSpinBox::keyPressEvent(event);
    }
  }
};

class QIntSlider : public QWidget
{
  Q_OBJECT
public:
  QIntSlider(QWidget* pParent = nullptr);

  int value() const;
  int maximum() const;
  void setValue(int value, bool BlockSignals = false);
  void setRange(int rmin, int rmax);
  void setSingleStep(int val);
  void setSuffix(const QString&);

  void setTickPosition(QSlider::TickPosition position);
  void setTickInterval(int ti);

  void setTracking(bool enable);
  void setSpinnerKeyboardTracking(bool tracking);

private slots:
  void OnValueChanged(int value);

signals:
  void valueChanged(int value);

protected:
  // Access to the embedded slider for subclasses that need its geometry -- e.g.
  // to align an overlay with the groove. Kept protected so the public API of
  // QIntSlider stays a closed composite.
  QSlider& sliderWidget() { return m_slider; }
  const QSlider& sliderWidget() const { return m_slider; }

private:
  QGridLayout m_layout;
  MySpinBoxWithEnter m_spinner;
  QSlider m_slider;
};

class AgaveFormLayout : public QGridLayout
{
  Q_OBJECT
public:
  AgaveFormLayout(QWidget* parent = nullptr)
    : QGridLayout(parent)
  {
  }
  void addRow(const QString& label, QWidget* widget);
  void addRow(const QString& label, QLayout* layout);
  void addRow(QWidget* section);
};

class Controls
{
public:
  static QFormLayout* createFormLayout(QWidget* parent = nullptr);
  static void initFormLayout(QFormLayout& layout);

  static AgaveFormLayout* createAgaveFormLayout(QWidget* parent = nullptr);

  // Add a row to a QFormLayout and apply the given toolTip/statusTip to BOTH the
  // field widget and the auto-created label. Empty strings are skipped so existing
  // tooltips/statusTips on the widget are not erased.
  static void addFormRow(QFormLayout* layout,
                         const QString& labelText,
                         QWidget* field,
                         const QString& toolTip,
                         const QString& statusTip = QString());
  static void addFormRow(QFormLayout* layout,
                         const QString& labelText,
                         QLayout* fieldLayout,
                         const QString& toolTip,
                         const QString& statusTip = QString());

  // Same as above but for AgaveFormLayout. The label is located at column 0 of
  // the just-added row.
  static void addFormRow(AgaveFormLayout* layout,
                         const QString& labelText,
                         QWidget* field,
                         const QString& toolTip,
                         const QString& statusTip = QString());
  static void addFormRow(AgaveFormLayout* layout,
                         const QString& labelText,
                         QLayout* fieldLayout,
                         const QString& toolTip,
                         const QString& statusTip = QString());
};

/**
 * @brief QComboBox with support of checkboxes
 * http://stackoverflow.com/questions/8422760/combobox-of-checkboxes
 */
class QCheckList : public QComboBox
{
  Q_OBJECT

public:
  /**
   * @brief Additional value to Qt::CheckState when some checkboxes are Qt::PartiallyChecked
   */
  static const int StateUnknown = 3;

private:
  QStandardItemModel* m_model;
  QString m_titleText;

signals:
  void globalCheckStateChanged(int);

public:
  QCheckList(QWidget* _parent = nullptr)
    : QComboBox(_parent)
  {
    m_model = new QStandardItemModel();
    setModel(m_model);

    setEditable(true);
    lineEdit()->setReadOnly(true);
    lineEdit()->installEventFilter(this);
    setItemDelegate(new QCheckListStyledItemDelegate(this));

    connect(lineEdit(), &QLineEdit::selectionChanged, lineEdit(), &QLineEdit::deselect);
    connect((QListView*)view(), SIGNAL(pressed(QModelIndex)), this, SLOT(on_itemPressed(QModelIndex)));
    connect(m_model, SIGNAL(dataChanged(QModelIndex, QModelIndex, QVector<int>)), this, SLOT(on_modelDataChanged()));
  }

  QCheckList(const QString& title, QWidget* _parent = nullptr)
    : QCheckList(_parent)
  {
    m_titleText = title;
  }

  ~QCheckList() override { delete m_model; }

  void setTitleText(const QString& text)
  {
    m_titleText = text;
    updateText();
  }

  std::vector<uint32_t> getCheckedIndices() const
  {
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < m_model->rowCount(); ++i) {
      if (m_model->item(static_cast<int>(i))->checkState() == Qt::Checked) {
        indices.push_back(i);
      }
    }
    return indices;
  }

  /**
   * @brief Adds a item to the checklist (setChecklist must have been called)
   * @return the new QStandardItem
   */
  QStandardItem* addCheckItem(const QString& label, const QVariant& data, const Qt::CheckState checkState)
  {
    QStandardItem* item = new QStandardItem(label);
    item->setCheckState(checkState);
    item->setData(data);
    item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);

    m_model->appendRow(item);

    updateText();

    return item;
  }

  /**
   * @brief Computes the global state of the checklist :
   *      - if there is no item: StateUnknown
   *      - if there is at least one item partially checked: StateUnknown
   *      - if all items are checked: Qt::Checked
   *      - if no item is checked: Qt::Unchecked
   *      - else: Qt::PartiallyChecked
   */
  int globalCheckState()
  {
    int nbRows = m_model->rowCount(), nbChecked = 0, nbUnchecked = 0;

    if (nbRows == 0) {
      return StateUnknown;
    }

    for (int i = 0; i < nbRows; i++) {
      if (m_model->item(i)->checkState() == Qt::Checked) {
        nbChecked++;
      } else if (m_model->item(i)->checkState() == Qt::Unchecked) {
        nbUnchecked++;
      } else {
        return StateUnknown;
      }
    }

    if (nbChecked == nbRows) {
      return Qt::Checked;
    } else if (nbUnchecked == nbRows) {
      return Qt::Unchecked;
    } else {
      return Qt::PartiallyChecked;
    }
  }

protected:
  bool eventFilter(QObject* _object, QEvent* _event) override
  {
    if (_object == lineEdit() && _event->type() == QEvent::MouseButtonPress) {
      showPopup();
      return true;
    }

    return false;
  }

private:
  void updateText()
  {
    QString text = m_titleText;
    int numChecked = 0;
    switch (globalCheckState()) {
      case Qt::Checked:
        // text = m_allCheckedText;
        text += QString(" (%1/%2 selected)").arg(m_model->rowCount()).arg(m_model->rowCount());
        break;

      case Qt::Unchecked:
        // text = m_noneCheckedText;
        text += QString(" (0/%1 selected)").arg(m_model->rowCount());
        break;

      case Qt::PartiallyChecked:

        for (int i = 0; i < m_model->rowCount(); i++) {
          if (m_model->item(i)->checkState() == Qt::Checked) {
            numChecked++;
            // if (!text.isEmpty()) {
            //   text += ", ";
            // }
            // text += m_model->item(i)->text();
          }
        }
        text += QString(" (%1/%2 selected)").arg(numChecked).arg(m_model->rowCount());
        break;

      default:
        text = m_titleText;
    }

    lineEdit()->setText(text);
  }

private slots:
  void on_modelDataChanged()
  {
    updateText();
    emit globalCheckStateChanged(globalCheckState());
  }

  void on_itemPressed(const QModelIndex& index)
  {
    QStandardItem* item = m_model->itemFromIndex(index);

    if (item->checkState() == Qt::Checked) {
      item->setCheckState(Qt::Unchecked);
    } else {
      item->setCheckState(Qt::Checked);
    }
  }

public:
  class QCheckListStyledItemDelegate : public QStyledItemDelegate
  {
  public:
    QCheckListStyledItemDelegate(QObject* parent = nullptr)
      : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter* painter_, const QStyleOptionViewItem& option_, const QModelIndex& index_) const override
    {
      QStyleOptionViewItem& refToNonConstOption = const_cast<QStyleOptionViewItem&>(option_);
      refToNonConstOption.showDecorationSelected = false;
      QStyledItemDelegate::paint(painter_, refToNonConstOption, index_);
    }
  };
};
