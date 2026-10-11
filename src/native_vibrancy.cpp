#include "native_vibrancy.h"

#include <QWidget>

class NativeVibrancy::Private {};

NativeVibrancy::NativeVibrancy(QWidget* window)
    : QObject(window) {}

NativeVibrancy::~NativeVibrancy() = default;

bool NativeVibrancy::isActive() const { return false; }
bool NativeVibrancy::isSupported() const { return false; }
void NativeVibrancy::setEnabled(bool) {}

bool NativeVibrancy::eventFilter(QObject* watched, QEvent* event) {
  return QObject::eventFilter(watched, event);
}
