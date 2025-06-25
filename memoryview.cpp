#include "memoryview.h"
#include <QtCore/qminmax.h>
#include <QtCore/qnamespace.h>
#include <QtGui/qcolor.h>
#include <QtGui/qguiapplication.h>
#include <QtWidgets/qapplication.h>
#include <sys/types.h>
#include <QApplication>
#include <QChar>
#include <QClipboard>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <cstdint>
#include "heatmap.h"

MemoryView::MemoryView(QWidget* parent, HeatMap* heat_map) : QWidget(parent)
{
  selection_start_ = -1;
  selection_end_ = -1;
  selecting_ = false;
  first_visible_row_ = 0;
  heat_map_ = heat_map;
  edit_nibble_ = 0;

  setFont(QFont("Courier New", 12));
  const QFontMetrics font_metrics(font());
  font_width_ = font_metrics.horizontalAdvance(' ');
  font_height_ = font_metrics.height();

  setFocusPolicy(Qt::StrongFocus);  // For keyboard input
}

void MemoryView::setData(const QVector<quint8>& data)
{
  memory_data_ = data;
  moveToPos(0);
  update();
}

const QVector<quint8>& MemoryView::getData() const
{
  return memory_data_;
}

void MemoryView::setStatusBar(QStatusBar* status_bar)
{
  status_bar_ = status_bar;
}

void MemoryView::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::TextAntialiasing);
  const int y_offset = font_height_;

  // Draw background
  auto palette_font_color = this->palette().color(QWidget::foregroundRole());
  painter.setPen(palette_font_color);
  auto select_color = this->palette().color(QPalette::Highlight);

  // Calculate a stronger color for ascii characters
  QColor ascii_color = QColor(0xeb9b9b);
  QColor header_color = palette_font_color.darker(150);
  QColor addr_color = palette_font_color.darker(150);
  QColor zero_color = palette_font_color.darker(200);

  // Draw header (aligned per cell)
  painter.setPen(header_color);
  for (int col = 0; col < BYTES_PER_LINE; ++col) {
    QString header = QString("%1").arg(col, 2, 16, QChar('0')).toUpper();
    int x = ADDRESS_WIDTH + col * CELL_WIDTH;
    painter.drawText(x, y_offset, CELL_WIDTH, font_height_, Qt::AlignCenter,
                     header);
  }

  // Draw ASCII header
  int ascii_x = ADDRESS_WIDTH + BYTES_PER_LINE * CELL_WIDTH + 20;
  int ascii_area_width = font_width_ * BYTES_PER_LINE;
  painter.drawText(ascii_x, y_offset, ascii_area_width, font_height_,
                   Qt::AlignLeft, "ASCII");

  // Draw memory content
  int num_rows = (memory_data_.size() + BYTES_PER_LINE - 1) / BYTES_PER_LINE;
  int ascii_width = font_width_;

  int num_rows_left = num_rows - first_visible_row_;
  int num_rows_to_show = qMin(num_rows_left, numVisibleRows());

  for (int row = 0; row < num_rows_to_show; row++) {
    int y = y_offset + (row + 1) * font_height_;

    // Draw address
    painter.setPen(header_color);
    uint64_t addr = (first_visible_row_ + row) * BYTES_PER_LINE;
    QString addr_str = QString("%1").arg(addr, 8, 16, QChar('0')).toUpper();
    painter.drawText(0, y, ADDRESS_WIDTH, font_height_,
                     Qt::AlignRight | Qt::AlignVCenter, addr_str);

    for (int col = 0; col < BYTES_PER_LINE; ++col) {
      int i = (first_visible_row_ + row) * BYTES_PER_LINE + col;
      if (i >= memory_data_.size())
        break;

      int x = ADDRESS_WIDTH + col * CELL_WIDTH;
      int ascii_char_x = ascii_x + ascii_width * col;

      // Highlight selection in hex view
      if (selection_start_ != -1 && selection_end_ != -1) {
        int sel_min = qMin(selection_start_, selection_end_);
        int sel_max = qMax(selection_start_, selection_end_);
        if (i >= sel_min && i <= sel_max) {
          painter.fillRect(x, y, CELL_WIDTH, font_height_, select_color);
          painter.fillRect(ascii_char_x, y, ascii_width, font_height_,
                           select_color);
        }
      }

      // ASCII color logic
      char ch = static_cast<char>(memory_data_[i]);
      if (ch == 0) {
        painter.setPen(zero_color);  // Highlight zero bytes
      }
      else if (ch >= 32 && ch <= 126) {
        painter.setPen(ascii_color);  // Highlight printable characters
      }
      else {
        painter.setPen(palette_font_color);
      }

      // Draw byte value
      QString byte_str =
          QString("%1").arg(memory_data_[i], 2, 16, QChar('0')).toUpper();
      painter.drawText(x, y, CELL_WIDTH, font_height_, Qt::AlignCenter,
                       byte_str);

      // Draw ASCII character
      QChar ascii_char = (ch >= 32 && ch <= 126) ? QChar(ch) : QChar('.');
      painter.drawText(ascii_char_x, y, ascii_width, font_height_,
                       Qt::AlignCenter, ascii_char);
    }
  }

  // Add a scrollbar if needed
  if (num_rows_to_show < num_rows) {
    int scrollbar_height =
        ((float)numVisibleRows() / (float)num_rows) * height();
    if (scrollbar_height < 20) {
      scrollbar_height = 20;  // Minimum height for scrollbar
    }

    int max_scroll_y = height() - scrollbar_height;
    float scroll_ratio =
        (float)first_visible_row_ / (float)(num_rows - numVisibleRows());
    scroll_ratio = qBound(0.0f, scroll_ratio, 1.0f);
    int scrollbar_y = scroll_ratio * max_scroll_y;

    painter.fillRect(width() - 20, scrollbar_y, 10, scrollbar_height,
                     QColor(200, 200, 200));
  }

  // Update heatmap if available
  if (heat_map_) {
    heat_map_->updateViewRect(
        first_visible_row_ * BYTES_PER_LINE,
        (first_visible_row_ + num_rows_to_show) * BYTES_PER_LINE);
  }
}

