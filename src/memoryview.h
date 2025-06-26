#ifndef MEMORYVIEW_H
#define MEMORYVIEW_H

#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStatusBar>
#include <QVector>
#include <QWidget>
#include "heatmap.h"

class HeatMap;

class MemoryView : public QWidget {
  Q_OBJECT

 public:
  explicit MemoryView(QWidget* parent, HeatMap* heat_map);
  void setData(const QVector<quint8>& data);
  const QVector<quint8>& getData() const;
  void moveToPos(uint64_t pos, bool select_byte = false);
  void setStatusBar(QStatusBar* status_bar);

 protected:
  QSize sizeHint() const override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

 private:
  QVector<quint8> memory_data_;
  int font_width_;
  int font_height_;
  int selection_start_;
  int selection_end_;
  bool selecting_;
  int first_visible_row_;
  HeatMap* heat_map_;
  int edit_nibble_;
  QStatusBar* status_bar_;

  static const int BYTES_PER_LINE = 16;
  static const int CELL_WIDTH = 40;
  static const int CELL_HEIGHT = 20;
  static const int ADDRESS_WIDTH = 80;

  int posToIndex(const QPoint& pos) const;
  void copySelectionToClipboard() const;
  int numVisibleRows() const;
  int maxFirstVisibleRows() const;
  void zeroSelection();
  void editSelectedByte(int value);
};

#endif  // MEMORYVIEW_H