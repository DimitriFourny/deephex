#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QUndoStack>
#include <QScrollBar>
#include <QAbstractButton>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QAction>
#include "mainwindow.h"
#include "heatmap.h"
#include <limits>
#include "memoryview.h"
#include "standard_map.h"
#include "macho_map.h"
#include "elf_map.h"

namespace {
void put(QVector<quint8>& data, int offset, quint64 value, int width, bool little = true) {
  for (int i = 0; i < width; ++i) data[offset + i] = quint8(value >> (8 * (little ? i : width - 1 - i)));
}
QVector<quint8> machO(bool little, bool is64) {
  QVector<quint8> data(160, 0);
  int header = is64 ? 32 : 28;
  int command = is64 ? 72 : 56;
  put(data, 0, is64 ? 0xfeedfacf : 0xfeedface, 4, little);
  put(data, 4, is64 ? 0x0100000c : 12, 4, little);
  put(data, 16, 1, 4, little);
  put(data, 20, command, 4, little);
  put(data, header, is64 ? 0x19 : 1, 4, little);
  put(data, header + 4, command, 4, little);
  const QByteArray name("__TEXT");
  for (int i = 0; i < name.size(); ++i) data[header + 8 + i] = name[i];
  put(data, header + (is64 ? 40 : 32), 128, is64 ? 8 : 4, little);
  put(data, header + (is64 ? 48 : 36), 16, is64 ? 8 : 4, little);
  return data;
}
QVector<quint8> elf(bool little, bool is64) {
  QVector<quint8> data(160, 0);
  data[0] = 0x7f; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
  data[4] = is64 ? 2 : 1; data[5] = little ? 1 : 2; data[6] = 1;
  const int header = is64 ? 64 : 52;
  const int entry = is64 ? 56 : 32;
  put(data, 20, 1, 4, little);
  put(data, is64 ? 32 : 28, header, is64 ? 8 : 4, little);
  put(data, is64 ? 52 : 40, header, 2, little);
  put(data, is64 ? 54 : 42, entry, 2, little);
  put(data, is64 ? 56 : 44, 1, 2, little);
  put(data, header, 1, 4, little);
  put(data, header + (is64 ? 8 : 4), 128, is64 ? 8 : 4, little);
  put(data, header + (is64 ? 32 : 16), 16, is64 ? 8 : 4, little);
  put(data, header + (is64 ? 40 : 20), 16, is64 ? 8 : 4, little);
  return data;
}
}