QSize MemoryView::sizeHint() const
{
  int num_rows = (memory_data_.size() + BYTES_PER_LINE - 1) / BYTES_PER_LINE;
  const QFontMetrics font_metrics(font());
  int line_height = font_metrics.height();
  int ascii_area_width = font_width_ * BYTES_PER_LINE;
  int total_height = (num_rows + 1) * line_height;  // +1 for header

  // Add space for ASCII area
  return QSize(
      ADDRESS_WIDTH + BYTES_PER_LINE * CELL_WIDTH + 20 + ascii_area_width,
      total_height);
}

int MemoryView::posToIndex(const QPoint& pos) const
{
  const QFontMetrics font_metrics(font());
  int line_height = font_metrics.height();
  int y_offset = line_height;
  int row = (pos.y() - y_offset) / line_height - 1;
  if (row < 0)
    return -1;

  // Hex panel column
  int col = (pos.x() - ADDRESS_WIDTH) / CELL_WIDTH;

  // ASCII panel start x and width per char
  int ascii_x = ADDRESS_WIDTH + BYTES_PER_LINE * CELL_WIDTH + 20;
  int ascii_width = font_width_;

  // Check if click is in hex panel
  if (col >= 0 && col < BYTES_PER_LINE) {
    int idx = (first_visible_row_ + row) * BYTES_PER_LINE + col;
    if (idx < 0 || idx >= memory_data_.size())
      return -1;
    return idx;
  }

  // Check if click is in ASCII panel
  if (pos.x() >= ascii_x && pos.x() < ascii_x + ascii_width * BYTES_PER_LINE) {
    int ascii_col = (pos.x() - ascii_x) / ascii_width;
    if (ascii_col >= 0 && ascii_col < BYTES_PER_LINE) {
      int idx = (first_visible_row_ + row) * BYTES_PER_LINE + ascii_col;
      if (idx < 0 || idx >= memory_data_.size())
        return -1;
      return idx;
    }
  }

  return -1;
}

void MemoryView::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::RightButton) {
    QMenu menu(this);
    QAction* zeroAction = menu.addAction("Zero selected bytes");
    zeroAction->setEnabled(selection_start_ != -1 && selection_end_ != -1);
    QAction* chosen = menu.exec(event->globalPosition().toPoint());
    if (chosen == zeroAction) {
      zeroSelection();
    }
    return;
  }

  int idx = posToIndex(event->pos());
  if (idx == -1) {
    return;
  }

  selection_start_ = selection_end_ = idx;
  selecting_ = true;
  edit_nibble_ = 0;  // Reset nibble edit state on new selection
  update();
}

