#include "native_vibrancy.h"

#include <QApplication>
#include <QEvent>
#include <QMetaObject>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#import <Cocoa/Cocoa.h>

// Match Qt's coordinate system without changing the QNSView or the titlebar.
@interface DeepHexVisualEffectView : NSVisualEffectView
@property(nonatomic) BOOL qtFlipped;
@end

@implementation DeepHexVisualEffectView
- (BOOL)isFlipped { return self.qtFlipped; }
- (BOOL)allowsVibrancy { return NO; }
- (NSView*)hitTest:(NSPoint)point { Q_UNUSED(point); return nil; }
@end

class NativeVibrancy::Private {
 public:
  explicit Private(NativeVibrancy* owner, QWidget* widget)
      : q(owner), target(widget) {
    refreshTimer.setSingleShot(true);
    QObject::connect(&refreshTimer, &QTimer::timeout, q, [this] { refresh(); });
    if (target) target->installEventFilter(q);
    if (!supported()) return;

    // The workspace owns the token/block, but the block never owns the controller.
    QPointer<NativeVibrancy> guard(q);
    accessibilityObserver = [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                  if (!guard) return;
                  QMetaObject::invokeMethod(guard.data(), [guard] {
                    if (guard) guard->d_->refresh();
                  }, Qt::QueuedConnection);
                }];
  }

  ~Private() {
    destroying = true;
    refreshTimer.stop();
    if (accessibilityObserver) {
      [[[NSWorkspace sharedWorkspace] notificationCenter]
          removeObserver:accessibilityObserver];
    }
    detach();
  }

  bool supported() const {
    if (QApplication::platformName() != QStringLiteral("cocoa")) return false;
    if (@available(macOS 10.14, *)) return true;
    return false;
  }

  void scheduleRefresh() {
    if (!refreshTimer.isActive()) refreshTimer.start(0);
  }

  void setActive(bool value) {
    if (active == value) return;
    active = value;
    if (!destroying) emit q->activeChanged(active);
  }

  bool attachedTo(NSView* view) const {
    return effect && nativeWindow && view == qtView &&
           view.window == nativeWindow && effect.superview == originalParent &&
           view.superview == originalParent;
  }

  void detach() {
    Q_ASSERT([NSThread isMainThread]);
    NSWindow* window = nativeWindow;
    if (effect) {
      [effect removeFromSuperview];
      if (window) {
        window.backgroundColor = originalBackground;
        window.titlebarAppearsTransparent = originalTitlebarTransparent;
        window.appearance = originalAppearance;
      }
    }
    effect = nil;
    nativeWindow = nil;
    qtView = nil;
    originalParent = nil;
    originalBackground = nil;
    originalAppearance = nil;
    setActive(false);
  }
  void makeOpaque(NSWindow* window) {
    // WA_TranslucentBackground makes Qt clear even the native titlebar.
    // Qt overrides NSWindow.isOpaque for its alpha-capable surface. Opacity
    // here comes from solid backing pixels, not that native optimization flag.
    const QColor color = target->palette().color(QPalette::Window);
    window.backgroundColor = [NSColor colorWithSRGBRed:color.redF()
                                               green:color.greenF()
                                                blue:color.blueF()
                                               alpha:1.0];
  }

  void refresh() {
    Q_ASSERT([NSThread isMainThread]);
    if (refreshing) {
      scheduleRefresh();
      return;
    }
    QScopedValueRollback<bool> refreshingGuard(refreshing, true);
    if (!supported() || !target) {
      detach();
      return;
    }

    QWindow* handle = target->windowHandle();
    if (observedHandle != handle) {
      detach();
      if (observedHandle) observedHandle->removeEventFilter(q);
      observedHandle = handle;
      surfaceUnavailable = false;
      if (handle) handle->installEventFilter(q);
    }
    // Do not create a native surface during construction, destruction, or offscreen use.
    if (surfaceUnavailable || !handle || !target->internalWinId()) {
      detach();
      return;
    }
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(target->winId());
    NSWindow* window = view.window;
    if (!window) {
      detach();
      return;
    }
    if (!enabled ||
        [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceTransparency]) {
      detach();
      makeOpaque(window);
      return;
    }
    if (attachedTo(view)) {
      effect.frame = view.superview.bounds;
      window.titlebarAppearsTransparent = YES;
      return;
    }
    detach();

    NSView* parent = view.superview;
    if (!parent) return;

    if (@available(macOS 10.14, *)) {
      nativeWindow = window;
      qtView = view;
      originalParent = parent;
      originalBackground = window.backgroundColor;
      originalAppearance = window.appearance;
      originalTitlebarTransparent = window.titlebarAppearsTransparent;

      // Cover the native frame too, not just Qt's content below the titlebar.
      effect = [[DeepHexVisualEffectView alloc] initWithFrame:parent.bounds];
      effect.identifier = @"DeepHexVibrancy";
      effect.qtFlipped = view.isFlipped;
      effect.material = NSVisualEffectMaterialUnderWindowBackground;
      effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
      effect.state = NSVisualEffectStateActive;
      effect.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];

      // Keep QNSView as NSWindow.contentView: Qt uses that identity to resize
      // top-level windows. Its native frame parent can also host our backing.
      effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
      [parent addSubview:effect positioned:NSWindowBelow relativeTo:view];
      window.backgroundColor = [NSColor clearColor];
      window.appearance = effect.appearance;
      window.titlebarAppearsTransparent = YES;
      setActive(true);
    }
  }

  NativeVibrancy* q;
  QPointer<QWidget> target;
  QPointer<QWindow> observedHandle;
  QTimer refreshTimer;
  bool enabled = false;
  bool refreshing = false;
  bool destroying = false;
  bool active = false;
  bool surfaceUnavailable = false;
  __strong id accessibilityObserver = nil;
  __strong DeepHexVisualEffectView* effect = nil;
  __weak NSWindow* nativeWindow = nil;
  __weak NSView* qtView = nil;
  __weak NSView* originalParent = nil;
  __strong NSColor* originalBackground = nil;
  __strong NSAppearance* originalAppearance = nil;
  BOOL originalTitlebarTransparent = NO;
};

NativeVibrancy::NativeVibrancy(QWidget* window)
    : QObject(window), d_(std::make_unique<Private>(this, window)) {}

NativeVibrancy::~NativeVibrancy() {
  // Removing the backing can synchronously deliver native geometry events.
  // Stop filtering them before unique_ptr clears d_ during member destruction.
  if (d_->target) d_->target->removeEventFilter(this);
  if (d_->observedHandle) d_->observedHandle->removeEventFilter(this);
}

bool NativeVibrancy::isActive() const { return d_->active; }
bool NativeVibrancy::isSupported() const { return d_->supported(); }

void NativeVibrancy::setEnabled(bool enabled) {
  if (QThread::currentThread() != thread()) {
    QMetaObject::invokeMethod(this, [this, enabled] { setEnabled(enabled); },
                              Qt::QueuedConnection);
    return;
  }
  d_->enabled = enabled;
  d_->refresh();
}

bool NativeVibrancy::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::PlatformSurface) {
    auto* surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event);
    if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
      d_->surfaceUnavailable = true;
      d_->refreshTimer.stop();
      d_->detach();
    } else {
      d_->surfaceUnavailable = false;
      d_->scheduleRefresh();
    }
  } else if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange ||
             event->type() == QEvent::Resize || event->type() == QEvent::ParentChange ||
             event->type() == QEvent::WindowStateChange || event->type() == QEvent::PaletteChange) {
    d_->scheduleRefresh();
  }
  return QObject::eventFilter(watched, event);
}
