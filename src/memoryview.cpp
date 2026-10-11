#include "memoryview.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QUndoCommand>
#include <algorithm>
#include <utility>

#include "heatmap.h"

namespace {
constexpr char kHexDigits[] = "0123456789ABCDEF";

int hexDigit(QChar character)
{
  const ushort value = character.unicode();
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

QColor blend(const QColor& first, const QColor& second, int percent)
{
  return QColor((first.red() * (100 - percent) + second.red() * percent) / 100,
                (first.green() * (100 - percent) + second.green() * percent) / 100,
                (first.blue() * (100 - percent) + second.blue() * percent) / 100);
}
}  // namespace

class ByteEditCommand : public QUndoCommand {
 public:
  ByteEditCommand(MemoryView* view, int start, QVector<quint8> before,
                  QVector<quint8> after, const QString& description,
                  quint64 token)
      : QUndoCommand(description), view_(view), start_(start),
        before_(std::move(before)), after_(std::move(after)), token_(token)
  {
  }

  void undo() override { view_->applyBytes(start_, before_); }
  void redo() override { view_->applyBytes(start_, after_); }
  int id() const override { return token_ ? 1 : -1; }

  bool mergeWith(const QUndoCommand* command) override
  {
    const auto* other = dynamic_cast<const ByteEditCommand*>(command);
    if (!other || other->view_ != view_ || !token_ || token_ != other->token_ ||
        start_ != other->start_ || before_.size() != other->before_.size())
      return false;
    after_ = other->after_;
    setObsolete(before_ == after_);
    return true;
  }

