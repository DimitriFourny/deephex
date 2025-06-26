#include "mainwindow.h"
#include <QtWidgets/qwidget.h>
#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScrollArea>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include "elf_map.h"
#include "heatmap.h"
#include "macho_map.h"
#include "standard_map.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
  setWindowTitle("DeepHex");
  resize(900, 600);

  // Create central widget and layout
  QWidget* centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);
  QVBoxLayout* layout = new QVBoxLayout(centralWidget);
  layout->setContentsMargins(0, 0, 0, 0);

  // Create a memory heat map
  heat_map_ = new HeatMap(this);
  layout->addWidget(heat_map_);

  // Create and add memory view
  memory_view_ = new MemoryView(this, heat_map_);
  layout->addWidget(memory_view_);
  heat_map_->setMemoryView(memory_view_);

  // Add random data for demonstration
  // QVector<quint8> data;
  // for (int i = 0; i < 0x1000; i++) {
  //   uint8_t n = rand() % 256;
  //   if (n > 150) {
  //     n = 0;
  //   }
  //   data.append(n);
  // }
  // memory_view_->setData(data);
  // heat_map_->updateMap(data);

  // Add File menu for opening files
  QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
  QAction* openFileAction = new QAction(tr("Open..."), this);
  fileMenu->addAction(openFileAction);

  // Add Save As action
  QAction* saveFileAction = new QAction(tr("Save As..."), this);
  fileMenu->addAction(saveFileAction);
  connect(saveFileAction, &QAction::triggered, this, [this]() {
    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Save File"), QString(), tr("All Files (*)"));
    if (!fileName.isEmpty()) {
      QFile file(fileName);
      if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to save file."));
        return;
      }
      // Assuming memory_view_ has a method to get current data as
      // QVector<quint8>
      const QVector<quint8> data = memory_view_->getData();
      QByteArray fileData;
      fileData.reserve(data.size());
      for (quint8 byte : data) {
        fileData.append(static_cast<char>(byte));
      }
      file.write(fileData);
      file.close();
    }
  });

  // Add menu for Memory Tree
  QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
  QAction* showTreeAction = new QAction(tr("Show Memory Tree"), this);
  viewMenu->addAction(showTreeAction);

  // Add Action -> Go to menu
  QMenu* actionMenu = menuBar()->addMenu(tr("&Action"));
  goToAction_ = new QAction(tr("Go to"), this);
  goToAction_->setShortcut(QKeySequence(Qt::Key_G));
  actionMenu->addAction(goToAction_);
  connect(goToAction_, &QAction::triggered, this, &MainWindow::onGoToAddress);

  connect(openFileAction, &QAction::triggered, this, &MainWindow::onOpenFile);

  // Add status bar below the MemoryView
  QStatusBar* statusBarWidget = new QStatusBar(this);
  setStatusBar(statusBarWidget);
  memory_view_->setStatusBar(statusBarWidget);
}

void MainWindow::onGoToAddress()
{
  bool ok = false;
  QString addrStr = QInputDialog::getText(this, tr("Go to Address"),
                                          tr("Enter address (hex or decimal):"),
                                          QLineEdit::Normal, QString(), &ok);
  if (!ok || addrStr.isEmpty())
    return;

  bool hex = addrStr.startsWith("0x", Qt::CaseInsensitive);
  bool okNum = false;
  quint64 addr = addrStr.toULongLong(&okNum, hex ? 16 : 10);
  if (!okNum) {
    QMessageBox::warning(this, tr("Invalid Address"),
                         tr("Could not parse address."));
    return;
  }

  // Assuming you have a pointer to your MemoryView instance called memoryView_
  if (memory_view_) {
    memory_view_->moveToPos(addr, true);
  }
}

void MainWindow::onOpenFile()
{
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Open File"), QString(), tr("All Files (*)"));
  if (!fileName.isEmpty()) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
      QMessageBox::warning(this, tr("Error"), tr("Failed to open file."));
      return;
    }

    qint64 fileSize = file.size();
    const qint64 chunkSize = 1024 * 1024;  // 1 MB
    QByteArray fileData;
    QVector<quint8> data;
    data.reserve(fileSize);

    QProgressDialog progress(tr("Loading file..."), tr("Cancel"), 0, fileSize,
                             this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);

    qint64 bytesRead = 0;
    QElapsedTimer timer;
    timer.start();

    while (!file.atEnd()) {
      if (progress.wasCanceled()) {
        file.close();
        return;
      }
      QByteArray chunk = file.read(chunkSize);
      fileData.append(chunk);
      for (char byte : chunk) {
        data.append(static_cast<quint8>(byte));
      }
      bytesRead += chunk.size();

      // Calculate percent and ETA
      double percent = (fileSize > 0) ? (100.0 * bytesRead / fileSize) : 100.0;
      qint64 elapsedMs = timer.elapsed();
      QString etaStr;
      if (bytesRead > 0 && bytesRead < fileSize) {
        double bytesPerMs = double(bytesRead) / elapsedMs;
        qint64 msRemaining =
            (fileSize - bytesRead) / (bytesPerMs > 0 ? bytesPerMs : 1);
        int seconds = int(msRemaining / 1000) % 60;
        int minutes = int(msRemaining / 60000);
        etaStr = QString("ETA: %1m %2s")
                     .arg(minutes)
                     .arg(seconds, 2, 10, QChar('0'));
      }
      else {
        etaStr = "ETA: --";
      }
      progress.setLabelText(tr("Loading file... %1%\n%2")
                                .arg(QString::number(percent, 'f', 1))
                                .arg(etaStr));

      progress.setValue(bytesRead);
      QCoreApplication::processEvents();
    }
    progress.setValue(fileSize);

    memory_view_->setData(data);

    std::unique_ptr<StandardMap> standard_map = nullptr;
    if (MachoMap::isSupported(data)) {
      printf("Mach-O\n");
      standard_map = std::make_unique<MachoMap>();
    }
    else if (ElfMap::isSupported(data)) {
      printf("ELF\n");
      standard_map = std::make_unique<ElfMap>();
    }
    else {
      standard_map = std::make_unique<StandardMap>();
    }

    if (!standard_map) {
      standard_map = std::make_unique<StandardMap>();
    }

    heat_map_->updateMap(data, standard_map.get());
    file.close();
  }
}