#include "mainwindow.h"
#include "memoryview.h"
#include "heatmap.h"
#include "elf_map.h"
#include "macho_map.h"
#include "native_vibrancy.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPainter>
#include <QSet>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleHints>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtEndian>
#include <cstring>
#include <memory>

namespace {
constexpr qint64 MaxFileSize = 256 * 1024 * 1024;
QString offsetText(quint64 offset) {
  return QString("0x%1").arg(offset, 8, 16, QChar('0')).toUpper().replace("0X", "0x");
}
QString fileKind(const QVector<quint8>& data) {
  if (MachoMap::isSupported(data)) return "Mach-O executable";
  if (ElfMap::isSupported(data)) return "ELF executable";
  return "Binary file";
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
#ifdef Q_OS_MACOS
  // Qt must know this before the first native window handle is requested.
  setAttribute(Qt::WA_TranslucentBackground);
#endif
  setAutoFillBackground(false);
  setWindowTitle("DeepHex");
  setAcceptDrops(true);
  resize(1180, 760);
  setMinimumSize(780, 480);

  auto* fileMenu = menuBar()->addMenu(tr("&File"));
  auto* openAction = fileMenu->addAction(tr("&Open…"), QKeySequence::Open, this, &MainWindow::chooseFile);
  openAction->setObjectName("openFile");
  recent_menu_ = fileMenu->addMenu(tr("Open Recent"));
  fileMenu->addSeparator();
  save_action_ = fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, [this] { save(); });
  save_action_->setObjectName("saveFile");
  save_as_action_ = fileMenu->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, [this] { save(true); });
  fileMenu->addSeparator();
  fileMenu->addAction(tr("Close Window"), QKeySequence::Close, this, &QWidget::close);
  fileMenu->addAction(tr("Quit DeepHex"), QKeySequence::Quit, qApp, &QApplication::closeAllWindows);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(20, 16, 20, 12);
  layout->setSpacing(12);
  setCentralWidget(central);
  auto* heading = new QHBoxLayout;
  auto* titles = new QVBoxLayout;
  file_label_ = new QLabel(tr("Open a file to explore"));
  QFont titleFont = font();
  titleFont.setPointSize(titleFont.pointSize() + 5);
  titleFont.setBold(true);
  file_label_->setFont(titleFont);
  file_label_->setTextFormat(Qt::PlainText);
  detail_label_ = new QLabel(tr("Drop a file here, or press ⌘O. Changes overwrite bytes without shifting offsets."));
  detail_label_->setTextFormat(Qt::PlainText);
  detail_label_->setForegroundRole(QPalette::PlaceholderText);
  titles->addWidget(file_label_);
  titles->addWidget(detail_label_);
  heading->addLayout(titles, 1);
  auto* openButton = new QPushButton(tr("Open File…"));
  connect(openButton, &QPushButton::clicked, this, &MainWindow::chooseFile);
  heading->addWidget(openButton);
  layout->addLayout(heading);

  heat_map_ = new HeatMap(this);
  heat_map_->setToolTip(tr("File overview · click or drag to navigate"));
  layout->addWidget(heat_map_);
  overview_legend_ = new QLabel;
  overview_legend_->setObjectName("overviewLegend");
  overview_legend_->setTextFormat(Qt::RichText);
  overview_legend_->setWordWrap(true);
  overview_legend_->setForegroundRole(QPalette::PlaceholderText);
  overview_legend_->setToolTip(tr("Colors describe file contents, not edits. Distribution bins are zero-filled or ASCII when at least half their bytes match; otherwise they are binary. The outlined marker shows the visible bytes."));
  overview_legend_->hide();
  layout->addWidget(overview_legend_);

  find_bar_ = new QWidget;
  find_bar_->setObjectName("findBar");
  auto* findLayout = new QHBoxLayout(find_bar_);
  findLayout->setContentsMargins(0, 0, 0, 0);
  search_mode_ = new QComboBox;
  search_mode_->setObjectName("searchMode");
  search_mode_->addItems({tr("Hex bytes"), tr("UTF-8 text")});
  search_input_ = new QLineEdit;
  search_input_->setObjectName("searchInput");
  search_input_->setPlaceholderText(tr("e.g. 48 65 6C 6C 6F"));
  search_input_->setClearButtonEnabled(true);
  search_status_ = new QLabel;
  search_status_->setObjectName("searchStatus");
  search_status_->setTextFormat(Qt::PlainText);
  auto* prevButton = new QPushButton(tr("Previous"));
  auto* nextButton = new QPushButton(tr("Next"));
  auto* closeFind = new QPushButton(tr("Done"));
  findLayout->addWidget(search_mode_);
  findLayout->addWidget(search_input_, 1);
  findLayout->addWidget(search_status_);
  findLayout->addWidget(prevButton);
  findLayout->addWidget(nextButton);
  findLayout->addWidget(closeFind);
  connect(prevButton, &QPushButton::clicked, this, [this] { find(true); });
  connect(nextButton, &QPushButton::clicked, this, [this] { find(); });
  connect(search_input_, &QLineEdit::returnPressed, this, [this] { find(); });
  connect(closeFind, &QPushButton::clicked, this, [this] { find_bar_->hide(); memory_view_->setFocus(); });
  connect(search_mode_, &QComboBox::currentIndexChanged, this, [this](int mode) {
    search_input_->setPlaceholderText(mode == 0 ? tr("e.g. 48 65 6C 6C 6F") : tr("Case-sensitive UTF-8 text"));
    search_status_->clear();
  });
  connect(search_input_, &QLineEdit::textChanged, search_status_, &QLabel::clear);
  layout->addWidget(find_bar_);
  find_bar_->hide();

  splitter_ = new QSplitter;
  memory_view_ = new MemoryView(this, heat_map_);
  memory_view_->setObjectName("hexEditor");
  heat_map_->setMemoryView(memory_view_);
  splitter_->addWidget(memory_view_);
  auto* sidebar = new QWidget;
  auto* sideLayout = new QVBoxLayout(sidebar);
  sideLayout->setContentsMargins(12, 0, 0, 0);
  auto* inspectorTitle = new QLabel(tr("DATA INSPECTOR"));
  QFont smallBold = font();
  smallBold.setBold(true);
  inspectorTitle->setFont(smallBold);
  sideLayout->addWidget(inspectorTitle);
  byte_order_ = new QComboBox;
  byte_order_->addItems({tr("Little endian"), tr("Big endian")});
  byte_order_->setObjectName("byteOrder");
  sideLayout->addWidget(byte_order_);
  inspector_ = new QLabel;
  inspector_->setObjectName("dataInspector");
  inspector_->setTextFormat(Qt::RichText);
  inspector_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  inspector_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  inspector_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  inspector_scroll_ = new QScrollArea;
  inspector_scroll_->setObjectName("inspectorScroll");
  inspector_scroll_->setWidgetResizable(true);
  inspector_scroll_->setFrameShape(QFrame::NoFrame);
  inspector_scroll_->setMinimumHeight(120);
  inspector_scroll_->setWidget(inspector_);
  sideLayout->addWidget(inspector_scroll_, 2);
  auto* regionTitle = new QLabel(tr("FILE REGIONS"));
  regionTitle->setFont(smallBold);
  sideLayout->addWidget(regionTitle);
  regions_ = new QTreeWidget;
  regions_->setObjectName("fileRegions");
  regions_->setHeaderLabels({tr("Region"), tr("Offset")});
  regions_->setRootIsDecorated(false);
  regions_->setAlternatingRowColors(true);
  regions_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  regions_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  sideLayout->addWidget(regions_, 1);
  sidebar->setMinimumWidth(240);
  splitter_->addWidget(sidebar);
  splitter_->setStretchFactor(0, 1);
  splitter_->setStretchFactor(1, 0);
  splitter_->setSizes({850, 270});
  layout->addWidget(splitter_, 1);
  connect(regions_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
    memory_view_->moveToPos(item->data(0, Qt::UserRole).toULongLong(), true);
    memory_view_->setFocus();
  });
  connect(byte_order_, &QComboBox::currentIndexChanged, this, &MainWindow::updateSelection);
  connect(memory_view_, &MemoryView::selectionChanged, this, &MainWindow::updateSelection);
  connect(memory_view_, &MemoryView::modifiedChanged, this, &MainWindow::updateDocument);
  // Coalesce a burst of edits; parsing happens outside the key event, not in paintEvent.
  auto* overviewTimer = new QTimer(this);
  overviewTimer->setSingleShot(true);
  overviewTimer->setInterval(200);
  connect(overviewTimer, &QTimer::timeout, this, &MainWindow::updateOverview);
  connect(memory_view_, &MemoryView::dataChanged, this, [this, overviewTimer] {
    updateSelection();
    search_status_->clear();
    last_match_ = -1;
    overviewTimer->start();
  });

  auto* editMenu = menuBar()->addMenu(tr("&Edit"));
  auto* undoAction = memory_view_->undoStack()->createUndoAction(this, tr("Undo"));
  undoAction->setShortcut(QKeySequence::Undo);
  editMenu->addAction(undoAction);
  auto* redoAction = memory_view_->undoStack()->createRedoAction(this, tr("Redo"));
  redoAction->setShortcut(QKeySequence::Redo);
  editMenu->addAction(redoAction);
  for (auto* action : {undoAction, redoAction}) {
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    memory_view_->addAction(action);
  }
  editMenu->addSeparator();
  auto* copyAction = editMenu->addAction(tr("Copy as Hex"), QKeySequence::Copy, memory_view_, &MemoryView::copySelection);
  auto* pasteAction = editMenu->addAction(tr("Paste / Overwrite"), QKeySequence::Paste, memory_view_, &MemoryView::pasteClipboard);
  auto* allAction = editMenu->addAction(tr("Select All"), QKeySequence::SelectAll, memory_view_, &MemoryView::selectAll);
  // The active text field must retain standard editing shortcuts during search/dialog input.
  for (auto* action : {copyAction, pasteAction, allAction}) {
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    memory_view_->addAction(action);
  }
  auto* zeroAction = editMenu->addAction(tr("Zero Selected Bytes"), memory_view_, &MemoryView::zeroSelection);
  connect(memory_view_, &MemoryView::selectionChanged, this, [this, copyAction, pasteAction, zeroAction, allAction] {
    const bool selected = memory_view_->selectionLength() > 0;
    copyAction->setEnabled(selected);
    pasteAction->setEnabled(selected);
    zeroAction->setEnabled(selected);
    allAction->setEnabled(!memory_view_->getData().isEmpty());
  });
  copyAction->setEnabled(false);
  pasteAction->setEnabled(false);
  zeroAction->setEnabled(false);
  allAction->setEnabled(false);
  editMenu->addSeparator();
  auto* findAction = editMenu->addAction(tr("Find…"), QKeySequence::Find, this, &MainWindow::showFind);
  next_action_ = editMenu->addAction(tr("Find Next"), QKeySequence::FindNext, this, [this] { find(); });
  previous_action_ = editMenu->addAction(tr("Find Previous"), QKeySequence::FindPrevious, this, [this] { find(true); });
  auto* gotoAction = editMenu->addAction(tr("Go to Offset…"), QKeySequence("Ctrl+L"), this, &MainWindow::goToOffset);
  gotoAction->setObjectName("goToOffset");
  auto* viewMenu = menuBar()->addMenu(tr("&View"));
  auto* inspectorAction = viewMenu->addAction(tr("Show Inspector"));
  inspectorAction->setCheckable(true);
  inspectorAction->setChecked(true);
  connect(inspectorAction, &QAction::toggled, sidebar, &QWidget::setVisible);
  auto* appearance = viewMenu->addMenu(tr("Appearance"));
  auto* appearanceGroup = new QActionGroup(this);
  const QStringList schemeNames = {tr("System"), tr("Light"), tr("Dark"), tr("Terminal")};
  const QStringList schemeObjects = {"appearanceSystem", "appearanceLight", "appearanceDark", "appearanceTerminal"};
  QSettings settings;
  const int storedScheme = qBound(0, settings.value("appearance", 0).toInt(), 3);
  transparency_preferred_ = settings.value("terminalTransparency", true).toBool();
  for (int i = 0; i < schemeNames.size(); ++i) {
    auto* action = appearance->addAction(schemeNames[i]);
    action->setObjectName(schemeObjects[i]);
    appearanceGroup->addAction(action);
    action->setCheckable(true);
    action->setChecked(i == storedScheme);
    connect(action, &QAction::triggered, this, [this, i] {
      QSettings().setValue("appearance", i);
      setAppearance(i);
    });
  }
  appearance->addSeparator();
  transparency_action_ = appearance->addAction(tr("Translucent Background"));
  transparency_action_->setObjectName("terminalTransparency");
  transparency_action_->setCheckable(true);
  transparency_action_->setChecked(transparency_preferred_);
  connect(transparency_action_, &QAction::toggled, this, [this](bool enabled) {
    transparency_preferred_ = enabled;
    QSettings().setValue("terminalTransparency", enabled);
    vibrancy_->setEnabled(appearance_ == 3 && enabled);
    applyAppearance();
  });
  connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
    // The platform updates its palette after announcing a new color scheme.
    QTimer::singleShot(0, this, &MainWindow::applyAppearance);
  });

  auto* toolbar = toolbar_ = addToolBar(tr("Editor"));
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
  toolbar->addAction(openAction);
  toolbar->addAction(save_action_);
  toolbar->addSeparator();
  toolbar->addAction(undoAction);
  toolbar->addAction(redoAction);
  toolbar->addSeparator();
  toolbar->addAction(findAction);
  toolbar->addAction(gotoAction);
  selection_label_ = new QLabel;
  statusBar()->addWidget(selection_label_, 1);
  statusBar()->addPermanentWidget(new QLabel(tr("OVERWRITE  ·  HEX / ASCII")));
  vibrancy_ = new NativeVibrancy(this);
  connect(vibrancy_, &NativeVibrancy::activeChanged, this, &MainWindow::applyAppearance);
  setAppearance(storedScheme);
  restoreGeometry(settings.value("windowGeometry").toByteArray());
  splitter_->restoreState(settings.value("splitterState").toByteArray());
  updateRecentFiles();
  updateDocument();
  updateSelection();
}

