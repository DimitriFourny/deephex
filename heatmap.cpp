#include "heatmap.h"
#include <QtCore/qnamespace.h>
#include <QtGui/qcolor.h>
#include <QtGui/qguiapplication.h>
#include <QtWidgets/qapplication.h>
#include <sys/types.h>
#include <QApplication>
#include <QChar>
#include <QClipboard>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <cstdint>
#include "memoryview.h"

HeatMap::HeatMap(QWidget* parent) : QWidget(parent)
{
  view_rect_start_ = 0;
  view_rect_end_ = 0;
  memory_view_ = nullptr;
  dragging_ = false;
  setMinimumHeight(20);
  setMaximumHeight(20);
}

void HeatMap::setMemoryView(MemoryView* memory_view)
{
  memory_view_ = memory_view;
}

void HeatMap::updateMap(const QVector<quint8>& data)
{
  regions_.clear();
  int nb_regions = 100;
  uint64_t bytes_per_region = data.size() / nb_regions;

  auto font_color = this->palette().color(QWidget::foregroundRole());
  QColor ascii_color = QColor(0xeb9b9b);
  QColor zero_color = font_color.darker(200);

  for (int i = 0; i < nb_regions; i++) {
    // Calculate the start and end indices for each region
    uint64_t start = i * bytes_per_region;
    uint64_t end = start + bytes_per_region;
    if (end > data.size()) {
      end = data.size();
    }

    // For each region, calculate the number of zeroes and ASCII characters
    int nb_zero = 0;
    int nb_ascii = 0;
    for (int j = start; j < end; ++j) {
      char ch = static_cast<char>(data[j]);
      if (ch == 0) {
        nb_zero++;
      }
      else if (ch >= 32 && ch <= 126) {
        nb_ascii++;
      }
    }

    // Calculate the color
    bytes_per_region = end - start;
    QColor color = font_color;

    if (nb_zero >= bytes_per_region / 2) {
      color = zero_color;
    }
    else if (nb_ascii >= bytes_per_region / 2) {
      color = ascii_color;
    }

    regions_.push_back({start, end, color});
  }

  update();
}

void HeatMap::updateViewRect(uint64_t byte_start, uint64_t byte_end)
{
  view_rect_start_ = byte_start;
  view_rect_end_ = byte_end;
  update();
}

void HeatMap::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter painter(this);

  // Draw background
  auto palette_font_color = this->palette().color(QWidget::foregroundRole());
  painter.setPen(palette_font_color);
  auto select_color = this->palette().color(QPalette::Highlight);
  select_color.setAlpha(100);

  if (regions_.isEmpty()) {
    // If no regions, fill with a default color
    painter.fillRect(rect(), palette_font_color);
    return;
  }

  // Draw heat map regions
  int regions_end = regions_.last().end + 1;

  for (const auto& region : regions_) {
    uint64_t start_x = region.start * width() / regions_end;
    uint64_t end_x = region.end * width() / regions_end;
    painter.fillRect(start_x, 0, end_x - start_x, height(), region.color);
  }

  // Draw view rectangle
  if (view_rect_start_ < view_rect_end_) {
    double percent_start = static_cast<double>(view_rect_start_) / regions_end;
    uint64_t start_x = percent_start * width();
    double percent_end = static_cast<double>(view_rect_end_) / regions_end;
    uint64_t end_x = percent_end * width();
    uint64_t width = end_x - start_x;

    QColor view_rect_color = select_color;
    if (width < 5) {
      // If the width is too small, draw a thin line instead
      width = 1;
      view_rect_color = Qt::black;
    }
    painter.fillRect(start_x, 0, width, height(), view_rect_color);
  }
}

void HeatMap::mousePressEvent(QMouseEvent* event)
{
  if (regions_.isEmpty()) {
    return;
  }
  if (!memory_view_) {
    return;  // No memory view to notify
  }

  dragging_ = true;
  handleDrag(event->pos().x());
}

void HeatMap::mouseMoveEvent(QMouseEvent* event)
{
  if (!dragging_)
    return;
  if (regions_.isEmpty() || !memory_view_)
    return;

  handleDrag(event->pos().x());
}

void HeatMap::mouseReleaseEvent(QMouseEvent* event)
{
  Q_UNUSED(event);
  dragging_ = false;
}

void HeatMap::handleDrag(int x)
{
  uint64_t regions_end = regions_.last().end + 1;
  uint64_t byte_pos = x * regions_end / width();
  byte_pos = qBound(0u, byte_pos, regions_end - 1);

  uint64_t view_rect_width = view_rect_end_ - view_rect_start_;
  memory_view_->moveToPos(byte_pos - view_rect_width / 2);
}