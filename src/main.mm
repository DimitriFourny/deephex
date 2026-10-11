#import <AppKit/AppKit.h>
#import "mainwindow.h"
#include <cstdio>
#include <cstring>

@interface DHApplicationDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) DHMainWindowController *controller;
@property(nonatomic, copy) NSString *pendingFile;
@end
@implementation DHApplicationDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  self.controller = [[DHMainWindowController alloc] init];
  [self.controller installMenus]; [self.controller showWindow:nil];
  [NSApp activateIgnoringOtherApps:YES];
  if (self.pendingFile) { [self.controller openFile:self.pendingFile]; self.pendingFile = nil; }
}
- (BOOL)application:(NSApplication *)application openFile:(NSString *)filename {
  (void)application;
  if (!self.controller) { self.pendingFile = filename; return YES; }
  [self.controller showWindow:nil]; [NSApp activateIgnoringOtherApps:YES];
  return [self.controller openFile:filename];
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
  (void)sender; return [self.controller maybeSave] ? NSTerminateNow : NSTerminateCancel;
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)visible {
  (void)sender; if (!visible) [self.controller showWindow:nil]; return YES;
}
@end
int main(int argc, char *argv[]) {
  @autoreleasepool {
    NSString *path = nil;
    for (int i = 1; i < argc; ++i) {
      if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
        std::puts("DeepHex — an overwrite-only binary editor and inspector\nUsage: deephex [file]\n  -h, --help     Show help\n  -v, --version  Show version"); return 0;
      }
      if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) { std::puts("DeepHex 2.0.0"); return 0; }
      if (!strncmp(argv[i], "-psn_", 5)) continue;
      if (path || argv[i][0] == '-') { std::fputs("Usage: deephex [file]\n", stderr); return 1; }
      path = [NSFileManager.defaultManager stringWithFileSystemRepresentation:argv[i] length:strlen(argv[i])];
    }
    NSApplication *app = NSApplication.sharedApplication;
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    DHApplicationDelegate *delegate = [[DHApplicationDelegate alloc] init];
    delegate.pendingFile = path; app.delegate = delegate; [app run];
  }
  return 0;
}