 private:
  MemoryView* view_;
  int start_;
  QVector<quint8> before_;
  QVector<quint8> after_;
  quint64 token_;
};

MemoryView::MemoryView(QWidget* parent, HeatMap* heat_map)
    : QAbstractScrollArea(parent), heat_map_(heat_map), undo_stack_(this)
{
  QFont fixed_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  fixed_font.setPointSize(13);
  setFont(fixed_font);
  setFocusPolicy(Qt::StrongFocus);
  setFrameShape(QFrame::StyledPanel);
  viewport()->setMouseTracking(true);
  viewport()->setAutoFillBackground(false);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  connect(&undo_stack_, &QUndoStack::cleanChanged, this, [this](bool clean) {
    emit modifiedChanged(!clean);
  });
  connect(&undo_stack_, &QUndoStack::indexChanged, this, [this](int) {
    if (!pushing_command_)
      resetNibble();
  });
  drag_timer_.setInterval(40);
  connect(&drag_timer_, &QTimer::timeout, this, &MemoryView::updateDrag);
  updateMetrics();
}

MemoryView::~MemoryView()
{
  // QUndoStack::clear emits while member destruction precedes QObject disconnection.
  undo_stack_.disconnect(this);
}

void MemoryView::setTerminalTheme(bool enabled)
{
  if (terminal_theme_ == enabled)
    return;
  terminal_theme_ = enabled;
  viewport()->update();
}

void MemoryView::setData(QVector<quint8> data)
{
  stopDragging();
  resetNibble();
  memory_data_ = std::move(data);
  undo_stack_.clear();
  undo_stack_.setClean();
  anchor_ = cursor_ = memory_data_.isEmpty() ? -1 : 0;
  pane_ = Pane::Hex;
  updateScrollBars();
  verticalScrollBar()->setValue(0);
  horizontalScrollBar()->setValue(0);
  updateOverview();
  viewport()->update();
  emit selectionChanged();
  emit dataChanged();
}

const QVector<quint8>& MemoryView::getData() const
{
  return memory_data_;
}

int MemoryView::selectionStart() const
{
  return cursor_ < 0 || anchor_ < 0 ? -1 : qMin(cursor_, anchor_);
}

int MemoryView::selectionLength() const
{
  return selectionStart() < 0 ? 0 : qMax(cursor_, anchor_) - selectionStart() + 1;
}

void MemoryView::selectRange(int start, int length)
{
  resetNibble();
  const int old_anchor = anchor_;
  const int old_cursor = cursor_;
  if (memory_data_.isEmpty() || length <= 0) {
    anchor_ = cursor_ = -1;
  }
  else {
    start = qBound(0, start, int(memory_data_.size()) - 1);
    anchor_ = start;
    cursor_ = start + qMin(length, int(memory_data_.size()) - start) - 1;
    ensureCursorVisible();
  }
  viewport()->update();
  if (old_anchor != anchor_ || old_cursor != cursor_)
    emit selectionChanged();
}

QUndoStack* MemoryView::undoStack()
{
  return &undo_stack_;
}

bool MemoryView::isModified() const
{
  return !undo_stack_.isClean();
}

void MemoryView::markSaved()
{
  resetNibble();
  undo_stack_.setClean();
}

void MemoryView::moveToPos(uint64_t pos, bool select_byte)
{
  if (memory_data_.isEmpty())
    return;
  const int index = int(std::min(pos, uint64_t(memory_data_.size() - 1)));
  if (select_byte) {
    resetNibble();
    setCursor(index, false);
  }
  verticalScrollBar()->setValue(index / kBytesPerRow);
  updateOverview();
}

QSize MemoryView::sizeHint() const
{
  return QSize(content_width_ + 24, header_height_ + row_height_ * 24);
}

int MemoryView::hexCellX(int column) const
{
  return hex_start_ + column * char_width_ * 3 + (column / 4) * char_width_;
}

int MemoryView::visibleRows() const
{
  return qMax(1, (viewport()->height() - header_height_) / row_height_);
}

void MemoryView::updateMetrics()
{
  const QFontMetrics metrics(font());
  char_width_ = qMax(1, metrics.horizontalAdvance(QLatin1Char('0')));
  row_height_ = qMax(1, metrics.height() + 6);
  header_height_ = row_height_ + 8;
  hex_start_ = char_width_ * 11;
  ascii_start_ = hexCellX(kBytesPerRow - 1) + char_width_ * 4;
  content_width_ = ascii_start_ + kBytesPerRow * char_width_ + char_width_ * 2;
  updateScrollBars();
  viewport()->update();
}

void MemoryView::updateScrollBars()
{
  const int rows = memory_data_.isEmpty()
                       ? 0 : (int(memory_data_.size()) - 1) / kBytesPerRow + 1;
  verticalScrollBar()->setSingleStep(1);
  verticalScrollBar()->setPageStep(visibleRows());
  verticalScrollBar()->setRange(0, qMax(0, rows - visibleRows()));
  horizontalScrollBar()->setSingleStep(char_width_ * 3);
  horizontalScrollBar()->setPageStep(qMax(1, viewport()->width()));
  horizontalScrollBar()->setRange(0, qMax(0, content_width_ - viewport()->width()));
  updateOverview();
}

void MemoryView::updateOverview()
{
  if (!heat_map_)
    return;
  const uint64_t start = uint64_t(verticalScrollBar()->value()) * kBytesPerRow;
  const int painted_rows = qMax(0, (viewport()->height() - header_height_ +
                                   row_height_ - 1) / row_height_);
  const uint64_t end = std::min(uint64_t(memory_data_.size()),
                              start + uint64_t(painted_rows) * kBytesPerRow);
  heat_map_->updateViewRect(std::min(start, end), end);
}

void MemoryView::scrollContentsBy(int dx, int dy)
{
  Q_UNUSED(dx);
  Q_UNUSED(dy);
  viewport()->update();
  updateOverview();
}

void MemoryView::resizeEvent(QResizeEvent* event)
{
  QAbstractScrollArea::resizeEvent(event);
  updateScrollBars();
}

void MemoryView::changeEvent(QEvent* event)
{
  QAbstractScrollArea::changeEvent(event);
  if (event->type() == QEvent::FontChange ||
      event->type() == QEvent::ApplicationFontChange)
    updateMetrics();
  else if (event->type() == QEvent::PaletteChange)
    viewport()->update();
}

void MemoryView::focusInEvent(QFocusEvent* event)
{
  QAbstractScrollArea::focusInEvent(event);
  viewport()->update();
}

void MemoryView::focusOutEvent(QFocusEvent* event)
{
  stopDragging();
  resetNibble();
  QAbstractScrollArea::focusOutEvent(event);
  viewport()->update();
}

bool MemoryView::focusNextPrevChild(bool next)
{
  Q_UNUSED(next);
  // Tab is an editor command, not focus traversal between the scrollbars.
  return false;
}

void MemoryView::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter painter(viewport());
  painter.setFont(font());
  const QPalette colors = palette();
  const QColor base = colors.color(QPalette::Base);
  const QColor text = colors.color(QPalette::Text);
  const QColor muted = terminal_theme_ ? colors.color(QPalette::PlaceholderText)
                                       : blend(text, base, 48);
  const QColor printable = terminal_theme_ ? colors.color(QPalette::LinkVisited)
                                           : blend(text, colors.color(QPalette::Link), 32);
  QColor selection = hasFocus() ? colors.color(QPalette::Highlight)
                                : blend(colors.color(QPalette::Highlight), base, 45);
  selection.setAlpha(255);
  const QColor selected_text = colors.color(QPalette::HighlightedText);
  if (!terminal_theme_)
    painter.fillRect(viewport()->rect(), base);

