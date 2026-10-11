#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QIcon>
#include "mainwindow.h"

class DeepHexApplication : public QApplication {
 public:
  using QApplication::QApplication;
  MainWindow* window = nullptr;
  QString pendingFile;

 protected:
  bool event(QEvent* event) override {
    if (event->type() == QEvent::FileOpen) {
      const QString file = static_cast<QFileOpenEvent*>(event)->file();
      if (window) {
        window->show();
        window->raise();
        window->activateWindow();
        window->openFile(file);
      } else pendingFile = file;
      return true;
    }
    return QApplication::event(event);
  }
};

int main(int argc, char* argv[])
{
  DeepHexApplication app(argc, argv);
  app.setOrganizationName("DeepHex");
  app.setApplicationName("DeepHex");
  app.setApplicationVersion("2.0.0");
  app.setWindowIcon(QIcon(":/img/favicon.png"));
  QCommandLineParser parser;
  parser.setApplicationDescription("DeepHex — an overwrite-only binary editor and inspector");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("file", "Binary file to open", "[file]");
  parser.process(app);
  if (parser.positionalArguments().size() > 1) parser.showHelp(1);
  MainWindow mainWindow;
  app.window = &mainWindow;
  mainWindow.show();
  const QString path = parser.positionalArguments().isEmpty() ? app.pendingFile : parser.positionalArguments().first();
  if (!path.isEmpty()) mainWindow.openFile(path);
  return app.exec();
}
