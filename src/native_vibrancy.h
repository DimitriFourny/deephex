#pragma once

#include <QObject>
#include <memory>

class QWidget;

class NativeVibrancy : public QObject {
  Q_OBJECT

 public:
  explicit NativeVibrancy(QWidget* window);
  ~NativeVibrancy() override;

  bool isActive() const;
  bool isSupported() const;

 public slots:
  void setEnabled(bool enabled);

 signals:
  void activeChanged(bool active);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  class Private;
  std::unique_ptr<Private> d_;
};