MainWindow::~MainWindow()
{
  // Restore the native content view before QWidget starts tearing down its layout.
  delete vibrancy_;
}

void MainWindow::setAppearance(int appearance)
{
  appearance_ = appearance;
  const Qt::ColorScheme scheme = appearance == 0 ? Qt::ColorScheme::Unknown
                              : appearance == 1 ? Qt::ColorScheme::Light
                                                : Qt::ColorScheme::Dark;
  qApp->styleHints()->setColorScheme(scheme);
  transparency_action_->setEnabled(appearance == 3 && vibrancy_->isSupported());
  vibrancy_->setEnabled(appearance == 3 && transparency_preferred_);
  applyAppearance();
}

void MainWindow::applyAppearance()
{
  const bool terminal = appearance_ == 3;
  const bool translucent = terminal && vibrancy_->isActive();
  memory_view_->setTerminalTheme(terminal);
  auto applyContentPalette = [this](const QPalette& colors) {
    setPalette(colors);
    // Stylesheet boundaries do not inherit explicit palette roles automatically.
    const auto applyTree = [&colors](const auto& self, QObject* object) -> void {
      if (auto* widget = qobject_cast<QWidget*>(object))
        widget->setPalette(colors);
      for (QObject* child : object->children())
        self(self, child);
    };
    for (QWidget* root : {centralWidget(), static_cast<QWidget*>(toolbar_),
                         static_cast<QWidget*>(statusBar())})
      applyTree(applyTree, root);
  };
  if (terminal) {
    QPalette colors;
    QColor window("#15161c");
    window.setAlpha(translucent ? 64 : 255);
    QColor base("#15161c");
    // The root tint already covers the grid; only add a light row tint here.
    base.setAlpha(translucent ? 18 : 255);
    QColor alternate("#1b1c23");
    alternate.setAlpha(base.alpha());
    colors.setColor(QPalette::Window, window);
    colors.setColor(QPalette::Base, base);
    colors.setColor(QPalette::AlternateBase, alternate);
    colors.setColor(QPalette::WindowText, QColor("#d6d8e2"));
    colors.setColor(QPalette::Text, QColor("#d6d8e2"));
    QColor button("#252731");
    button.setAlpha(translucent ? 96 : 255);
    colors.setColor(QPalette::Button, button);
    colors.setColor(QPalette::ButtonText, QColor("#d6d8e2"));
    colors.setColor(QPalette::PlaceholderText, QColor("#727784"));
    colors.setColor(QPalette::Link, QColor("#20b8df"));
    colors.setColor(QPalette::LinkVisited, QColor("#83dcd2"));
    colors.setColor(QPalette::Highlight, QColor("#23617a"));
    colors.setColor(QPalette::HighlightedText, QColor("#f5f7ff"));
    colors.setColor(QPalette::Accent, QColor("#20b8df"));
    colors.setColor(QPalette::Light, QColor("#414350"));
    colors.setColor(QPalette::Midlight, QColor("#343643"));
    colors.setColor(QPalette::Mid, QColor("#51445f"));
    colors.setColor(QPalette::Dark, QColor("#111218"));
    colors.setColor(QPalette::Shadow, QColor("#111218"));
    colors.setColor(QPalette::ToolTipBase, QColor("#252731"));
    colors.setColor(QPalette::ToolTipText, QColor("#d6d8e2"));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
      colors.setColor(QPalette::Disabled, role, QColor("#727784"));
    const QString controlBackground = translucent
        ? QStringLiteral("rgba(37,39,49,96)") : QStringLiteral("#252731");
    // Scope widget rules to the editor, not native menus or modal dialogs.
    centralWidget()->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; }"
        "QWidget#findBar { background: transparent; }"
        "QAbstractScrollArea#hexEditor { border: 1px solid #51445f; }"
        "QScrollArea#inspectorScroll { background: transparent; border: none; }"
        "QTreeWidget#fileRegions { background: transparent;"
        " alternate-background-color: rgba(214,216,226,6); border: 1px solid #51445f; }"
        "QHeaderView::section { background: transparent; color: #20b8df;"
        " border: none; border-bottom: 1px solid #51445f; padding: 4px; }"
        "QLineEdit, QComboBox, QPushButton { color: #d6d8e2;"
        " background-color: %1; border: 1px solid #51445f;"
        " border-radius: 4px; padding: 4px 7px; }"
        "QLineEdit:focus, QComboBox:focus, QPushButton:focus { border-color: #20b8df; }"
        "QPushButton:pressed { background-color: #343643; }"
        "QSplitter::handle { background: transparent; }").arg(controlBackground));
    applyContentPalette(colors);
    QPalette regionColors = colors;
    regionColors.setColor(QPalette::Window, Qt::transparent);
    regionColors.setColor(QPalette::Base, Qt::transparent);
    regionColors.setColor(QPalette::AlternateBase, QColor(214, 216, 226, 6));
    regions_->setPalette(regionColors);
    regions_->viewport()->setPalette(regionColors);
    QPalette chromeColors = colors;
    chromeColors.setColor(QPalette::Window, Qt::transparent);
    chromeColors.setColor(QPalette::Base, Qt::transparent);
    inspector_scroll_->setPalette(chromeColors);
    inspector_scroll_->viewport()->setPalette(chromeColors);
    regions_->header()->setPalette(chromeColors);
    statusBar()->setStyleSheet(QStringLiteral(
        "QStatusBar { background: transparent; border-top: 1px solid #51445f; }"
        "QStatusBar::item { border: none; }"));
    toolbar_->setStyleSheet(QStringLiteral(
        "QToolBar { background: transparent; border: none;"
        " border-bottom: 1px solid #51445f; spacing: 6px; }"
        "QToolBar::separator { background: #51445f; width: 1px; margin: 5px; }"
        "QToolButton { color: #20b8df; background: transparent;"
        " border: none; padding: 5px; }"
        "QToolButton:disabled { color: #727784; }"
        "QToolButton:hover { background: #252731; }"));
  } else {
    centralWidget()->setStyleSheet(QString());
    statusBar()->setStyleSheet(QString());
    toolbar_->setStyleSheet(QString());
    // An unresolved palette follows the platform again; never copy a stale
    // Terminal palette into the application or keep its explicit color roles.
    applyContentPalette(QPalette());
  }
  centralWidget()->setAutoFillBackground(false);
  inspector_->setAutoFillBackground(!terminal);
  inspector_scroll_->viewport()->setAutoFillBackground(!terminal);
  // The editor paints Base itself. A second viewport fill would hide the blur.
  memory_view_->viewport()->setAutoFillBackground(false);
  memory_view_->viewport()->setAttribute(Qt::WA_OpaquePaintEvent, !translucent);
  regions_->viewport()->setAttribute(Qt::WA_OpaquePaintEvent, !terminal);
  updateSelection();
  updateOverview();
  update();
}

