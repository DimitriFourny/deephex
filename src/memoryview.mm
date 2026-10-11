#import "memoryview.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace {
constexpr char kHex[] = "0123456789ABCDEF";
NSString* hexText(uint8_t byte)
{
  static NSString* values[256];
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    for (unsigned i = 0; i < 256; ++i) {
      char digits[] = {kHex[i >> 4], kHex[i & 15], 0};
      values[i] = [[NSString alloc] initWithUTF8String:digits];
    }
  });
  return values[byte];
}
NSString* asciiText(uint8_t byte)
{
  static NSString* values[95];
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    for (unsigned i = 0; i < 95; ++i) {
      char text[] = {static_cast<char>(i + 32), 0};
      values[i] = [[NSString alloc] initWithUTF8String:text];
    }
  });
  return values[(byte >= 32 && byte <= 126 ? byte : '.') - 32];
}
int hexDigit(unichar value)
{
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}
NSColor* rgb(CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha = 1)
{
  return [NSColor colorWithSRGBRed:red green:green blue:blue alpha:alpha];
}
}  // namespace

// Only the changed range is retained. A pending second nibble amends this same
// native undo operation instead of keeping an undo group open across run loops.
@interface DHByteEdit : NSObject {
 @public
  NSInteger start;
  std::vector<uint8_t> before, after;
  uint64_t beforeState, afterState;
  NSString* name;
}
@end
@implementation DHByteEdit
@end

@interface DHMemoryView () {
  std::vector<uint8_t> _bytes;
  NSUndoManager* _byteUndoManager;
  DHByteEdit* _pendingNibble;
  uint64_t _state, _savedState, _nextState;
  NSInteger _anchor, _cursor;
  BOOL _asciiPane, _lowNibble, _dragging;
  NSTimer* _dragTimer;
  NSPoint _dragWindowPoint;
  __weak NSClipView* _observedClip;
  NSFont* _font;
  CGFloat _charWidth, _rowHeight, _headerHeight, _hexStart, _asciiStart,
      _contentWidth;
}
- (void)resetNibble;
- (void)updateDocumentSize;
- (void)viewportChanged:(NSNotification*)notification;
- (void)setCursor:(NSInteger)index extend:(BOOL)extend;
- (void)ensureCursorVisible;
- (void)applyEdit:(DHByteEdit*)edit undo:(BOOL)undo;
- (DHByteEdit*)overwrite:(NSInteger)start
                   bytes:(const uint8_t*)bytes
                   count:(size_t)count
                    name:(NSString*)name;
- (void)notifyDataWithPreviousModified:(BOOL)previous;
- (void)stopDragging;
@end

@implementation DHMemoryView

- (instancetype)initWithFrame:(NSRect)frame
{
  if ((self = [super initWithFrame:frame])) {
    _anchor = _cursor = -1;
    _nextState = 1;
    _byteUndoManager = [[NSUndoManager alloc] init];
    _byteUndoManager.groupsByEvent = NO;
    _font = [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular];
    _charWidth = std::ceil(
        [@"0" sizeWithAttributes:@{NSFontAttributeName : _font}].width);
    _rowHeight =
        std::ceil(_font.ascender - _font.descender + _font.leading) + 6;
    _headerHeight = _rowHeight + 8;
    _hexStart = _charWidth * 11;
    _asciiStart = [self hexX:15] + _charWidth * 4;
    _contentWidth = _asciiStart + _charWidth * 18;
    self.accessibilityElement = YES;
    self.accessibilityRole = NSAccessibilityTextAreaRole;
    self.accessibilityLabel = @"Hex and ASCII byte editor";
    [self updateDocumentSize];
  }
  return self;
}

- (void)dealloc
{
  [_dragTimer invalidate];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}