  if (memory_data_.isEmpty()) {
    if (terminal_theme_)
      painter.fillRect(viewport()->rect(), base);
    painter.setPen(muted);
    painter.drawText(viewport()->rect(), Qt::AlignCenter,
                     tr("No bytes to display\nOpen a file to begin editing."));
    return;
  }

  const int horizontal = horizontalScrollBar()->value();
  painter.translate(-horizontal, 0);
  const int first_row = verticalScrollBar()->value();
  const int selected_start = selectionStart();
  const int selected_end = selected_start + selectionLength();
  const int row_count = (int(memory_data_.size()) - 1) / kBytesPerRow + 1;
  const int visible = qMax(0, (viewport()->height() - header_height_ +
                             row_height_ - 1) / row_height_);
  const int last_row = qMin(row_count, first_row + visible);
  const QColor alternate = terminal_theme_ ? colors.color(QPalette::AlternateBase)
                                           : blend(base, text, 3);
  if (terminal_theme_) {
    // Paint the Base tint once, not again over a previously filled viewport.
    painter.fillRect(horizontal, 0, viewport()->width(), header_height_, base);
    const int bottom = header_height_ + (last_row - first_row) * row_height_;
    if (bottom < viewport()->height())
      painter.fillRect(horizontal, bottom, viewport()->width(),
                       viewport()->height() - bottom, base);
  }
  painter.save();
  painter.setClipRect(horizontal, header_height_, viewport()->width(),
                      qMax(0, viewport()->height() - header_height_));
  for (int row = first_row; row < last_row; ++row) {
    const int y = header_height_ + (row - first_row) * row_height_;
    if (terminal_theme_)
      painter.fillRect(horizontal, y, viewport()->width(), row_height_,
                       row & 1 ? alternate : base);
    else if (row & 1)
      painter.fillRect(horizontal, y, viewport()->width(), row_height_, alternate);
    painter.setPen(terminal_theme_ ? colors.color(QPalette::Link) : muted);
    painter.drawText(char_width_, y, char_width_ * 8, row_height_,
                     Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("%1").arg(qulonglong(row) * kBytesPerRow,
                                              8, 16, QLatin1Char('0')).toUpper());
    for (int column = 0; column < kBytesPerRow; ++column) {
      const int index = row * kBytesPerRow + column;
      if (index >= memory_data_.size())
        break;
      const quint8 byte = memory_data_.at(index);
      const bool is_printable = byte >= 32 && byte <= 126;
      const bool selected = index >= selected_start && index < selected_end;
      const QRect hex_rect(hexCellX(column), y, char_width_ * 3, row_height_);
      const QRect ascii_rect(ascii_start_ + column * char_width_, y,
                             char_width_, row_height_);
      if (selected) {
        painter.fillRect(hex_rect, selection);
        painter.fillRect(ascii_rect, selection);
      }
      painter.setPen(selected ? selected_text
                              : byte == 0 ? muted : is_printable ? printable : text);
      const QChar digits[] = {QLatin1Char(kHexDigits[byte >> 4]),
                              QLatin1Char(kHexDigits[byte & 15])};
      painter.drawText(hex_rect.adjusted(char_width_ / 2, 0, 0, 0),
                       Qt::AlignLeft | Qt::AlignVCenter, QString(digits, 2));
      painter.drawText(ascii_rect, Qt::AlignCenter,
                       QString(is_printable ? QChar(byte) : QLatin1Char('.')));
      if (index == cursor_) {
        painter.setPen(hasFocus() ? selected_text : muted);
        painter.drawRect(hex_rect.adjusted(0, 1, -1, -2));
        painter.drawRect(ascii_rect.adjusted(0, 1, -1, -2));
        if (hasFocus()) {
          const int caret_x = pane_ == Pane::Hex
                                  ? hex_rect.x() + char_width_ / 2 + nibble_ * char_width_
                                  : ascii_rect.x();
          painter.fillRect(caret_x, y + row_height_ - 4, char_width_, 2,
                           selected ? selected_text : text);
        }
      }
    }
  }
  painter.restore();

