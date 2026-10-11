#import <AppKit/AppKit.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#import "mainwindow.h"

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
      std::abort();                                                        \
    }                                                                      \
  } while (0)

namespace {
void pump()
{
  [NSRunLoop.currentRunLoop
      runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
}
void type(DHMemoryView* view, NSString* characters)
{
  NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                    location:NSZeroPoint
                               modifierFlags:0
                                   timestamp:0
                                windowNumber:view.window.windowNumber
                                     context:nil
                                  characters:characters
                 charactersIgnoringModifiers:characters
                                   isARepeat:NO
                                     keyCode:0];
  [view keyDown:event];
}
void clickAlert(NSString* title)
{
  __block NSTimer* timer = nil;
  CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 5;
  timer = [NSTimer
      timerWithTimeInterval:0.01
                    repeats:YES
                      block:^(NSTimer* tick) {
                        CHECK(CFAbsoluteTimeGetCurrent() < deadline);
                        NSWindow* modal = NSApp.modalWindow;
                        if (!modal)
                          return;
                        NSMutableArray<NSView*>* pending =
                            [NSMutableArray arrayWithObject:modal.contentView];
                        while (pending.count) {
                          NSView* view = pending.lastObject;
                          [pending removeLastObject];
                          if ([view isKindOfClass:NSButton.class] &&
                              [((NSButton*)view).title isEqual:title]) {
                            [tick invalidate];
                            [((NSButton*)view) performClick:nil];
                            return;
                          }
                          [pending addObjectsFromArray:view.subviews];
                        }
                      }];
  [NSRunLoop.currentRunLoop addTimer:timer forMode:NSModalPanelRunLoopMode];
  [NSRunLoop.currentRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
}
NSView* findView(NSView* root, Class kind)
{
  if ([root isKindOfClass:kind] &&
      (kind != NSTextView.class || !((NSTextView*)root).editable))
    return root;
  for (NSView* view in root.subviews)
    if (NSView* found = findView(view, kind))
      return found;
  return nil;
}
NSData* disk(NSString* path)
{
  return [NSData dataWithContentsOfFile:path];
}
NSData* bytes(std::initializer_list<uint8_t> values)
{
  return [NSData dataWithBytes:values.begin() length:values.size()];
}
}  // namespace

int main()
{
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSString* domain = NSProcessInfo.processInfo.processName;
    [NSUserDefaults.standardUserDefaults removePersistentDomainForName:domain];
    NSString* directory = [NSTemporaryDirectory()
        stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    CHECK([NSFileManager.defaultManager createDirectoryAtPath:directory
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil]);
    NSString* path = [directory stringByAppendingPathComponent:@"original.bin"];
    CHECK([bytes({0x12, 0x34, 0x56}) writeToFile:path atomically:YES]);
    CHECK([NSFileManager.defaultManager setAttributes:@{
      NSFilePosixPermissions : @0751
    }
                                         ofItemAtPath:path
                                                error:nil]);
    DHMainWindowController* window = [[DHMainWindowController alloc] init];
    [window installMenus];
    [window showWindow:nil];
    pump();
    CHECK([window openFile:path]);
    DHMemoryView* editor = window.editor;
    type(editor, @"a");
    CHECK(editor.modified);
    clickAlert(@"Cancel");
    [window.window performClose:nil];
    CHECK(window.window.visible);
    CHECK(editor.modified);
    CHECK([editor data][0] == 0xa2);
    clickAlert(@"Discard");
    [window.window performClose:nil];
    CHECK(!window.window.visible);
    CHECK(!editor.modified && [editor data].empty());
    CHECK([disk(path) isEqual:bytes({0x12, 0x34, 0x56})]);
    CHECK([window openFile:path]);
    type(editor, @"a");
    type(editor, @"b");
    [editor selectRange:0 length:2];
    for (NSInteger appearance : {3, 1, 2, 0}) {
      NSMenuItem* item = [[NSMenuItem alloc] init];
      item.tag = appearance;
      [window selectAppearance:item];
      pump();
      CHECK([editor data] == std::vector<uint8_t>({0xab, 0x34, 0x56}));
      CHECK(editor.selectionStart == 0 && editor.selectionLength == 2 &&
            editor.modified);
    }
    CHECK([window saveFile:path]);
    CHECK(!editor.modified);
    CHECK([disk(path) isEqual:bytes({0xab, 0x34, 0x56})]);
    CHECK([[[NSFileManager.defaultManager attributesOfItemAtPath:path error:nil]
              objectForKey:NSFilePosixPermissions] unsignedIntValue] == 0751);
    [editor undo:nil];
    CHECK([editor data][0] == 0x12 && editor.modified);
    [editor redo:nil];
    CHECK([editor data][0] == 0xab && !editor.modified);
    // Save As adopts the new path without changing the original document.
    NSString* copy = [directory stringByAppendingPathComponent:@"copy.bin"];
    [editor moveToOffset:0 select:YES];
    type(editor, @"c");
    type(editor, @"d");
    CHECK([window saveFile:copy]);
    CHECK([disk(copy) isEqual:bytes({0xcd, 0x34, 0x56})]);
    CHECK([disk(path) isEqual:bytes({0xab, 0x34, 0x56})]);
    type(editor, @"e");
    type(editor, @"f");
    CHECK([bytes({1, 2, 3, 4}) writeToFile:copy atomically:YES]);
    clickAlert(@"Cancel");
    CHECK(![window saveFile:copy]);
    CHECK(editor.modified);
    CHECK([disk(copy) isEqual:bytes({1, 2, 3, 4})]);
    clickAlert(@"Replace");
    CHECK([window saveFile:copy]);
    CHECK(!editor.modified);
    CHECK([disk(copy) isEqual:bytes({0xcd, 0xef, 0x56})]);
    // Native find controls execute searches, including overlap and wraparound.
    NSString* searchPath =
        [directory stringByAppendingPathComponent:@"search.bin"];
    CHECK([bytes({0x41, 0x42, 0x41, 0x42, 0x41}) writeToFile:searchPath
                                                  atomically:YES]);
    CHECK([window openFile:searchPath]);
    [window showFind:nil];
    NSSearchField* search = (NSSearchField*)findView(window.window.contentView,
                                                     NSSearchField.class);
    CHECK(search);
    search.stringValue = @"41 42 41";
    [window findNext:nil];
    CHECK(editor.selectionStart == 0 && editor.selectionLength == 3);
    [window findNext:nil];
    CHECK(editor.selectionStart == 2);
    [window findNext:nil];
    CHECK(editor.selectionStart == 0);
    [window findPrevious:nil];
    CHECK(editor.selectionStart == 2);
    search.stringValue = @"4";
    [window findNext:nil];
    CHECK(editor.selectionStart == 2);
    // EOF inspector never interprets values beyond the buffer.
    [editor selectRange:4 length:1];
    NSTextView* inspector =
        (NSTextView*)findView(window.window.contentView, NSTextView.class);
    CHECK(inspector);
    CHECK([inspector.string containsString:@"UInt16   —"]);
    CHECK([inspector.string containsString:@"Float64  —"]);
    [window.window performClose:nil];
    CHECK([NSFileManager.defaultManager removeItemAtPath:directory error:nil]);
    [NSUserDefaults.standardUserDefaults removePersistentDomainForName:domain];
    std::puts("PASS: native document cancel/discard, appearance/history, "
              "atomic save/save-as, external-change protection, search "
              "wrap/overlap, EOF inspector");
  }
  return 0;
}
