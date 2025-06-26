#pragma once

#include <QtCore/qcontainerfwd.h>
#include <QClipboard>
#include <QMouseEvent>
#include <QPainter>
#include <QVector>
#include <QWidget>
#include "standard_map.h"

class MemoryView;

class HeatMap : public QWidget {
  Q_OBJECT

 public:
  explicit HeatMap(QWidget* parent = nullptr);
  virtual void updateMap(const QVector<quint8>& data,
                         StandardMap* map_calculator);
  void updateViewRect(uint64_t byte_start, uint64_t byte_end);
  void setMemoryView(MemoryView* memory_view);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  QVector<MapRegion> regions_;
  unsigned view_rect_start_;
  unsigned view_rect_end_;
  MemoryView* memory_view_;
  bool dragging_;
  void handleDrag(int x);
};