  if (!terminal_theme_)
    painter.fillRect(horizontal, 0, viewport()->width(), header_height_,
                     colors.color(QPalette::Window));
  painter.setPen(terminal_theme_ ? colors.color(QPalette::Link) : muted);
  painter.drawText(char_width_, 0, char_width_ * 8, header_height_,
                   Qt::AlignRight | Qt::AlignVCenter, tr("OFFSET"));
  for (int column = 0; column < kBytesPerRow; ++column) {
    painter.drawText(hexCellX(column), 0, char_width_ * 3, header_height_,
                     Qt::AlignCenter,
                     QStringLiteral("%1").arg(column, 2, 16, QLatin1Char('0')).toUpper());
  }
  painter.drawText(ascii_start_, 0, char_width_ * kBytesPerRow, header_height_,
                   Qt::AlignLeft | Qt::AlignVCenter, tr("ASCII"));
  painter.setPen(terminal_theme_ ? colors.color(QPalette::Mid) : blend(text, base, 80));
  painter.drawLine(horizontal, header_height_ - 1,
                   horizontal + viewport()->width(), header_height_ - 1);
}

int MemoryView::indexAt(const QPoint& position, Pane* pane, bool clamp) const
{
  if (memory_data_.isEmpty() || (!clamp && position.y() < header_height_))
    return -1;
  const int x = position.x() + horizontalScrollBar()->value();
  int column = -1;
  Pane target = *pane;
  if (clamp) {
    if (target == Pane::Ascii)
      column = qBound(0, (x - ascii_start_) / char_width_, kBytesPerRow - 1);
    else {
      column = 0;
      for (int candidate = 1; candidate < kBytesPerRow; ++candidate) {
        if (x >= hexCellX(candidate))
          column = candidate;
      }
    }
  }
  else if (x >= ascii_start_ && x < ascii_start_ + kBytesPerRow * char_width_) {
    target = Pane::Ascii;
    column = (x - ascii_start_) / char_width_;
  }
  else {
    for (int candidate = 0; candidate < kBytesPerRow; ++candidate) {
      if (x >= hexCellX(candidate) &&
          x < hexCellX(candidate) + char_width_ * 3) {
        target = Pane::Hex;
        column = candidate;
        break;
      }
    }
  }
  if (column < 0)
    return -1;
  const int local_row = qMax(0, (position.y() - header_height_) / row_height_);
  const qint64 index = (qint64(verticalScrollBar()->value()) + local_row) *
                          kBytesPerRow + column;
  if (!clamp && index >= memory_data_.size())
    return -1;
  *pane = target;
  return int(qBound(qint64(0), index, qint64(memory_data_.size() - 1)));
}

void MemoryView::resetNibble()
{
  nibble_ = 0;
  ++edit_token_;
  viewport()->update();
}

void MemoryView::setCursor(int index, bool extend)
{
  if (memory_data_.isEmpty())
    return;
  index = qBound(0, index, int(memory_data_.size()) - 1);
  const int old_anchor = anchor_;
  const int old_cursor = cursor_;
  if (!extend || anchor_ < 0)
    anchor_ = index;
  cursor_ = index;
  ensureCursorVisible();
  viewport()->update();
  if (old_anchor != anchor_ || old_cursor != cursor_)
    emit selectionChanged();
}

