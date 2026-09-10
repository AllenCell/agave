#include "citationDialog.h"

#include <QApplication>
#include <QDate>
#include <QDialogButtonBox>
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>

CitationDialog::CitationDialog()
  : QDialog()
{
  setWindowTitle("Cite AGAVE");
  auto layout = new QVBoxLayout(this);

  auto label = new QLabel(this);
  label->setText("If you use AGAVE in your research, please cite the following:");
  layout->addWidget(label);

  QString agaveUrl = "https://github.com/AllenCell/agave";

  int year = QDate::currentDate().year();
  QString syear = QString::number(year);

  auto citationtext = new QLabel(this);
  citationtext->setText("Daniel Toloudis, AGAVE Contributors (" + syear +
                        "). AGAVE: Advanced GPU Accelerated Volume Explorer (Version " + qApp->applicationVersion() +
                        ") [Computer software]. Allen Institute, Cell Science. <a "
                        "href=\"" +
                        agaveUrl + "\"></a>");
  citationtext->setFrameShape(QFrame::Panel);
  citationtext->setFrameShadow(QFrame::Sunken);
  citationtext->setTextInteractionFlags(Qt::TextBrowserInteraction);
  citationtext->setOpenExternalLinks(true);
  citationtext->setWordWrap(true);
  layout->addWidget(citationtext);

  auto label2 = new QLabel(this);
  label2->setText("bibtex:");
  layout->addWidget(label2);

  auto citationtext2 = new QLabel(this);
  citationtext2->setText("<tt>@software{agave,<br/>"
                         "&nbsp;&nbsp;author    = {Toloudis, Daniel and AGAVE Contributors},<br/>"
                         "&nbsp;&nbsp;title     = {AGAVE: Advanced GPU Accelerated Volume Explorer},<br/>"
                         "&nbsp;&nbsp;year      = {" +
                         syear +
                         "},<br/>"
                         "&nbsp;&nbsp;version = {" +
                         qApp->applicationVersion() +
                         "},<br/>"
                         "&nbsp;&nbsp;url       = {" +
                         agaveUrl +
                         "},<br/>"
                         "&nbsp;&nbsp;organization = {Allen Institute, Cell Science},<br/>"
                         "&nbsp;&nbsp;note      = {Computer Software}<br/>"
                         "}</tt><br/>");
  citationtext2->setFrameShape(QFrame::Panel);
  citationtext2->setFrameShadow(QFrame::Sunken);
  citationtext2->setTextInteractionFlags(Qt::TextBrowserInteraction);
  citationtext2->setOpenExternalLinks(true);
  citationtext2->setWordWrap(true);
  layout->addWidget(citationtext2);

  auto button = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  button->setCenterButtons(true);
  layout->addWidget(button);
  connect(button, &QDialogButtonBox::accepted, this, &QDialog::accept);

  setLayout(layout);
}

CitationDialog::~CitationDialog() = default;
