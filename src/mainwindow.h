#pragma once

#include <QMainWindow>
#include <QDateTime>

class MemoryView;
class HeatMap;
class QLabel;
class QLineEdit;
class QComboBox;
class QTreeWidget;
class QSplitter;
class QMenu;
class QAction;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  bool openFile(const QString& path);
  bool saveFile(const QString& path);

 protected:
  void closeEvent(QCloseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  void chooseFile();
  bool save(bool saveAs = false);
  bool maybeSave();
  void goToOffset();
  void find(bool backwards = false);
  void showFind();
  void updateDocument();
  void updateSelection();
  void updateOverview();
  void updateRecentFiles();
  void rememberFile(const QString& path);

  MemoryView* memory_view_;
  HeatMap* heat_map_;
  QSplitter* splitter_;
  QLabel* overview_legend_;
  QLabel* file_label_;
  QLabel* detail_label_;
  QLabel* selection_label_;
  QLabel* inspector_;
  QLabel* search_status_;
  QWidget* find_bar_;
  QLineEdit* search_input_;
  QComboBox* search_mode_;
  QComboBox* byte_order_;
  QTreeWidget* regions_;
  QMenu* recent_menu_;
  QAction* save_action_;
  QAction* save_as_action_;
  QAction* next_action_;
  QAction* previous_action_;
  QString path_;
  QString last_query_;
  int last_search_mode_ = -1;
  int last_match_ = -1;
  bool document_open_ = false;
  QDateTime disk_modified_;
  qint64 disk_size_ = 0;
};