void MainWindow::paintEvent(QPaintEvent* event)
{
  QMainWindow::paintEvent(event);
  // One root tint only; central/sidebar widgets propagate this background.
  // The macOS translucent surface also needs an opaque fill outside Terminal.
  if (appearance_ == 3 || testAttribute(Qt::WA_TranslucentBackground)) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
  }
}

void MainWindow::chooseFile()
{
  const QString file = QFileDialog::getOpenFileName(this, tr("Open Binary File"), path_);
  if (!file.isEmpty()) openFile(file);
}

bool MainWindow::openFile(const QString& path)
{
  if (!maybeSave()) return false;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("Cannot Open File"), file.errorString());
    return false;
  }
  const qint64 size = file.size();
  if (size < 0 || size > MaxFileSize) {
    QMessageBox::warning(this, tr("File Too Large"), tr("This in-memory editor supports files up to 256 MiB."));
    return false;
  }
  QVector<quint8> data;
  data.resize(size);
  QProgressDialog progress(tr("Opening file…"), tr("Cancel"), 0, 100, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(500);
  qint64 read = 0;
  while (read < size) {
    const qint64 count = file.read(reinterpret_cast<char*>(data.data()) + read, qMin<qint64>(1024 * 1024, size - read));
    if (count <= 0) {
      QMessageBox::warning(this, tr("Cannot Read File"), count < 0 ? file.errorString() : tr("The file changed while it was being read."));
      return false;
    }
    read += count;
    progress.setValue(int(read * 100 / size));
    if (progress.wasCanceled()) return false;
  }
  progress.setValue(100);
  path_ = QFileInfo(path).absoluteFilePath();
  document_open_ = true;
  disk_modified_ = QFileInfo(path_).lastModified();
  disk_size_ = size;
  memory_view_->setData(std::move(data));
  search_status_->clear();
  updateOverview();
  updateDocument();
  updateSelection();
  rememberFile(path_);
  memory_view_->setFocus();
  return true;
}

