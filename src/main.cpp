#include <QApplication>
#include <QColor>
#include <QPalette>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);

  // Set a dark color scheme
  QPalette palette;
  palette.setColor(QPalette::Window, QColor("#232629"));
  palette.setColor(QPalette::WindowText, QColor("#f0f0f0"));
  palette.setColor(QPalette::Base, QColor("#181a1b"));
  palette.setColor(QPalette::AlternateBase, QColor("#232629"));
  palette.setColor(QPalette::ToolTipBase, QColor("#232629"));
  palette.setColor(QPalette::ToolTipText, QColor("#f0f0f0"));
  palette.setColor(QPalette::Text, QColor("#f0f0f0"));
  palette.setColor(QPalette::Button, QColor("#232629"));
  palette.setColor(QPalette::ButtonText, QColor("#f0f0f0"));
  palette.setColor(QPalette::BrightText, QColor("#ff4040"));
  palette.setColor(QPalette::Highlight, QColor("#14305c"));
  palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));

  app.setPalette(palette);

  MainWindow mainWindow;
  mainWindow.show();
  return app.exec();
}