- (BOOL)isFlipped
{
  return YES;
}
- (BOOL)isOpaque
{
  return !(_terminalTheme && _translucent);
}
- (BOOL)acceptsFirstResponder
{
  return YES;
}
- (BOOL)becomeFirstResponder
{
  self.needsDisplay = YES;
  return YES;
}
- (BOOL)resignFirstResponder
{
  [self stopDragging];
  [self resetNibble];
  self.needsDisplay = YES;
  return YES;
}
- (NSUndoManager*)undoManager
{
  return _byteUndoManager;
}
- (CGFloat)hexX:(NSInteger)column
{
  return _hexStart + (column * 3 + column / 4) * _charWidth;
}
- (void)viewDidMoveToSuperview
{
  [super viewDidMoveToSuperview];
  NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
  if (_observedClip) {
    [center removeObserver:self
                      name:NSViewBoundsDidChangeNotification
                    object:_observedClip];
    [center removeObserver:self
                      name:NSViewFrameDidChangeNotification
                    object:_observedClip];
  }
  _observedClip = self.enclosingScrollView.contentView;
  if (_observedClip) {
    _observedClip.postsBoundsChangedNotifications = YES;
    _observedClip.postsFrameChangedNotifications = YES;
    [center addObserver:self
               selector:@selector(viewportChanged:)
                   name:NSViewBoundsDidChangeNotification
                 object:_observedClip];
    [center addObserver:self
               selector:@selector(viewportChanged:)
                   name:NSViewFrameDidChangeNotification
                 object:_observedClip];
    self.enclosingScrollView.verticalLineScroll = _rowHeight;
    self.enclosingScrollView.horizontalLineScroll = _charWidth * 3;
  }
  [self updateDocumentSize];
  [self viewportChanged:nil];
}
- (void)updateDocumentSize
{
  const CGFloat rows = static_cast<CGFloat>((_bytes.size() + 15) / 16);
  const NSSize viewport =
      _observedClip ? _observedClip.bounds.size : NSZeroSize;
  NSSize size =
      NSMakeSize(std::max(_contentWidth, viewport.width),
                 std::max(_headerHeight + rows * _rowHeight, viewport.height));
  if (!NSEqualSizes(size, self.frame.size))
    [self setFrameSize:size];
}
- (void)viewportChanged:(NSNotification*)notification
{
  if ([notification.name isEqualToString:NSViewFrameDidChangeNotification])
    [self updateDocumentSize];
  self.needsDisplay = YES;
  [self.delegate memoryViewViewportChanged:self];
}
- (void)viewDidChangeEffectiveAppearance
{
  [super viewDidChangeEffectiveAppearance];
  self.needsDisplay = YES;
}
- (void)setTerminalTheme:(BOOL)value
{
  _terminalTheme = value;
  self.needsDisplay = YES;
}
- (void)setTranslucent:(BOOL)value
{
  _translucent = value;
  self.needsDisplay = YES;
}
- (void)setData:(std::vector<uint8_t>)data
{
  [self stopDragging];
  [self resetNibble];
  BOOL wasModified = self.modified;
  _bytes = std::move(data);
  [_byteUndoManager removeAllActions];
  _state = _savedState = 0;
  _nextState = 1;
  _anchor = _cursor = _bytes.empty() ? -1 : 0;
  _asciiPane = NO;
  [self updateDocumentSize];
  [self scrollPoint:NSZeroPoint];
  self.needsDisplay = YES;
  [self.delegate memoryViewSelectionChanged:self];
  [self.delegate memoryViewDataChanged:self];
  [self.delegate memoryViewViewportChanged:self];
  if (wasModified)
    [self.delegate memoryViewModifiedChanged:self];
}
- (const std::vector<uint8_t>&)data
{
  return _bytes;
}
- (NSInteger)selectionStart
{
  return _anchor < 0 || _cursor < 0 ? -1 : std::min(_anchor, _cursor);
}
- (NSInteger)selectionLength
{
  return self.selectionStart < 0
             ? 0
             : std::max(_anchor, _cursor) - self.selectionStart + 1;
}
- (BOOL)isModified
{
  return _state != _savedState;
}
- (void)markSaved
{
  [self resetNibble];
  BOOL wasModified = self.modified;
  _savedState = _state;
  if (wasModified)
    [self.delegate memoryViewModifiedChanged:self];
}
- (uint64_t)visibleByteStart
{
  uint64_t row = static_cast<uint64_t>(
      std::max<CGFloat>(0, std::floor(self.visibleRect.origin.y / _rowHeight)));
  return std::min<uint64_t>(_bytes.size(), row * 16);
}
- (uint64_t)visibleByteEnd
{
  CGFloat bottom =
      std::max<CGFloat>(0, NSMaxY(self.visibleRect) - _headerHeight);
  uint64_t end = static_cast<uint64_t>(std::ceil(bottom / _rowHeight)) * 16;
  return std::max(self.visibleByteStart,
                  std::min<uint64_t>(_bytes.size(), end));
}
- (void)resetNibble
{
  _lowNibble = NO;
  _pendingNibble = nil;
  self.needsDisplay = YES;
}
- (void)selectRange:(NSInteger)start length:(NSInteger)length
{
  [self resetNibble];
  NSInteger oldAnchor = _anchor, oldCursor = _cursor;
  if (_bytes.empty() || length <= 0)
    _anchor = _cursor = -1;
  else {
    _anchor = std::clamp<NSInteger>(start, 0,
                                    static_cast<NSInteger>(_bytes.size()) - 1);
    _cursor =
        _anchor + std::min<NSInteger>(length, _bytes.size() - _anchor) - 1;
    [self ensureCursorVisible];
  }
  self.needsDisplay = YES;
  if (oldAnchor != _anchor || oldCursor != _cursor)
    [self.delegate memoryViewSelectionChanged:self];
}
- (void)setCursor:(NSInteger)index extend:(BOOL)extend
{
  if (_bytes.empty())
    return;
  index = std::clamp<NSInteger>(index, 0,
                                static_cast<NSInteger>(_bytes.size()) - 1);
  NSInteger oldAnchor = _anchor, oldCursor = _cursor;
  if (!extend || _anchor < 0)
    _anchor = index;
  _cursor = index;
  [self ensureCursorVisible];
  self.needsDisplay = YES;
  if (oldAnchor != _anchor || oldCursor != _cursor)
    [self.delegate memoryViewSelectionChanged:self];
}
- (void)ensureCursorVisible
{
  if (_cursor < 0)
    return;
  NSRect visible = self.visibleRect;
  CGFloat top = _headerHeight + (_cursor / 16) * _rowHeight;
  CGFloat x = _asciiPane ? _asciiStart + (_cursor % 16) * _charWidth
                         : [self hexX:_cursor % 16];
  CGFloat width = _asciiPane ? _charWidth : _charWidth * 3;
  NSPoint point = visible.origin;
  if (top < NSMinY(visible) + _headerHeight)
    point.y = top - _headerHeight;
  else if (top + _rowHeight > NSMaxY(visible))
    point.y = top + _rowHeight - visible.size.height;
  if (x < NSMinX(visible))
    point.x = x;
  else if (x + width > NSMaxX(visible))
    point.x = x + width - visible.size.width;
  [self scrollPoint:point];
}
- (void)moveToOffset:(uint64_t)offset select:(BOOL)select
{
  if (_bytes.empty())
    return;
  NSInteger index =
      static_cast<NSInteger>(std::min<uint64_t>(offset, _bytes.size() - 1));
  if (select) {
    [self resetNibble];
    [self setCursor:index extend:NO];
  }
  [self scrollPoint:NSMakePoint(self.visibleRect.origin.x,
                                (index / 16) * _rowHeight)];
  [self.delegate memoryViewViewportChanged:self];
}

