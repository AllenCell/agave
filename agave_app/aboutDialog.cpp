#include "aboutDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>

AboutDialog::AboutDialog()
  : QDialog()
{
  setWindowTitle("About AGAVE");
  auto layout = new QVBoxLayout(this);
  auto label = new QLabel(this);

  label->setText("<h1 style=\"line-height:70%\"><b>AGAVE</b></h1>"
                 "<i  style=\"line-height:70%\">Advanced GPU Accelerated Volume Explorer</i>"
                 "<h4><b>Version " +
                 qApp->applicationVersion() + "</b></h4>");
  label->setAlignment(Qt::AlignCenter);
  layout->addWidget(label);

  QString agaveUrl = "https://github.com/AllenCell/agave";

  auto text = new QLabel(this);
  text->setText(
    "AGAVE is a desktop volume viewer that uses path trace rendering and cinematic lighting techniques to generate "
    "images with a high degree of resolution and clarity. It is designed and optimized for multi-channel OME-TIFF or "
    "OME-Zarr files.<br><br>"
    "<a href=\"https://www.allencell.org/pathtrace-rendering.html\">Visit our website</a> to learn more and download "
    "the latest version.");
  text->setFrameShape(QFrame::Panel);
  text->setFrameShadow(QFrame::Sunken);
  text->setTextInteractionFlags(Qt::TextBrowserInteraction);
  text->setOpenExternalLinks(true);
  text->setWordWrap(true);
  layout->addWidget(text);

  label = new QLabel(this);
  label->setText("AGAVE is made possible through the hard work and dedication of engineers, designers, and scientists "
                 "at the Allen Institute and through the continued philanthropy of the Paul Allen estate."
                 "<br>"
                 "<br>"
                 "<b>Copyright 2023-26 The Allen Institute.  All rights reserved.</b><br>"
                 "");
  label->setWordWrap(true);
  layout->addWidget(label);

  auto button = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  button->setCenterButtons(true);
  layout->addWidget(button);
  connect(button, &QDialogButtonBox::accepted, this, &QDialog::accept);

  setLayout(layout);
}

AboutDialog::~AboutDialog() {}