bool MainWindow::saveFile(const QString& path)
{
  if (!document_open_) return false;
  const QString target = QFileInfo(path).absoluteFilePath();
  if (target == path_) {
    QFileInfo info(path_);
    if (!info.exists() || info.size() != disk_size_ || info.lastModified() != disk_modified_) {
      if (QMessageBox::warning(this, tr("File Changed on Disk"), tr("The original file changed outside DeepHex. Replace it with your edited bytes?"), QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Save)
        return false;
    }
  }
  QSaveFile file(target);
  if (!file.open(QIODevice::WriteOnly)) {
    QMessageBox::warning(this, tr("Cannot Save File"), file.errorString());
    return false;
  }
  const auto& data = memory_view_->getData();
  qint64 written = 0;
  while (written < data.size()) {
    qint64 count = file.write(reinterpret_cast<const char*>(data.constData()) + written, qMin<qint64>(1024 * 1024, data.size() - written));
    if (count <= 0) {
      QMessageBox::warning(this, tr("Cannot Save File"), file.errorString());
      return false;
    }
    written += count;
  }
  if (!file.commit()) {
    QMessageBox::warning(this, tr("Cannot Save File"), file.errorString());
    return false;
  }
  path_ = target;
  disk_modified_ = QFileInfo(path_).lastModified();
  disk_size_ = data.size();
  memory_view_->markSaved();
  updateDocument();
  rememberFile(path_);
  statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path_).fileName()), 3000);
  return true;
}

