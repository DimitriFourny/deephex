#include "heatmap.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
#include "memoryview.h"

HeatMap::HeatMap(QWidget* parent) : QWidget(parent)
{
  setMinimumHeight(24);
  setMaximumHeight(24);
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setAccessibleName(tr("File overview"));
}

void HeatMap::setMemoryView(MemoryView* memory_view)
{
  memory_view_ = memory_view;
}

void HeatMap::updateMap(const QVector<quint8>& data,
                        StandardMap* map_calculator)
{
  total_size_ = static_cast<uint64_t>(data.size());
  const QColor foreground = palette().color(foregroundRole());
  if (map_calculator) {
    map_calculator->calculateRegions(data, regions_, foreground);
  }
  else {
    StandardMap fallback;
    fallback.calculateRegions(data, regions_, foreground);
  }
  if (total_size_ == 0) {
    dragging_ = false;
    QToolTip::hideText();
  }
  update();
}

void HeatMap::updateViewRect(uint64_t byte_start, uint64_t byte_end)
{
  // The editor can report its new viewport before the new document map is ready.
  view_rect_start_ = byte_start;
  view_rect_end_ = std::max(byte_start, byte_end);
  update();
}

void HeatMap::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), palette().color(QPalette::Base));
  if (total_size_ == 0 || width() <= 0)
    return;
  const auto pixel = [&](uint64_t offset) {
    return static_cast<qreal>(std::min(offset, total_size_)) /
           static_cast<qreal>(total_size_) * width();
  };
  for (const MapRegion& region : regions_) {
    const qreal left = pixel(region.start);
    const qreal right = pixel(region.end);
    if (right > left)
      painter.fillRect(QRectF(left, 0, right - left, height()), region.color);
  }
  if (std::min(view_rect_start_, total_size_) < std::min(view_rect_end_, total_size_)) {
    const qreal left = pixel(view_rect_start_);
    const qreal right = pixel(view_rect_end_);
    QColor fill = palette().color(QPalette::Highlight);
    fill.setAlpha(95);
    const qreal indicator_width = std::max<qreal>(2, right - left);
    const qreal indicator_left = std::min(left, std::max<qreal>(0, width() - indicator_width));
    const QRectF indicator(indicator_left, 1, indicator_width,
                           std::max(0, height() - 2));
    painter.fillRect(indicator, fill);
    // Palette-derived edges remain visible on bright and dark regions.
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawRect(indicator.adjusted(0, 0, -1, -1));
    if (indicator.width() >= 4 && indicator.height() >= 6) {
      painter.setPen(palette().color(QPalette::Base));
      painter.drawRect(indicator.adjusted(1, 1, -2, -2));
    }
  }
}

uint64_t HeatMap::offsetAt(int x) const
{
  if (total_size_ == 0 || width() <= 1)
    return 0;
  const int bounded = std::clamp(x, 0, width() - 1);
  return static_cast<uint64_t>(static_cast<long double>(bounded) *
                                (total_size_ - 1) / (width() - 1));
}

bool HeatMap::event(QEvent* event)
{
  if (event->type() == QEvent::ToolTip) {
    const auto* help = static_cast<QHelpEvent*>(event);
    if (total_size_ == 0) {
      QToolTip::showText(help->globalPos(), tr("Empty file"), this);
      return true;
    }
    const uint64_t offset = offsetAt(help->pos().x());
    const auto region = std::upper_bound(
        regions_.cbegin(), regions_.cend(), offset,
        [](uint64_t position, const MapRegion& entry) {
          return position < entry.start;
        });
    QString label = tr("Unmapped");
    QString range;
    if (region != regions_.cbegin()) {
      const MapRegion& entry = *(region - 1);
      if (offset < entry.end) {
        label = entry.label;
        range = tr("\nRange [0x%1, 0x%2)")
                    .arg(entry.start, 0, 16).arg(entry.end, 0, 16);
      }
    }
    QToolTip::showText(help->globalPos(),
                      tr("%1\nOffset 0x%2 (%3)%4")
                          .arg(label).arg(offset, 0, 16).arg(offset).arg(range),
                      this);
    return true;
  }
  return QWidget::event(event);
}

void HeatMap::mousePressEvent(QMouseEvent* event)
{
  if (event->button() != Qt::LeftButton || total_size_ == 0 || !memory_view_)
    return;
  dragging_ = true;
  handleDrag(static_cast<int>(event->position().x()));
  event->accept();
}

void HeatMap::mouseMoveEvent(QMouseEvent* event)
{
  if (dragging_ && (event->buttons() & Qt::LeftButton))
    handleDrag(static_cast<int>(event->position().x()));
}

void HeatMap::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    if (dragging_)
      handleDrag(static_cast<int>(event->position().x()));
    dragging_ = false;
  }
}

void HeatMap::handleDrag(int x)
{
  if (!memory_view_ || total_size_ == 0 || width() <= 0)
    return;
  const uint64_t position = offsetAt(x);
  const uint64_t span = std::min(view_rect_end_, total_size_) -
                        std::min(view_rect_start_, total_size_);
  const uint64_t half = span / 2;
  const uint64_t centered = position >= half ? position - half : 0;
  const uint64_t maximum = span != 0 ? total_size_ - span : total_size_ - 1;
  memory_view_->moveToPos(std::min(centered, maximum));
}
