#pragma once

#include <QPointer>
#include <QWidget>
#include <cstdint>
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
  const QVector<MapRegion>& regions() const { return regions_; }

 protected:
  bool event(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  QVector<MapRegion> regions_;
  uint64_t total_size_ = 0;
  uint64_t view_rect_start_ = 0;
  uint64_t view_rect_end_ = 0;
  QPointer<MemoryView> memory_view_;
  bool dragging_ = false;
  uint64_t offsetAt(int x) const;
  void handleDrag(int x);
};