bool MainWindow::save(bool saveAs)
{
  if (!document_open_) return false;
  QString file = path_;
  if (saveAs || file.isEmpty()) file = QFileDialog::getSaveFileName(this, tr("Save Binary File"), path_);
  return !file.isEmpty() && saveFile(file);
}

bool MainWindow::maybeSave()
{
  if (!memory_view_->isModified()) return true;
  const auto answer = QMessageBox::warning(this, tr("Unsaved Changes"), tr("Save changes to %1?").arg(QFileInfo(path_).fileName()), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (answer == QMessageBox::Cancel) return false;
  return answer != QMessageBox::Save || save();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  if (!maybeSave()) { event->ignore(); return; }
  QSettings settings;
  settings.setValue("windowGeometry", saveGeometry());
  settings.setValue("splitterState", splitter_->saveState());
  event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
  if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1 && event->mimeData()->urls().first().isLocalFile()) event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* event)
{
  const auto urls = event->mimeData()->urls();
  if (urls.size() == 1 && urls.first().isLocalFile() && openFile(urls.first().toLocalFile())) event->acceptProposedAction();
}

void MainWindow::showFind()
{
  find_bar_->show();
  search_input_->setFocus();
  search_input_->selectAll();
}

void MainWindow::find(bool backwards)
{
  find_bar_->show();
  QByteArray needle;
  if (search_mode_->currentIndex() == 1) needle = search_input_->text().toUtf8();
  else {
    QByteArray digits;
    for (QChar ch : search_input_->text()) {
      if (ch.isSpace()) continue;
      if (!((ch >= '0' && ch <= '9') || (ch.toLower() >= 'a' && ch.toLower() <= 'f'))) {
        search_status_->setText(tr("Use hex pairs (00–FF)"));
        return;
      }
      digits.append(ch.toLatin1());
    }
    if (digits.size() % 2) { search_status_->setText(tr("Incomplete hex pair")); return; }
    needle = QByteArray::fromHex(digits);
  }
  if (needle.isEmpty()) { search_status_->setText(tr("Enter a pattern")); return; }
  const auto& data = memory_view_->getData();
  const QByteArray bytes = QByteArray::fromRawData(reinterpret_cast<const char*>(data.constData()), data.size());
  const int cursor = memory_view_->selectionStart();
  const bool repeat = search_input_->text() == last_query_ &&
      search_mode_->currentIndex() == last_search_mode_ && cursor == last_match_;
  qsizetype found = -1;
  bool wrapped = false;
  if (backwards) {
    const int from = cursor - (repeat ? 1 : 0);
    if (from >= 0) found = bytes.lastIndexOf(needle, from);
    if (found < 0) { found = bytes.lastIndexOf(needle); wrapped = found >= 0; }
  } else {
    found = bytes.indexOf(needle, qMax(0, cursor + (repeat ? 1 : 0)));
    if (found < 0) { found = bytes.indexOf(needle); wrapped = found >= 0; }
  }
  if (found < 0) { search_status_->setText(tr("No match")); return; }
  memory_view_->selectRange(int(found), int(needle.size()));
  last_query_ = search_input_->text();
  last_search_mode_ = search_mode_->currentIndex();
  last_match_ = int(found);
  search_status_->setText(wrapped ? tr("Wrapped · %1").arg(offsetText(found)) : offsetText(found));
}