void MemoryView::ensureCursorVisible()
{
  if (cursor_ < 0)
    return;
  const int row = cursor_ / kBytesPerRow;
  const int first = verticalScrollBar()->value();
  if (row < first)
    verticalScrollBar()->setValue(row);
  else if (row >= first + visibleRows())
    verticalScrollBar()->setValue(row - visibleRows() + 1);
  const int column = cursor_ % kBytesPerRow;
  const int x = pane_ == Pane::Hex ? hexCellX(column)
                                  : ascii_start_ + column * char_width_;
  const int width = pane_ == Pane::Hex ? char_width_ * 3 : char_width_;
  const int left = horizontalScrollBar()->value();
  if (x < left)
    horizontalScrollBar()->setValue(x);
  else if (x + width > left + viewport()->width())
    horizontalScrollBar()->setValue(x + width - viewport()->width());
}

void MemoryView::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::RightButton) {
    resetNibble();
    QMenu menu(this);
    QAction* copy = menu.addAction(tr("Copy Hex"));
    QAction* paste = menu.addAction(pane_ == Pane::Hex ? tr("Paste Hex") : tr("Paste Text"));
    QAction* zero = menu.addAction(tr("Zero Selection"));
    QAction* all = menu.addAction(tr("Select All"));
    menu.addSeparator();
    QAction* undo = menu.addAction(tr("Undo"));
    QAction* redo = menu.addAction(tr("Redo"));
    copy->setEnabled(selectionLength() > 0);
    zero->setEnabled(selectionLength() > 0);
    paste->setEnabled(selectionLength() > 0);
    all->setEnabled(!memory_data_.isEmpty());
    undo->setEnabled(undo_stack_.canUndo());
    redo->setEnabled(undo_stack_.canRedo());
    QAction* action = menu.exec(event->globalPosition().toPoint());
    if (action == copy)
      copySelection();
    else if (action == paste)
      pasteClipboard();
    else if (action == zero)
      zeroSelection();
    else if (action == all)
      selectAll();
    else if (action == undo)
      undo_stack_.undo();
    else if (action == redo)
      undo_stack_.redo();
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton) {
    QAbstractScrollArea::mousePressEvent(event);
    return;
  }
  Pane clicked_pane = pane_;
  const int index = indexAt(event->position().toPoint(), &clicked_pane, false);
  if (index < 0)
    return;
  setFocus(Qt::MouseFocusReason);
  resetNibble();
  pane_ = clicked_pane;
  const bool extend = event->modifiers().testFlag(Qt::ShiftModifier);
  const int clicked_x = event->position().toPoint().x() + horizontalScrollBar()->value();
  setCursor(index, extend);
  if (!extend && pane_ == Pane::Hex) {
    nibble_ = clicked_x >= hexCellX(index % kBytesPerRow) + char_width_ / 2 + char_width_ ? 1 : 0;
  }
  dragging_ = true;
  drag_position_ = event->position().toPoint();
  drag_timer_.start();
  viewport()->update();
  event->accept();
}

void MemoryView::mouseMoveEvent(QMouseEvent* event)
{
  if (!dragging_) {
    QAbstractScrollArea::mouseMoveEvent(event);
    return;
  }
  drag_position_ = event->position().toPoint();
  updateDrag();
  event->accept();
}

void MemoryView::updateDrag()
{
  if (!dragging_)
    return;
  int scroll = 0;
  if (drag_position_.y() < header_height_)
    scroll = -qMin(8, 1 + (header_height_ - drag_position_.y()) / row_height_);
  else if (drag_position_.y() >= viewport()->height())
    scroll = qMin(8, 1 + (drag_position_.y() - viewport()->height()) / row_height_);
  verticalScrollBar()->setValue(verticalScrollBar()->value() + scroll);
  if (drag_position_.x() < 0)
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - char_width_ * 3);
  else if (drag_position_.x() >= viewport()->width())
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + char_width_ * 3);
  QPoint position = drag_position_;
  position.setY(qBound(header_height_, position.y(),
                       qMax(header_height_, viewport()->height() - 1)));
  Pane target = pane_;
  const int index = indexAt(position, &target, true);
  if (index != cursor_) {
    resetNibble();
    setCursor(index, true);
  }
}