- (void)notifyDataWithPreviousModified:(BOOL)previous
{
  self.needsDisplay = YES;
  [self.delegate memoryViewDataChanged:self];
  if (previous != self.modified)
    [self.delegate memoryViewModifiedChanged:self];
}
- (void)applyEdit:(DHByteEdit*)edit undo:(BOOL)undo
{
  [self resetNibble];
  const auto& bytes = undo ? edit->before : edit->after;
  if (edit->start < 0 || static_cast<size_t>(edit->start) > _bytes.size() ||
      bytes.size() > _bytes.size() - edit->start)
    return;
  [_byteUndoManager registerUndoWithTarget:self
                                   handler:^(DHMemoryView* target) {
                                     [target applyEdit:edit undo:!undo];
                                   }];
  [_byteUndoManager setActionName:edit->name];
  BOOL wasModified = self.modified;
  std::copy(bytes.begin(), bytes.end(), _bytes.begin() + edit->start);
  _state = undo ? edit->beforeState : edit->afterState;
  [self notifyDataWithPreviousModified:wasModified];
}
- (DHByteEdit*)overwrite:(NSInteger)start
                   bytes:(const uint8_t*)bytes
                   count:(size_t)count
                    name:(NSString*)name
{
  if (start < 0 || static_cast<size_t>(start) > _bytes.size() ||
      count > _bytes.size() - start)
    return nil;
  size_t first = 0, last = count;
  while (first < last && _bytes[start + first] == bytes[first])
    ++first;
  while (last > first && _bytes[start + last - 1] == bytes[last - 1])
    --last;
  if (first == last)
    return nil;
  DHByteEdit* edit = [[DHByteEdit alloc] init];
  edit->start = start + first;
  edit->before.assign(_bytes.begin() + edit->start,
                      _bytes.begin() + start + last);
  edit->after.assign(bytes + first, bytes + last);
  edit->beforeState = _state;
  edit->afterState = _nextState++;
  edit->name = name;
  [_byteUndoManager beginUndoGrouping];
  [_byteUndoManager registerUndoWithTarget:self
                                   handler:^(DHMemoryView* target) {
                                     [target applyEdit:edit undo:YES];
                                   }];
  [_byteUndoManager setActionName:name];
  [_byteUndoManager endUndoGrouping];
  BOOL wasModified = self.modified;
  std::copy(edit->after.begin(), edit->after.end(),
            _bytes.begin() + edit->start);
  _state = edit->afterState;
  [self notifyDataWithPreviousModified:wasModified];
  return edit;
}
- (void)enterHexDigit:(int)digit
{
  if (self.selectionLength != 1)
    return;
  NSInteger index = _cursor;
  uint8_t original = _bytes[index];
  uint8_t value =
      _lowNibble ? (original & 0xf0) | digit : (digit << 4) | (original & 0xf);
  if (_lowNibble && _pendingNibble) {
    BOOL wasModified = self.modified;
    _pendingNibble->after[0] = value;
    _bytes[index] = value;
    if (_pendingNibble->before == _pendingNibble->after)
      _pendingNibble->afterState = _pendingNibble->beforeState;
    _state = _pendingNibble->afterState;
    if (original != value)
      [self notifyDataWithPreviousModified:wasModified];
  }
  else {
    _pendingNibble = [self overwrite:index
                               bytes:&value
                               count:1
                                name:@"Edit Byte"];
  }
  if (_lowNibble) {
    [self resetNibble];
    if (index + 1 < static_cast<NSInteger>(_bytes.size()))
      [self setCursor:index + 1 extend:NO];
  }
  else
    _lowNibble = YES;
  self.needsDisplay = YES;
}
- (void)enterASCII:(uint8_t)value
{
  if (self.selectionLength != 1)
    return;
  NSInteger index = _cursor;
  [self resetNibble];
  [self overwrite:index bytes:&value count:1 name:@"Edit ASCII Byte"];
  if (index + 1 < static_cast<NSInteger>(_bytes.size()))
    [self setCursor:index + 1 extend:NO];
}
- (IBAction)copy:(id)sender
{
  [self resetNibble];
  NSInteger start = self.selectionStart, length = self.selectionLength;
  if (start < 0)
    return;
  std::string result;
  result.reserve(static_cast<size_t>(length) * 3 - 1);
  for (NSInteger i = 0; i < length; ++i) {
    if (i)
      result.push_back(' ');
    uint8_t byte = _bytes[start + i];
    result.push_back(kHex[byte >> 4]);
    result.push_back(kHex[byte & 15]);
  }
  NSString* text = [[NSString alloc] initWithBytes:result.data()
                                            length:result.size()
                                          encoding:NSASCIIStringEncoding];
  NSPasteboard* board = NSPasteboard.generalPasteboard;
  [board clearContents];
  [board setString:text forType:NSPasteboardTypeString];
}
- (void)showPasteError:(NSString*)message
{
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Cannot Paste";
  alert.informativeText = message;
  alert.alertStyle = NSAlertStyleWarning;
  if (self.window)
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
  else
    [alert runModal];
}
- (IBAction)paste:(id)sender
{
  [self resetNibble];
  NSInteger start = self.selectionStart;
  if (start < 0)
    return;
  NSString* text =
      [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
  if (!text.length)
    return;
  size_t available = _bytes.size() - start;
  if (_asciiPane) {
    NSData* utf8 = [text dataUsingEncoding:NSUTF8StringEncoding];
    if (utf8.length > available) {
      [self
          showPasteError:@"The UTF-8 text would extend beyond the end of the "
                         @"file. This editor only overwrites existing bytes."];
      return;
    }
    [self overwrite:start
              bytes:static_cast<const uint8_t*>(utf8.bytes)
              count:utf8.length
               name:@"Paste Bytes"];
    [self selectRange:start length:utf8.length];
    return;
  }
  // Validate the entire input before allocating or changing any bytes.
  size_t digits = 0;
  NSCharacterSet* whitespace = NSCharacterSet.whitespaceAndNewlineCharacterSet;
  for (NSUInteger i = 0; i < text.length; ++i) {
    unichar ch = [text characterAtIndex:i];
    if ([whitespace characterIsMember:ch])
      continue;
    if (hexDigit(ch) < 0) {
      [self
          showPasteError:@"Use only hexadecimal digits (0–9, A–F) and "
                         @"whitespace. Prefixes such as 0x are not accepted."];
      return;
    }
    ++digits;
  }
  if (digits & 1) {
    [self showPasteError:
              @"Hexadecimal input must contain two digits for every byte."];
    return;
  }
  if (digits / 2 > available) {
    [self showPasteError:@"The pasted bytes would extend beyond the end of the "
                         @"file. This editor only overwrites existing bytes."];
    return;
  }
  if (!digits)
    return;
  std::vector<uint8_t> bytes;
  bytes.reserve(digits / 2);
  int high = -1;
  for (NSUInteger i = 0; i < text.length; ++i) {
    int digit = hexDigit([text characterAtIndex:i]);
    if (digit < 0)
      continue;
    if (high < 0)
      high = digit;
    else {
      bytes.push_back((high << 4) | digit);
      high = -1;
    }
  }
  [self overwrite:start
            bytes:bytes.data()
            count:bytes.size()
             name:@"Paste Bytes"];
  [self selectRange:start length:bytes.size()];
}
- (IBAction)selectAll:(id)sender
{
  [self selectRange:0 length:_bytes.size()];
}
- (IBAction)zeroSelection:(id)sender
{
  [self resetNibble];
  NSInteger start = self.selectionStart, end = start + self.selectionLength;
  if (start < 0)
    return;
  while (start < end && !_bytes[start])
    ++start;
  while (end > start && !_bytes[end - 1])
    --end;
  if (start == end)
    return;
  std::vector<uint8_t> zero(end - start, 0);
  [self overwrite:start
            bytes:zero.data()
            count:zero.size()
             name:@"Zero Selection"];
}
- (IBAction)undo:(id)sender
{
  [self resetNibble];
  [_byteUndoManager undo];
}
- (IBAction)redo:(id)sender
{
  [self resetNibble];
  [_byteUndoManager redo];
}
- (BOOL)validateMenuItem:(NSMenuItem*)item
{
  SEL action = item.action;
  if (action == @selector(undo:))
    return _byteUndoManager.canUndo;
  if (action == @selector(redo:))
    return _byteUndoManager.canRedo;
  if (action == @selector(selectAll:))
    return !_bytes.empty();
  if (action == @selector(copy:) || action == @selector(paste:) ||
      action == @selector(zeroSelection:))
    return self.selectionLength > 0;
  return YES;
}
- (NSMenu*)menuForEvent:(NSEvent*)event
{
  [self resetNibble];
  [self.window makeFirstResponder:self];
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Byte Editor"];
  NSArray<NSString*>* titles = @[
    @"Copy Hex", _asciiPane ? @"Paste Text" : @"Paste Hex", @"Zero Selection",
    @"Select All", @"Undo", @"Redo"
  ];
  SEL actions[] = {@selector(copy:),          @selector(paste:),
                   @selector(zeroSelection:), @selector(selectAll:),
                   @selector(undo:),          @selector(redo:)};
  for (NSUInteger i = 0; i < titles.count; ++i) {
    if (i == 4)
      [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:titles[i]
                                                  action:actions[i]
                                           keyEquivalent:@""];
    item.target = self;
    [menu addItem:item];
  }
  return menu;
}
- (void)keyDown:(NSEvent*)event
{
  NSString* characters = event.charactersIgnoringModifiers;
  if (!characters.length)
    return;
  unichar key = [characters characterAtIndex:0];
  NSEventModifierFlags flags = event.modifierFlags;
  BOOL command = (flags & NSEventModifierFlagCommand) != 0;
  BOOL shift = (flags & NSEventModifierFlagShift) != 0;
  BOOL option = (flags & NSEventModifierFlagOption) != 0;
  if (command) {
    if (key >= 'A' && key <= 'Z')
      key += 'a' - 'A';
    switch (key) {
      case 'c':
        [self copy:nil];
        return;
      case 'v':
        [self paste:nil];
        return;
      case 'a':
        [self selectAll:nil];
        return;
      case 'z':
        if (shift)
          [self redo:nil];
        else
          [self undo:nil];
        return;
    }
  }
  if (key == '\t' || key == NSBackTabCharacter) {
    [self resetNibble];
    _asciiPane = !_asciiPane;
    [self ensureCursorVisible];
    return;
  }
  NSInteger index = std::max<NSInteger>(0, _cursor), destination = index;
  NSInteger page = std::max<NSInteger>(
      1,
      std::floor((self.visibleRect.size.height - _headerHeight) / _rowHeight));
  BOOL navigation = !option;
  switch (key) {
    case NSLeftArrowFunctionKey:
      destination = index - 1;
      break;
    case NSRightArrowFunctionKey:
      destination = index + 1;
      break;
    case NSUpArrowFunctionKey:
      destination = command ? 0 : index - 16;
      break;
    case NSDownArrowFunctionKey:
      destination =
          command ? static_cast<NSInteger>(_bytes.size()) - 1 : index + 16;
      break;
    case NSPageUpFunctionKey:
      destination = index - page * 16;
      break;
    case NSPageDownFunctionKey:
      destination = index + page * 16;
      break;
    case NSHomeFunctionKey:
      destination = command ? 0 : index - index % 16;
      break;
    case NSEndFunctionKey:
      destination = command ? static_cast<NSInteger>(_bytes.size()) - 1
                            : index - index % 16 + 15;
      break;
    default:
      navigation = NO;
      break;
  }
  if (navigation) {
    [self resetNibble];
    [self setCursor:destination extend:shift];
  }
  else if (!command && !option && event.characters.length == 1 &&
           self.selectionLength == 1) {
    unichar typed = [event.characters characterAtIndex:0];
    if (!_asciiPane && hexDigit(typed) >= 0)
      [self enterHexDigit:hexDigit(typed)];
    else if (_asciiPane && typed >= 32 && typed <= 126)
      [self enterASCII:typed];
    else
      NSBeep();
  }
  else
    [super keyDown:event];
}

- (NSInteger)indexAt:(NSPoint)point ascii:(BOOL*)ascii clamp:(BOOL)clamp
{
  if (_bytes.empty() ||
      (!clamp && point.y < self.visibleRect.origin.y + _headerHeight))
    return -1;
  NSInteger column = -1;
  if (clamp && *ascii)
    column = std::clamp<NSInteger>(
        std::floor((point.x - _asciiStart) / _charWidth), 0, 15);
  else if (!clamp && point.x >= _asciiStart &&
           point.x < _asciiStart + 16 * _charWidth) {
    *ascii = YES;
    column = std::floor((point.x - _asciiStart) / _charWidth);
  }
  else {
    for (NSInteger candidate = 0; candidate < 16; ++candidate) {
      CGFloat x = [self hexX:candidate];
      if (clamp ? point.x >= x || candidate == 0
                : point.x >= x && point.x < x + 3 * _charWidth)
        column = candidate;
    }
    if (!clamp && column >= 0)
      *ascii = NO;
  }
  if (column < 0)
    return -1;
  NSInteger row = std::max<NSInteger>(
      0, std::floor((point.y - _headerHeight) / _rowHeight));
  NSInteger index = row * 16 + column;
  if (!clamp && index >= static_cast<NSInteger>(_bytes.size()))
    return -1;
  return std::clamp<NSInteger>(index, 0,
                               static_cast<NSInteger>(_bytes.size()) - 1);
}
- (void)mouseDown:(NSEvent*)event
{
  if (event.modifierFlags & NSEventModifierFlagControl) {
    [super mouseDown:event];
    return;
  }
  NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  BOOL ascii = _asciiPane;
  NSInteger index = [self indexAt:point ascii:&ascii clamp:NO];
  if (index < 0)
    return;
  [self.window makeFirstResponder:self];
  [self resetNibble];
  _asciiPane = ascii;
  BOOL extend = (event.modifierFlags & NSEventModifierFlagShift) != 0;
  [self setCursor:index extend:extend];
  if (!extend && !ascii)
    _lowNibble = point.x >= [self hexX:index % 16] + _charWidth * 1.5;
  _dragging = YES;
  _dragWindowPoint = event.locationInWindow;
  __weak DHMemoryView* weakSelf = self;
  _dragTimer = [NSTimer timerWithTimeInterval:0.04
                                      repeats:YES
                                        block:^(NSTimer* timer) {
                                          (void)timer;
                                          [weakSelf updateDrag];
                                        }];
  [NSRunLoop.mainRunLoop addTimer:_dragTimer forMode:NSRunLoopCommonModes];
}
- (void)mouseDragged:(NSEvent*)event
{
  if (!_dragging)
    return;
  _dragWindowPoint = event.locationInWindow;
  [self updateDrag];
}
- (void)mouseUp:(NSEvent*)event
{
  if (_dragging) {
    _dragWindowPoint = event.locationInWindow;
    [self updateDrag];
  }
  [self stopDragging];
}
- (void)stopDragging
{
  _dragging = NO;
  [_dragTimer invalidate];
  _dragTimer = nil;
}
- (void)updateDrag
{
  if (!_dragging || _bytes.empty())
    return;
  NSRect visible = self.visibleRect;
  NSPoint point = [self convertPoint:_dragWindowPoint fromView:nil];
  NSPoint scroll = visible.origin;
  CGFloat top = NSMinY(visible) + _headerHeight;
  if (point.y < top)
    scroll.y -=
        std::min<CGFloat>(8, 1 + (top - point.y) / _rowHeight) * _rowHeight;
  else if (point.y >= NSMaxY(visible))
    scroll.y +=
        std::min<CGFloat>(8, 1 + (point.y - NSMaxY(visible)) / _rowHeight) *
        _rowHeight;
  if (point.x < NSMinX(visible))
    scroll.x -= _charWidth * 3;
  else if (point.x >= NSMaxX(visible))
    scroll.x += _charWidth * 3;
  [self scrollPoint:scroll];
  visible = self.visibleRect;
  point = [self convertPoint:_dragWindowPoint fromView:nil];
  point.y = std::clamp(
      point.y, NSMinY(visible) + _headerHeight,
      std::max(NSMinY(visible) + _headerHeight, NSMaxY(visible) - 1));
  BOOL ascii = _asciiPane;
  NSInteger index = [self indexAt:point ascii:&ascii clamp:YES];
  if (index != _cursor) {
    [self resetNibble];
    [self setCursor:index extend:YES];
  }
}

- (void)drawRect:(NSRect)dirtyRect
{
  NSRect visible = self.visibleRect;
  NSColor* base = _terminalTheme ? rgb(.055, .065, .08, _translucent ? .24 : 1)
                                 : NSColor.textBackgroundColor;
  NSColor* alternate =
      _terminalTheme ? rgb(.12, .10, .17, _translucent ? .30 : 1)
                     : NSColor.alternatingContentBackgroundColors.lastObject;
  NSColor* text = _terminalTheme ? rgb(.86, .89, .91) : NSColor.textColor;
  NSColor* muted =
      _terminalTheme ? rgb(.43, .49, .55) : NSColor.secondaryLabelColor;
  NSColor* accent =
      _terminalTheme ? rgb(.33, .82, .88) : NSColor.secondaryLabelColor;
  NSColor* printable =
      _terminalTheme ? rgb(.42, .85, .72) : NSColor.systemTealColor;
  BOOL focused = self.window.firstResponder == self;
  NSColor* selection =
      _terminalTheme
          ? rgb(.28, .30, .48)
          : (focused ? NSColor.selectedTextBackgroundColor
                     : NSColor.unemphasizedSelectedTextBackgroundColor);
  NSColor* selectedText =
      _terminalTheme ? rgb(.98, .98, 1) : NSColor.selectedTextColor;
  NSMutableDictionary* attributes =
      [@{NSFontAttributeName : _font, NSForegroundColorAttributeName : text}
          mutableCopy];
  auto drawText = [&](NSString* value, CGFloat x, CGFloat y, NSColor* color) {
    attributes[NSForegroundColorAttributeName] = color;
    [value drawAtPoint:NSMakePoint(x, y + (_rowHeight -
                                           (_font.ascender - _font.descender)) /
                                              2)
        withAttributes:attributes];
  };
  [base setFill];
  NSRectFillUsingOperation(dirtyRect, NSCompositingOperationCopy);
  if (_bytes.empty()) {
    NSString* message = @"No bytes to display\nOpen a file to begin editing.";
    attributes[NSForegroundColorAttributeName] = muted;
    NSSize size = [message sizeWithAttributes:attributes];
    [message drawAtPoint:NSMakePoint(NSMidX(visible) - size.width / 2,
                                     NSMidY(visible) - size.height / 2)
          withAttributes:attributes];
    return;
  }
  NSInteger first = self.visibleByteStart / 16;
  NSInteger last = (self.visibleByteEnd + 15) / 16;
  NSInteger selectedStart = self.selectionStart;
  NSInteger selectedEnd = selectedStart + self.selectionLength;
  [NSGraphicsContext saveGraphicsState];
  NSRectClip(NSMakeRect(
      NSMinX(visible), NSMinY(visible) + _headerHeight, visible.size.width,
      std::max<CGFloat>(0, visible.size.height - _headerHeight)));
  for (NSInteger row = first; row < last; ++row) {
    CGFloat y = _headerHeight + row * _rowHeight;
    if (y + _rowHeight < NSMinY(dirtyRect) || y > NSMaxY(dirtyRect))
      continue;
    if (row & 1) {
      [alternate setFill];
      NSRectFillUsingOperation(
          NSMakeRect(visible.origin.x, y, visible.size.width, _rowHeight),
          NSCompositingOperationCopy);
    }
    char offset[32];
    std::snprintf(offset, sizeof(offset), "%08llX",
                  static_cast<unsigned long long>(row) * 16);
    drawText([NSString stringWithUTF8String:offset], _charWidth, y, accent);
    for (NSInteger column = 0; column < 16; ++column) {
      NSInteger index = row * 16 + column;
      if (index >= static_cast<NSInteger>(_bytes.size()))
        break;
      uint8_t byte = _bytes[index];
      BOOL isPrintable = byte >= 32 && byte <= 126;
      BOOL selected = index >= selectedStart && index < selectedEnd;
      NSRect hexRect =
          NSMakeRect([self hexX:column], y, 3 * _charWidth, _rowHeight);
      NSRect asciiRect = NSMakeRect(_asciiStart + column * _charWidth, y,
                                    _charWidth, _rowHeight);
      if (selected) {
        [selection setFill];
        NSRectFill(hexRect);
        NSRectFill(asciiRect);
      }
      NSColor* color = selected      ? selectedText
                       : !byte       ? muted
                       : isPrintable ? printable
                                     : text;
      drawText(hexText(byte), hexRect.origin.x + _charWidth / 2, y, color);
      drawText(asciiText(byte), asciiRect.origin.x, y, color);
      if (index == _cursor) {
        [(focused ? selectedText : muted) setStroke];
        [NSBezierPath strokeRect:NSInsetRect(hexRect, .5, 1.5)];
        [NSBezierPath strokeRect:NSInsetRect(asciiRect, .5, 1.5)];
        if (focused) {
          [(selected ? selectedText : text) setFill];
          CGFloat x = _asciiPane ? asciiRect.origin.x
                                 : hexRect.origin.x + _charWidth / 2 +
                                       (_lowNibble ? _charWidth : 0);
          NSRectFill(NSMakeRect(x, y + _rowHeight - 4, _charWidth, 2));
        }
      }
    }
  }
  [NSGraphicsContext restoreGraphicsState];
  CGFloat headerY = NSMinY(visible);
  [base setFill];
  NSRectFillUsingOperation(
      NSMakeRect(visible.origin.x, headerY, visible.size.width, _headerHeight),
      NSCompositingOperationCopy);
  drawText(@"OFFSET", _charWidth * 3, headerY + 4, accent);
  for (NSInteger column = 0; column < 16; ++column)
    drawText(hexText(column), [self hexX:column] + _charWidth / 2, headerY + 4,
             accent);
  drawText(@"ASCII", _asciiStart, headerY + 4, accent);
  [(_terminalTheme ? rgb(.32, .24, .39) : NSColor.separatorColor) setFill];
  NSRectFill(NSMakeRect(visible.origin.x, headerY + _headerHeight - 1,
                        visible.size.width, 1));
}
@end
