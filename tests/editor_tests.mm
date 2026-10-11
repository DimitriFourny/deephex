#import <AppKit/AppKit.h>
#import "heatmap.h"
#import "memoryview.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#define CHECK(expression)                                                   \
  do {                                                                      \
    if (!(expression)) {                                                    \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
      std::abort();                                                         \
    }                                                                       \
  } while (0)

namespace {
struct Editor {
  NSWindow* window;
  NSScrollView* scroll;
  DHMemoryView* view;
  DHHeatMap* overview;
  Editor()
  {
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 900, 440)
                                         styleMask:NSWindowStyleMaskTitled
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    window.releasedWhenClosed = NO;
    scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 40, 900, 400)];
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.drawsBackground = NO;
    view = [[DHMemoryView alloc] initWithFrame:NSMakeRect(0, 0, 900, 400)];
    scroll.documentView = view;
    [window.contentView addSubview:scroll];
    overview = [[DHHeatMap alloc] initWithFrame:NSMakeRect(0, 0, 300, 24)];
    overview.memoryView = view;
    [window.contentView addSubview:overview];
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:view];
    [scroll tile];
  }
  ~Editor() { [window close]; }
};
void key(DHMemoryView* view,
         unichar character,
         NSEventModifierFlags modifiers = 0)
{
  NSString* text = [NSString stringWithCharacters:&character length:1];
  NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                    location:NSZeroPoint
                               modifierFlags:modifiers
                                   timestamp:0
                                windowNumber:view.window.windowNumber
                                     context:nil
                                  characters:text
                 charactersIgnoringModifiers:[text lowercaseString]
                                   isARepeat:NO
                                     keyCode:0];
  [view keyDown:event];
}
void clipboard(NSString* text)
{
  [NSPasteboard.generalPasteboard clearContents];
  CHECK([NSPasteboard.generalPasteboard setString:text
                                          forType:NSPasteboardTypeString]);
}
void bytes(DHMemoryView* view, std::initializer_list<uint8_t> expected)
{
  CHECK([view data] == std::vector<uint8_t>(expected));
}
void click(DHHeatMap* overview,
           CGFloat x,
           NSEventType type = NSEventTypeLeftMouseDown)
{
  NSPoint location = [overview convertPoint:NSMakePoint(x, 12) toView:nil];
  NSEvent* event = [NSEvent mouseEventWithType:type
                                      location:location
                                 modifierFlags:0
                                     timestamp:0
                                  windowNumber:overview.window.windowNumber
                                       context:nil
                                   eventNumber:0
                                    clickCount:1
                                      pressure:1];
  if (type == NSEventTypeLeftMouseDown)
    [overview mouseDown:event];
  else if (type == NSEventTypeLeftMouseDragged)
    [overview mouseDragged:event];
  else
    [overview mouseUp:event];
}
void mouse(DHMemoryView* view, NSPoint point, NSEventType type)
{
  NSEvent* event = [NSEvent mouseEventWithType:type
                                      location:[view convertPoint:point
                                                           toView:nil]
                                 modifierFlags:0
                                     timestamp:0
                                  windowNumber:view.window.windowNumber
                                       context:nil
                                   eventNumber:0
                                    clickCount:1
                                      pressure:1];
  if (type == NSEventTypeLeftMouseDown)
    [view mouseDown:event];
  else if (type == NSEventTypeLeftMouseDragged)
    [view mouseDragged:event];
  else
    [view mouseUp:event];
}
void dismissPasteAlert(Editor& editor)
{
  NSWindow* sheet = editor.window.attachedSheet;
  CHECK(sheet != nil);
  [NSApp endSheet:sheet returnCode:NSModalResponseOK];
  [sheet orderOut:nil];
  // Let the native sheet completion retire before presenting another alert.
  [NSRunLoop.mainRunLoop
      runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
}
void emptyDocument()
{
  Editor editor;
  DHMemoryView* view = editor.view;
  [view setData:{}];
  [view moveToOffset:std::numeric_limits<uint64_t>::max() select:YES];
  [view selectAll:nil];
  [view selectRange:0 length:1];
  [view zeroSelection:nil];
  [view copy:nil];
  [view paste:nil];
  key(view, 'A');
  key(view, NSEndFunctionKey, NSEventModifierFlagCommand);
  CHECK(view.selectionStart == -1);
  CHECK(view.selectionLength == 0);
  CHECK(!view.modified);
  CHECK([view data].empty());
  CHECK(view.visibleByteStart == 0 && view.visibleByteEnd == 0);
  CHECK(!view.undoManager.canUndo);
  [editor.overview setRegions:{} totalSize:0];
  click(editor.overview, -100);
}
void nibbleUndoAndCleanState()
{
  Editor editor;
  DHMemoryView* view = editor.view;
  [view setData:{0x12, 0x34, 0x56}];
  key(view, 'A');
  key(view, 'B');
  bytes(view, {0xab, 0x34, 0x56});
  CHECK(view.selectionStart == 1 && view.modified);
  [view.undoManager undo];
  bytes(view, {0x12, 0x34, 0x56});
  CHECK(!view.modified && !view.undoManager.canUndo);
  [view.undoManager redo];
  bytes(view, {0xab, 0x34, 0x56});
  [view markSaved];
  CHECK(!view.modified);
  [view undo:nil];
  CHECK(view.modified);
  [view redo:nil];
  CHECK(!view.modified);
  [view moveToOffset:0 select:YES];
  key(view, 'C');
  key(view, 'D');
  [view undo:nil];
  bytes(view, {0xab, 0x34, 0x56});
  CHECK(!view.modified);
  // Saving between nibbles must end the pair, not amend saved history.
  [view moveToOffset:0 select:YES];
  key(view, 'E');
  [view markSaved];
  key(view, 'F');
  bytes(view, {0xfb, 0x34, 0x56});
  [view undo:nil];
  bytes(view, {0xeb, 0x34, 0x56});
  CHECK(!view.modified);
  [view redo:nil];
  CHECK(view.modified);
  // Undo followed by a fresh edit cannot accidentally reuse the saved identity.
  [view undo:nil];
  [view undo:nil];
  [view moveToOffset:0 select:YES];
  key(view, '7');
  key(view, '8');
  CHECK(view.modified && !view.undoManager.canRedo);
  // An unchanged high nibble still permits a single low-nibble undo operation.
  [view setData:{0x12}];
  key(view, '1');
  key(view, 'F');
  bytes(view, {0x1f});
  [view undo:nil];
  bytes(view, {0x12});
  CHECK(!view.modified && !view.undoManager.canUndo);
  [view setData:{0x12}];
  key(view, '1');
  key(view, '2');
  CHECK(!view.modified && !view.undoManager.canUndo);
}
void overwritePasteAndZero()
{
  Editor editor;
  DHMemoryView* view = editor.view;
  [view setData:{0x11, 0x22, 0x33, 0x44}];
  [view selectRange:1 length:2];
  clipboard(@"AA bb");
  [view paste:nil];
  bytes(view, {0x11, 0xaa, 0xbb, 0x44});
  CHECK(view.selectionStart == 1 && view.selectionLength == 2);
  [view copy:nil];
  CHECK([[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString]
      isEqualToString:@"AA BB"]);
  [view undo:nil];
  bytes(view, {0x11, 0x22, 0x33, 0x44});
  [view selectRange:1 length:2];
  [view zeroSelection:nil];
  bytes(view, {0x11, 0, 0, 0x44});
  [view undo:nil];
  bytes(view, {0x11, 0x22, 0x33, 0x44});
  [view redo:nil];
  bytes(view, {0x11, 0, 0, 0x44});
  [view markSaved];
  [view zeroSelection:nil];
  CHECK(!view.modified);
  [view setData:{0x11, 0x22, 0x33, 0x44}];
  [view selectRange:0 length:1];
  for (NSString* invalid in @[ @"0xFF", @"ABC", @"AA BB CC DD EE", @"GG" ]) {
    clipboard(invalid);
    [view paste:nil];
    bytes(view, {0x11, 0x22, 0x33, 0x44});
    CHECK(!view.modified && !view.undoManager.canUndo);
    dismissPasteAlert(editor);
  }
  clipboard(@"AA\n\tbb");
  [view paste:nil];
  bytes(view, {0xaa, 0xbb, 0x33, 0x44});
  [view undo:nil];
  key(view, '\t');
  clipboard(@"éA");
  [view paste:nil];
  bytes(view, {0xc3, 0xa9, 'A', 0x44});
  CHECK(view.selectionLength == 3);
  [view undo:nil];
  bytes(view, {0x11, 0x22, 0x33, 0x44});
  [view selectRange:3 length:1];
  clipboard(@"é");
  [view paste:nil];
  bytes(view, {0x11, 0x22, 0x33, 0x44});
  dismissPasteAlert(editor);
}
void selectionAndKeyboardBounds()
{
  Editor editor;
  DHMemoryView* view = editor.view;
  [view setData:std::vector<uint8_t>(8192, 0x41)];
  [view selectRange:3 length:NSIntegerMax];
  CHECK(view.selectionStart == 3 && view.selectionLength == 8189);
  [view selectRange:NSIntegerMax length:100];
  CHECK(view.selectionStart == 8191 && view.selectionLength == 1);
  [view selectRange:NSIntegerMin length:2];
  CHECK(view.selectionStart == 0 && view.selectionLength == 2);
  [view selectRange:0 length:0];
  CHECK(view.selectionStart == -1 && view.selectionLength == 0);
  [view moveToOffset:std::numeric_limits<uint64_t>::max() select:YES];
  CHECK(view.selectionStart == 8191 && view.visibleByteEnd == 8192);
  key(view, NSHomeFunctionKey, NSEventModifierFlagCommand);
  CHECK(view.selectionStart == 0 && view.visibleByteStart == 0);
  key(view, NSRightArrowFunctionKey, NSEventModifierFlagShift);
  CHECK(view.selectionStart == 0 && view.selectionLength == 2);
  key(view, NSDownArrowFunctionKey, NSEventModifierFlagShift);
  CHECK(view.selectionLength == 18);
  key(view, NSEndFunctionKey);
  CHECK(view.selectionStart == 31 && view.selectionLength == 1);
  key(view, NSHomeFunctionKey);
  CHECK(view.selectionStart == 16);
  key(view, NSPageDownFunctionKey);
  CHECK(view.selectionStart > 16);
  key(view, NSPageUpFunctionKey);
  CHECK(view.selectionStart == 16);
  key(view, NSEndFunctionKey,
      NSEventModifierFlagCommand | NSEventModifierFlagShift);
  CHECK(view.selectionStart == 16 && view.selectionLength == 8176);
  CHECK(!view.modified);
  [view moveToOffset:0 select:YES];
  key(view, '\t');
  key(view, 'B');
  CHECK([view data][0] == 'B' && view.selectionStart == 1);
  [view undo:nil];
  CHECK([view data][0] == 'A' && !view.modified);
  // Native document geometry supplies real horizontal scrolling.
  [editor.scroll setFrameSize:NSMakeSize(300, 200)];
  [editor.scroll tile];
  [view moveToOffset:15 select:YES];
  CHECK(editor.scroll.contentView.bounds.origin.x > 0);
}
void dragSelectionAndEdgeScroll()
{
  Editor editor;
  DHMemoryView* view = editor.view;
  [view setData:std::vector<uint8_t>(8192, 0x41)];
  NSFont* font = [NSFont monospacedSystemFontOfSize:13
                                             weight:NSFontWeightRegular];
  CGFloat width =
      std::ceil([@"0" sizeWithAttributes:@{NSFontAttributeName : font}].width);
  CGFloat row = std::ceil(font.ascender - font.descender + font.leading) + 6;
  NSPoint first = NSMakePoint(width * 12, row + 8 + row / 2);
  mouse(view, first, NSEventTypeLeftMouseDown);
  CHECK(view.selectionStart == 0 && view.selectionLength == 1);
  NSPoint outside = NSMakePoint(first.x, NSMaxY(view.visibleRect) + row * 2);
  mouse(view, outside, NSEventTypeLeftMouseDragged);
  CHECK(view.selectionStart == 0 && view.selectionLength > 16);
  CHECK(view.visibleByteStart > 0 && !view.modified);
  uint64_t initial = view.visibleByteStart;
  [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
  CHECK(view.visibleByteStart > initial);
  mouse(view, outside, NSEventTypeLeftMouseUp);
  CHECK(view.selectionStart == 0 && view.selectionLength <= 8192);
  CHECK(!view.modified && !view.undoManager.canUndo);
}
void overviewNavigationBounds()
{
  Editor editor;
  DHMemoryView* view = editor.view;
  [view setData:std::vector<uint8_t>(8192, 0x41)];
  MapRegion region{0, 8192, MapColor(.2, .6, .8), "ASCII text", "ASCII text"};
  [editor.overview setRegions:{region} totalSize:8192];
  click(editor.overview, -100);
  CHECK(view.visibleByteStart == 0);
  click(editor.overview, 500, NSEventTypeLeftMouseDragged);
  CHECK(view.visibleByteEnd == 8192);
  CHECK(view.visibleByteStart < view.visibleByteEnd);
  click(editor.overview, -100, NSEventTypeLeftMouseUp);
  CHECK(view.visibleByteStart == 0);
  NSInteger selection = view.selectionStart;
  click(editor.overview, 150);
  CHECK(view.visibleByteStart > 0 && view.visibleByteStart < 8192);
  CHECK(view.selectionStart == selection);
  // Both viewport endpoints exceed this temporarily stale document map.
  [editor.overview setRegions:{} totalSize:16];
  click(editor.overview, 500);
  CHECK(view.visibleByteStart == 0);
  [view setData:std::vector<uint8_t>(16, 0x41)];
  [editor.overview setRegions:{} totalSize:16];
  click(editor.overview, 500);
  CHECK(view.visibleByteStart == 0 && view.visibleByteEnd == 16);
  NSString* tooltip = [editor.overview view:editor.overview
                           stringForToolTip:0
                                      point:NSMakePoint(-100, 12)
                                   userData:nullptr];
  CHECK([tooltip containsString:@"Offset 0x0"]);
  [editor.overview setRegions:{
    MapRegion{0, 16, MapColor(.2, .6, .8), "Test region", "Test region"}
  }
                    totalSize:16];
  tooltip = [editor.overview view:editor.overview
                 stringForToolTip:0
                            point:NSMakePoint(500, 12)
                         userData:nullptr];
  CHECK([tooltip containsString:@"Test region"] &&
        [tooltip containsString:@"Offset 0xF"]);
  CHECK([view.accessibilityLabel length] &&
        [editor.overview.accessibilityLabel length]);
}
void appearanceChangesPreserveDocumentHistory()
{
  Editor editor;
  DHMemoryView* view = editor.view;
  [view setData:{0x12, 0x34, 0x56}];
  key(view, 'A');
  key(view, 'B');
  [view selectRange:0 length:2];
  NSPoint origin = editor.scroll.contentView.bounds.origin;
  for (NSAppearanceName appearance in
       @[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]) {
    editor.window.appearance = [NSAppearance appearanceNamed:appearance];
    for (BOOL terminal : {NO, YES})
      for (BOOL translucent : {NO, YES}) {
        view.terminalTheme = terminal;
        view.translucent = translucent;
        bytes(view, {0xab, 0x34, 0x56});
        CHECK(view.selectionStart == 0 && view.selectionLength == 2 &&
              view.modified);
        CHECK(NSEqualPoints(origin, editor.scroll.contentView.bounds.origin));
        CHECK(view.isOpaque == !(terminal && translucent));
      }
  }
  [view markSaved];
  CHECK(!view.modified);
  [view undo:nil];
  bytes(view, {0x12, 0x34, 0x56});
  CHECK(view.modified);
  [view redo:nil];
  bytes(view, {0xab, 0x34, 0x56});
  CHECK(!view.modified);
}
}  // namespace

int main()
{
  @autoreleasepool {
    [NSApplication sharedApplication];
    NSMutableArray<NSPasteboardItem*>* clipboardItems = [NSMutableArray array];
    for (NSPasteboardItem* item in NSPasteboard.generalPasteboard
             .pasteboardItems) {
      NSPasteboardItem* saved = [[NSPasteboardItem alloc] init];
      for (NSPasteboardType type in item.types) {
        NSData* data = [item dataForType:type];
        if (data)
          [saved setData:data forType:type];
      }
      [clipboardItems addObject:saved];
    }
    emptyDocument();
    nibbleUndoAndCleanState();
    overwritePasteAndZero();
    selectionAndKeyboardBounds();
    dragSelectionAndEdgeScroll();
    overviewNavigationBounds();
    appearanceChangesPreserveDocumentHistory();
    [NSPasteboard.generalPasteboard clearContents];
    if (clipboardItems.count)
      [NSPasteboard.generalPasteboard writeObjects:clipboardItems];
    std::puts("Native editor behavior regressions passed.");
  }
  return 0;
}