void MemoryView::stopDragging()
{
  dragging_ = false;
  drag_timer_.stop();
}

void MemoryView::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton && dragging_) {
    drag_position_ = event->position().toPoint();
    updateDrag();
    stopDragging();
    event->accept();
    return;
  }
  QAbstractScrollArea::mouseReleaseEvent(event);
}

void MemoryView::copySelection()
{
  const int start = selectionStart();
  const int length = selectionLength();
  if (start < 0)
    return;
  QString hex;
  hex.reserve(qsizetype(length) * 3 - 1);
  for (int i = 0; i < length; ++i) {
    if (i)
      hex.append(QLatin1Char(' '));
    const quint8 byte = memory_data_.at(start + i);
    hex.append(QLatin1Char(kHexDigits[byte >> 4]));
    hex.append(QLatin1Char(kHexDigits[byte & 15]));
  }
  QApplication::clipboard()->setText(hex);
}

void MemoryView::pasteClipboard()
{
  resetNibble();
  const int start = selectionStart();
  if (start < 0)
    return;
  const QString text = QApplication::clipboard()->text();
  QVector<quint8> bytes;
  const int available = int(memory_data_.size()) - start;
  if (pane_ == Pane::Hex) {
    int high = -1;
    bytes.reserve(qMin(qsizetype(available), text.size() / 2));
    for (QChar character : text) {
      if (character.isSpace())
        continue;
      const int digit = hexDigit(character);
      if (digit < 0) {
        QMessageBox::warning(this, tr("Cannot Paste Hex"),
                             tr("Use only hexadecimal digits (0–9, A–F) and whitespace. "
                                "Prefixes such as 0x are not accepted."));
        return;
      }
      if (high < 0)
        high = digit;
      else {
        if (bytes.size() >= available) {
          QMessageBox::warning(this, tr("Cannot Paste"),
                               tr("The pasted bytes would extend beyond the end of the file. "
                                  "This editor only overwrites existing bytes."));
          return;
        }
        bytes.append(quint8((high << 4) | digit));
        high = -1;
      }
    }
    if (high >= 0) {
      QMessageBox::warning(this, tr("Cannot Paste Hex"),
                           tr("Hexadecimal input must contain two digits for every byte."));
      return;
    }
  }
  else {
    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() > available) {
      QMessageBox::warning(this, tr("Cannot Paste"),
                           tr("The UTF-8 text would extend beyond the end of the file. "
                              "This editor only overwrites existing bytes."));
      return;
    }
    bytes.reserve(utf8.size());
    for (char byte : utf8)
      bytes.append(quint8(static_cast<unsigned char>(byte)));
  }
  if (bytes.isEmpty())
    return;
  overwrite(start, bytes, tr("Paste Bytes"));
  selectRange(start, int(bytes.size()));
}

void MemoryView::zeroSelection()
{
  resetNibble();
  int start = selectionStart();
  int end = start + selectionLength();
  if (start < 0)
    return;
  while (start < end && memory_data_.at(start) == 0)
    ++start;
  while (end > start && memory_data_.at(end - 1) == 0)
    --end;
  if (start < end)
    overwrite(start, QVector<quint8>(end - start, 0), tr("Zero Selection"));
}

void MemoryView::selectAll()
{
  selectRange(0, int(memory_data_.size()));
}

void MemoryView::overwrite(int start, const QVector<quint8>& bytes,
                           const QString& description, quint64 token)
{
  if (start < 0 || start > memory_data_.size() ||
      bytes.size() > memory_data_.size() - start)
    return;
  int first = 0;
  int last = int(bytes.size());
  while (first < last && bytes[first] == memory_data_.at(start + first))
    ++first;
  while (last > first && bytes[last - 1] == memory_data_.at(start + last - 1))
    --last;
  if (first == last)
    return;
  QVector<quint8> before(last - first);
  QVector<quint8> after(last - first);
  std::copy_n(memory_data_.constData() + start + first, last - first, before.data());
  std::copy_n(bytes.constData() + first, last - first, after.data());
  pushing_command_ = true;
  undo_stack_.push(new ByteEditCommand(this, start + first, std::move(before),
                                      std::move(after), description, token));
  pushing_command_ = false;
}

