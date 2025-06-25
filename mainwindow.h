#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "heatmap.h"
#include "memoryview.h"

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() = default;

 private slots:
  void onGoToAddress();  // Slot for Go to action
  void onOpenFile();

 private:
  MemoryView* memory_view_;
  HeatMap* heat_map_;
  QAction* goToAction_;  // QAction for Go to
};

#endif  // MAINWINDOW_H