void MainWindow::goToOffset()
{
  bool ok;
  const QString input = QInputDialog::getText(this, tr("Go to Offset"), tr("Decimal offset, or hexadecimal prefixed with 0x:"), QLineEdit::Normal, offsetText(qMax(0, memory_view_->selectionStart())), &ok).trimmed();
  if (!ok) return;
  bool parsed;
  const quint64 offset = input.toULongLong(&parsed, input.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
  if (!parsed || offset >= quint64(memory_view_->getData().size())) {
    QMessageBox::warning(this, tr("Invalid Offset"), tr("Enter an offset within the file."));
    return;
  }
  memory_view_->moveToPos(offset, true);
  memory_view_->setFocus();
}

void MainWindow::updateDocument()
{
  const bool dirty = memory_view_->isModified();
  setWindowModified(dirty);
  setWindowFilePath(path_);
  setWindowTitle(document_open_ ? QFileInfo(path_).fileName() + "[*] — DeepHex" : "DeepHex");
  file_label_->setText(document_open_ ? QFileInfo(path_).fileName() : tr("Open a file to explore"));
  file_label_->setToolTip(path_);
  if (document_open_) detail_label_->setText(tr("%1  ·  %2 bytes  ·  %3").arg(fileKind(memory_view_->getData()), QLocale().toString(memory_view_->getData().size()), dirty ? tr("Unsaved changes") : tr("Saved")));
  save_action_->setEnabled(document_open_ && dirty);
  save_as_action_->setEnabled(document_open_);
  next_action_->setEnabled(document_open_);
  previous_action_->setEnabled(document_open_);
}

void MainWindow::updateSelection()
{
  const int start = memory_view_->selectionStart();
  const int length = memory_view_->selectionLength();
  if (start < 0 || !length) {
    selection_label_->setText(document_open_ ? tr("Empty file · 0 bytes") : tr("Ready"));
    inspector_->setText(tr("Select a byte to inspect its values.<br><br>Hex: type 0–9 / A–F<br>ASCII: click the text column<br>Tab: switch editing pane<br>Shift + arrows: extend selection"));
    return;
  }
  selection_label_->setText(tr("Offset %1  ·  %2 byte%3 selected").arg(offsetText(start)).arg(QLocale().toString(length), length == 1 ? "" : "s"));
  const auto& data = memory_view_->getData();
  const int available = int(data.size()) - start;
  const bool little = byte_order_->currentIndex() == 0;
  auto readValue = [&](int width) {
    quint64 value = 0;
    for (int i = 0; i < width; ++i) value |= quint64(data[start + i]) << ((little ? i : width - i - 1) * 8);
    return value;
  };
  QString html = "<table cellspacing='5'>";
  const QString valueStyle = appearance_ == 3 ? QStringLiteral(" style='color:#e5bd68'") : QString();
  auto row = [&](const QString& label, const QString& value) {
    html += "<tr><td>" + label.toHtmlEscaped() + "</td><td" + valueStyle + ">" +
            value.toHtmlEscaped() + "</td></tr>";
  };
  row(tr("Offset"), offsetText(start));
  row(tr("Binary"), QString("%1").arg(data[start], 8, 2, QChar('0')));
  for (int width : {1, 2, 4, 8}) {
    const QString suffix = QString::number(width * 8);
    if (available < width) { row("UInt" + suffix, "—"); row("Int" + suffix, "—"); continue; }
    quint64 value = readValue(width);
    qint64 signedValue;
    if (width == 8) std::memcpy(&signedValue, &value, sizeof(value));
    else signedValue = (value & (quint64(1) << (width * 8 - 1))) ? qint64(value) - (qint64(1) << (width * 8)) : qint64(value);
    row("UInt" + suffix, QString::number(value));
    row("Int" + suffix, QString::number(signedValue));
  }
  if (available >= 4) {
    quint32 bits = quint32(readValue(4));
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    row("Float32", QString::number(value, 'g', 9));
  } else row("Float32", "—");
  if (available >= 8) {
    quint64 bits = readValue(8);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    row("Float64", QString::number(value, 'g', 17));
  } else row("Float64", "—");
  html += "</table>";
  inspector_->setText(html);
}

void MainWindow::updateOverview()
{
  const auto& data = memory_view_->getData();
  std::unique_ptr<StandardMap> map;
  if (MachoMap::isSupported(data)) map = std::make_unique<MachoMap>();
  else if (ElfMap::isSupported(data)) map = std::make_unique<ElfMap>();
  else map = std::make_unique<StandardMap>();
  heat_map_->updateMap(data, map.get());
  QStringList legend;
  QSet<QPair<QString, QRgb>> seen;
  for (const auto& region : heat_map_->regions()) {
    const auto key = qMakePair(region.legend_label, region.color.rgba());
    if (seen.contains(key)) continue;
    seen.insert(key);
    const QString color = region.color.name();
    legend.append(QString("<span style='color:%1'>■</span>&nbsp;%2")
                      .arg(color, region.legend_label.toHtmlEscaped()));
  }
  if (!legend.isEmpty()) {
    legend.append(QString("<span style='color:%1'>▣</span>&nbsp;%2")
                      .arg(palette().color(QPalette::WindowText).name(),
                           tr("Visible bytes").toHtmlEscaped()));
  }
  overview_legend_->setText(legend.join(" &nbsp;&nbsp; "));
  overview_legend_->setVisible(!legend.isEmpty());
  const auto* current = regions_->currentItem();
  const QVariant currentOffset = current ? current->data(0, Qt::UserRole) : QVariant();
  const QString currentLabel = current ? current->text(0) : QString();
  const bool currentSelected = current && current->isSelected();
  const int regionScroll = regions_->verticalScrollBar()->value();
  regions_->clear();
  for (const auto& region : heat_map_->regions()) {
    auto* item = new QTreeWidgetItem(regions_, {region.label, offsetText(region.start)});
    item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(region.start));
    item->setToolTip(0, tr("%1 bytes · %2–%3").arg(region.end - region.start).arg(offsetText(region.start), offsetText(region.end - 1)));
    if (currentOffset.isValid() && currentOffset.toULongLong() == region.start &&
        currentLabel == region.label) {
      regions_->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
      item->setSelected(currentSelected);
    }
  }
  regions_->verticalScrollBar()->setValue(regionScroll);
  updateDocument();
}

void MainWindow::rememberFile(const QString& path)
{
  QSettings settings;
  QStringList recent = settings.value("recentFiles").toStringList();
  recent.removeAll(path);
  recent.prepend(path);
  while (recent.size() > 8) recent.removeLast();
  settings.setValue("recentFiles", recent);
  updateRecentFiles();
}
void MainWindow::updateRecentFiles()
{
  recent_menu_->clear();
  const QStringList recent = QSettings().value("recentFiles").toStringList();
  for (const QString& path : recent) {
    auto* action = recent_menu_->addAction(QFileInfo(path).fileName());
    action->setToolTip(path);
    connect(action, &QAction::triggered, this, [this, path] { openFile(path); });
  }
  recent_menu_->setEnabled(!recent.isEmpty());
}