void MemoryView::applyBytes(int start, const QVector<quint8>& bytes)
{
  std::copy(bytes.cbegin(), bytes.cend(), memory_data_.begin() + start);
  viewport()->update();
  emit dataChanged();
}

void MemoryView::enterHexDigit(int digit)
{
  if (selectionLength() != 1)
    return;
  const int index = cursor_;
  const quint8 original = memory_data_.at(index);
  const bool low = nibble_ == 1;
  if (!low)
    ++edit_token_;
  const quint8 byte = low ? quint8((original & 0xF0) | digit)
                         : quint8((digit << 4) | (original & 0x0F));
  overwrite(index, QVector<quint8>{byte}, tr("Edit Byte"), edit_token_);
  if (low) {
    resetNibble();
    if (index + 1 < memory_data_.size())
      setCursor(index + 1, false);
  }
  else {
    nibble_ = 1;
  }
  viewport()->update();
}

void MemoryView::enterAscii(quint8 byte)
{
  if (selectionLength() != 1)
    return;
  const int index = cursor_;
  resetNibble();
  overwrite(index, QVector<quint8>{byte}, tr("Edit ASCII Byte"));
  if (index + 1 < memory_data_.size())
    setCursor(index + 1, false);
}

void MemoryView::keyPressEvent(QKeyEvent* event)
{
  if (event->matches(QKeySequence::Copy)) {
    resetNibble();
    copySelection();
  }
  else if (event->matches(QKeySequence::Paste)) {
    pasteClipboard();
  }
  else if (event->matches(QKeySequence::SelectAll)) {
    selectAll();
  }
  else if (event->matches(QKeySequence::Undo)) {
    resetNibble();
    undo_stack_.undo();
  }
  else if (event->matches(QKeySequence::Redo)) {
    resetNibble();
    undo_stack_.redo();
  }
  else if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
    resetNibble();
    pane_ = pane_ == Pane::Hex ? Pane::Ascii : Pane::Hex;
    ensureCursorVisible();
  }
  else {
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool command = modifiers.testFlag(Qt::ControlModifier) ||
                         modifiers.testFlag(Qt::MetaModifier);
    const bool extend = modifiers.testFlag(Qt::ShiftModifier);
    const int index = cursor_ < 0 ? 0 : cursor_;
    qint64 destination = index;
    bool navigation = !modifiers.testFlag(Qt::AltModifier);
    switch (event->key()) {
      case Qt::Key_Left: destination = qint64(index) - 1; break;
      case Qt::Key_Right: destination = qint64(index) + 1; break;
      case Qt::Key_Up: destination = qint64(index) - kBytesPerRow; break;
      case Qt::Key_Down: destination = qint64(index) + kBytesPerRow; break;
      case Qt::Key_PageUp: destination = qint64(index) - qint64(visibleRows()) * kBytesPerRow; break;
      case Qt::Key_PageDown: destination = qint64(index) + qint64(visibleRows()) * kBytesPerRow; break;
      case Qt::Key_Home: destination = command ? 0 : index - index % kBytesPerRow; break;
      case Qt::Key_End:
        destination = command ? qint64(memory_data_.size()) - 1
                              : qint64(index - index % kBytesPerRow) + kBytesPerRow - 1;
        break;
      default: navigation = false; break;
    }
    if (navigation && !memory_data_.isEmpty()) {
      resetNibble();
      setCursor(int(qBound(qint64(0), destination, qint64(memory_data_.size() - 1))), extend);
    }
    else if (!command && !modifiers.testFlag(Qt::AltModifier) &&
             event->text().size() == 1 && selectionLength() == 1) {
      const QChar character = event->text().at(0);
      if (pane_ == Pane::Hex && hexDigit(character) >= 0)
        enterHexDigit(hexDigit(character));
      else if (pane_ == Pane::Ascii && character.unicode() >= 32 &&
               character.unicode() <= 126)
        enterAscii(quint8(character.unicode()));
      else {
        QAbstractScrollArea::keyPressEvent(event);
        return;
      }
    }
    else {
      QAbstractScrollArea::keyPressEvent(event);
      return;
    }
  }
  event->accept();
}