class EditorTests : public QObject {
  Q_OBJECT
  QTemporaryDir settings_dir_;
 private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("DeepHexTests");
    QCoreApplication::setApplicationName("DeepHexTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir_.path());
  }
  void cancelThenDiscardUnsavedChanges() {
    QTemporaryDir files;
    const QString path = files.filePath("original.bin");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray::fromHex("123456")), qint64(3));
    file.close();
    {
      MainWindow window;
      window.show();
      QVERIFY(window.openFile(path));
      auto* editor = window.findChild<MemoryView*>("hexEditor");
      QVERIFY(editor);
      QTest::keyClick(editor, Qt::Key_A);
      QVERIFY(editor->isModified());
      auto clickDialog = [](QMessageBox::StandardButton button) {
        QTimer::singleShot(0, qApp, [button] {
          for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(widget))
              box->button(button)->click();
        });
      };
      clickDialog(QMessageBox::Cancel);
      QVERIFY(!window.close());
      QVERIFY(window.isVisible());
      QCOMPARE(editor->getData()[0], quint8(0xa2));
      QVERIFY(editor->isModified());
      clickDialog(QMessageBox::Discard);
      QVERIFY(window.close());
    }
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray::fromHex("123456"));
  }
  void appearanceChangesPreserveDocumentHistory() {
    QTemporaryDir files;
    const QString path = files.filePath("appearance.bin");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray::fromHex("123456")), qint64(3));
    file.close();
    MainWindow window;
    window.show();
    QVERIFY(window.openFile(path));
    auto* editor = window.findChild<MemoryView*>("hexEditor");
    QVERIFY(editor);
    QTest::keyClick(editor, Qt::Key_A);
    QTest::keyClick(editor, Qt::Key_B);
    editor->selectRange(0, 2);
    for (const char* name : {"appearanceTerminal", "appearanceLight",
                             "appearanceDark", "appearanceSystem"}) {
      auto* action = window.findChild<QAction*>(name);
      QVERIFY(action);
      action->trigger();
      QCoreApplication::processEvents();
      QCOMPARE(editor->getData(), QVector<quint8>({0xab, 0x34, 0x56}));
      QCOMPARE(editor->selectionStart(), 0);
      QCOMPARE(editor->selectionLength(), 2);
      QVERIFY(editor->isModified());
    }
    QVERIFY(window.saveFile(path));
    QVERIFY(!editor->isModified());
    editor->undoStack()->undo();
    QCOMPARE(editor->getData(), QVector<quint8>({0x12, 0x34, 0x56}));
    QVERIFY(editor->isModified());
    editor->undoStack()->redo();
    QCOMPARE(editor->getData(), QVector<quint8>({0xab, 0x34, 0x56}));
    QVERIFY(!editor->isModified());
    QVERIFY(window.close());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray::fromHex("ab3456"));
  }
  void distributionCoverage() {
    StandardMap map;
    for (int size : {0, 1, 99, 101, 513, 4097, 1024 * 1024}) {
      QVector<quint8> data(size, 0x41);
      QVector<MapRegion> regions;
      map.calculateRegions(data, regions, Qt::white);
      quint64 end = 0;
      for (const auto& region : regions) {
        QCOMPARE(region.start, end);
        QVERIFY(region.end > region.start);
        QVERIFY(region.end <= quint64(size));
        end = region.end;
      }
      QCOMPARE(end, quint64(size));
      QVERIFY(regions.size() <= 256);
    }
  }
  void executableRegions() {
    for (bool little : {false, true}) for (bool is64 : {false, true}) {
      for (bool isMachO : {false, true}) {
        auto data = isMachO ? machO(little, is64) : elf(little, is64);
        QVector<MapRegion> regions;
        MachoMap macho;
        ElfMap elfMap;
        StandardMap* map = isMachO ? static_cast<StandardMap*>(&macho) : static_cast<StandardMap*>(&elfMap);
        map->calculateRegions(data, regions, Qt::white);
        quint64 end = 0;
        bool foundSegment = false;
        for (const auto& region : regions) {
          QCOMPARE(region.start, end);
          QVERIFY(region.end > region.start);
          end = region.end;
          if (region.start == 128 && region.end == 144) foundSegment = true;
        }
        QCOMPARE(end, quint64(data.size()));
        QVERIFY2(foundSegment, "Executable segment must retain its file offset and length");
      }
    }
  }
  void malformedExecutableFallback() {
    for (bool isMachO : {false, true}) {
      auto data = isMachO ? machO(true, true) : elf(true, true);
      // A command with no forward progress, or a program header with an overflowing offset.
      if (isMachO) put(data, 36, 0, 4);
      else put(data, 72, std::numeric_limits<quint64>::max(), 8);
      QVector<MapRegion> actual, expected;
      StandardMap fallback;
      fallback.calculateRegions(data, expected, Qt::white);
      MachoMap macho;
      ElfMap elfMap;
      StandardMap* parser = isMachO ? static_cast<StandardMap*>(&macho) : static_cast<StandardMap*>(&elfMap);
      parser->calculateRegions(data, actual, Qt::white);
      QCOMPARE(actual.size(), expected.size());
      for (int i = 0; i < actual.size(); ++i) {
        QCOMPARE(actual[i].start, expected[i].start);
        QCOMPARE(actual[i].end, expected[i].end);
      }
    }
  }
  void overviewNavigationBounds() {
    HeatMap overview;
    MemoryView view(nullptr, &overview);
    overview.setMemoryView(&view);
    view.resize(900, 400);
    overview.resize(300, 24);
    QVector<quint8> data(8192, 0x41);
    view.setData(data);
    StandardMap map;
    overview.updateMap(data, &map);
    overview.updateViewRect(0, std::numeric_limits<quint64>::max());
    QTest::mouseClick(&overview, Qt::LeftButton, Qt::NoModifier, QPoint(-100, 12));
    QCOMPARE(view.verticalScrollBar()->value(), 0);
    overview.updateViewRect(0, 16);
    QTest::mouseClick(&overview, Qt::LeftButton, Qt::NoModifier, QPoint(500, 12));
    QCOMPARE(view.verticalScrollBar()->value(), view.verticalScrollBar()->maximum());
    overview.updateViewRect(0, 16);
    QTest::mouseClick(&overview, Qt::LeftButton, Qt::NoModifier, QPoint(-100, 12));
    QCOMPARE(view.verticalScrollBar()->value(), 0);
  }
  void emptyDocument() {
    MemoryView view(nullptr, nullptr);
    view.setData({});
    view.moveToPos(std::numeric_limits<quint64>::max(), true);
    view.selectAll();
    view.selectRange(0, 1);
    view.zeroSelection();
    QCOMPARE(view.selectionStart(), -1);
    QCOMPARE(view.selectionLength(), 0);
    QVERIFY(!view.isModified());
    QVERIFY(view.getData().isEmpty());
  }
  void nibbleUndoAndCleanState() {
    MemoryView view(nullptr, nullptr);
    view.setData({0x12, 0x34, 0x56});
    view.moveToPos(0, true);
    QTest::keyClick(&view, Qt::Key_A);
    QTest::keyClick(&view, Qt::Key_B);
    QCOMPARE(view.getData()[0], quint8(0xab));
    QVERIFY(view.isModified());
    view.undoStack()->undo();
    QCOMPARE(view.getData()[0], quint8(0x12));
    QVERIFY(!view.isModified());
    view.undoStack()->redo();
    QCOMPARE(view.getData()[0], quint8(0xab));
    view.markSaved();
    QVERIFY(!view.isModified());
    view.undoStack()->undo();
    QVERIFY(view.isModified());
    view.undoStack()->redo();
    QVERIFY(!view.isModified());
    view.moveToPos(0, true);
    QTest::keyClick(&view, Qt::Key_C);
    QTest::keyClick(&view, Qt::Key_D);
    view.undoStack()->undo();
    QCOMPARE(view.getData()[0], quint8(0xab));
  }
  void overwritePasteAndZero() {
    MemoryView view(nullptr, nullptr);
    view.setData({0x11, 0x22, 0x33, 0x44});
    view.selectRange(1, 2);
    QApplication::clipboard()->setText("AA bb");
    view.pasteClipboard();
    QCOMPARE(view.getData(), QVector<quint8>({0x11, 0xaa, 0xbb, 0x44}));
    view.undoStack()->undo();
    QCOMPARE(view.getData(), QVector<quint8>({0x11, 0x22, 0x33, 0x44}));
    view.selectRange(1, 2);
    view.zeroSelection();
    QCOMPARE(view.getData(), QVector<quint8>({0x11, 0, 0, 0x44}));
    view.undoStack()->undo();
    QCOMPARE(view.getData(), QVector<quint8>({0x11, 0x22, 0x33, 0x44}));
    view.selectRange(3, 100);
    QCOMPARE(view.selectionStart(), 3);
    QCOMPARE(view.selectionLength(), 1);
  }
};
QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