void MemoryView::mouseMoveEvent(QMouseEvent* event)
{
  if (selecting_) {
    int idx = posToIndex(event->pos());
    if (idx != -1 && idx != selection_end_) {
      selection_end_ = idx;
      update();
    }
  }

  // Update the status bar to show the value in Little Endian
  if (!status_bar_) {
    return;
  }

  // Get the number of bytes to show
  int sel_min = qMin(selection_start_, selection_end_);
  int sel_max = qMax(selection_start_, selection_end_);
  unsigned nb_bytes =
      qMin(sel_max - sel_min + 1, memory_data_.size() - sel_min);
  nb_bytes = qMin(nb_bytes, 16u);

  // Calculate the value
  uint64_t value = 0;
  for (uint64_t i = 0; i < nb_bytes; i++) {
    value |= ((uint64_t)memory_data_[sel_min + i]) << (i * 8);
  }

  QString addr_str = QString("%1").arg(sel_min, 2, 16, QChar('0')).toUpper();
  QString value_str =
      QString("%1").arg(value, 2 * nb_bytes, 16, QChar('0')).toUpper();
  status_bar_->showMessage(
      QString("@0x%1: 0x%2 (%3)").arg(addr_str).arg(value_str).arg(value));
}

void MemoryView::mouseReleaseEvent(QMouseEvent* event)
{
  Q_UNUSED(event);
  if (selecting_) {
    selecting_ = false;
    copySelectionToClipboard();
  }
}

void MemoryView::copySelectionToClipboard() const
{
  if (selection_start_ == -1 || selection_end_ == -1)
    return;
  int sel_min = qMin(selection_start_, selection_end_);
  int sel_max = qMax(selection_start_, selection_end_);
  QStringList hex_list;
  for (int i = sel_min; i <= sel_max; ++i) {
    hex_list << QString("%1").arg(memory_data_[i], 2, 16, QChar('0')).toUpper();
  }
  QString hex_str = hex_list.join(" ");
  QClipboard* clipboard = QApplication::clipboard();
  clipboard->setText(hex_str);
}

void MemoryView::wheelEvent(QWheelEvent* event)
{
  int delta = event->angleDelta().y() / 2;  // Adjust scroll speed
  if (event->inverted()) {
    first_visible_row_ -= delta;
  }
  else {
    first_visible_row_ += delta;
  }
  if (first_visible_row_ < 0) {
    first_visible_row_ = 0;
  }

  first_visible_row_ = qMin(first_visible_row_, maxFirstVisibleRows());

  // Redraw to reflect new scroll position
  update();
  event->accept();
}

int MemoryView::numVisibleRows() const
{
  return (height() - font_height_) / font_height_ - 1;  // -1 for header
}

int MemoryView::maxFirstVisibleRows() const
{
  int num_rows = (memory_data_.size() + BYTES_PER_LINE - 1) / BYTES_PER_LINE;
  return qMax(0, num_rows - numVisibleRows());
}

void MemoryView::moveToPos(uint64_t pos, bool select_byte)
{
  // Get the first row to show
  pos = qBound(0u, pos, (uint64_t)memory_data_.size() - 1);
  int row = pos / BYTES_PER_LINE;
  row = qBound(0, row, maxFirstVisibleRows());
  first_visible_row_ = row;

  if (select_byte) {
    // Select the byte at the given position
    selection_start_ = selection_end_ = pos;
    edit_nibble_ = 0;  // Reset nibble edit state on new position
  }

  update();
}

void MemoryView::zeroSelection()
{
  if (selection_start_ == -1 || selection_end_ == -1)
    return;
  int sel_min = qMin(selection_start_, selection_end_);
  int sel_max = qMax(selection_start_, selection_end_);
  for (int i = sel_min; i <= sel_max; ++i) {
    memory_data_[i] = 0;
  }
  update();
}

void MemoryView::keyPressEvent(QKeyEvent* event)
{
  if (selection_start_ == -1 || selection_end_ == -1 ||
      selection_start_ != selection_end_)
    return;

  int idx = selection_start_;
  int value = -1;
  int key = event->key();
  if ((key >= Qt::Key_0 && key <= Qt::Key_9)) {
    value = key - Qt::Key_0;
  }
  else if (key >= Qt::Key_A && key <= Qt::Key_F) {
    value = 10 + (key - Qt::Key_A);
  }

  if (value != -1) {
    editSelectedByte(value);
    event->accept();
    return;
  }

  QWidget::keyPressEvent(event);
}

void MemoryView::editSelectedByte(int value)
{
  if (selection_start_ == -1 || selection_end_ == -1 ||
      selection_start_ != selection_end_)
    return;
  int idx = selection_start_;
  quint8 orig = memory_data_[idx];
  if (edit_nibble_ == 0) {
    // High nibble
    memory_data_[idx] = (value << 4) | (orig & 0x0F);
    edit_nibble_ = 1;
  }
  else {
    // Low nibble
    memory_data_[idx] = (orig & 0xF0) | value;
    edit_nibble_ = 0;
    // Optionally move selection to next byte
    if (idx + 1 < memory_data_.size()) {
      selection_start_ = selection_end_ = idx + 1;
    }
  }
  update();
}