#ifndef MEMORYVIEW_H
#define MEMORYVIEW_H

#include <QAbstractScrollArea>
#include <QPoint>
#include <QTimer>
#include <QUndoStack>
#include <QVector>
#include <cstdint>

class HeatMap;
class ByteEditCommand;

class MemoryView : public QAbstractScrollArea {
  Q_OBJECT

 public:
  explicit MemoryView(QWidget* parent, HeatMap* heat_map);
  ~MemoryView() override;
  void setData(QVector<quint8> data);
  const QVector<quint8>& getData() const;
  void moveToPos(uint64_t pos, bool select_byte = false);
  int selectionStart() const;
  int selectionLength() const;
  void selectRange(int start, int length);
  QUndoStack* undoStack();
  bool isModified() const;
  void markSaved();
  void setTerminalTheme(bool enabled);

 public slots:
  void copySelection();
  void pasteClipboard();
  void zeroSelection();
  void selectAll();

 signals:
  void selectionChanged();
  void dataChanged();
  void modifiedChanged(bool modified);

 protected:
  QSize sizeHint() const override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  bool focusNextPrevChild(bool next) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;

 private:
  friend class ByteEditCommand;
  enum class Pane { Hex, Ascii };
  static constexpr int kBytesPerRow = 16;

  QVector<quint8> memory_data_;
  HeatMap* heat_map_;
  QUndoStack undo_stack_;
  QTimer drag_timer_;
  int anchor_ = -1;
  int cursor_ = -1;
  Pane pane_ = Pane::Hex;
  int nibble_ = 0;
  quint64 edit_token_ = 0;
  bool pushing_command_ = false;
  bool dragging_ = false;
  bool terminal_theme_ = false;
  QPoint drag_position_;
  int char_width_ = 1;
  int row_height_ = 1;
  int header_height_ = 1;
  int hex_start_ = 0;
  int ascii_start_ = 0;
  int content_width_ = 0;

  void updateMetrics();
  void updateScrollBars();
  void updateOverview();
  int visibleRows() const;
  int hexCellX(int column) const;
  int indexAt(const QPoint& position, Pane* pane, bool clamp) const;
  void setCursor(int index, bool extend);
  void ensureCursorVisible();
  void resetNibble();
  void updateDrag();
  void stopDragging();
  void overwrite(int start, const QVector<quint8>& bytes,
                 const QString& description, quint64 token = 0);
  void applyBytes(int start, const QVector<quint8>& bytes);
  void enterHexDigit(int digit);
  void enterAscii(quint8 byte);
};

#endif  // MEMORYVIEW_H
