#import "heatmap.h"
#import "memoryview.h"

#include <algorithm>

@interface DHHeatMap () {
  std::vector<MapRegion> _regions;
  uint64_t _totalSize, _viewStart, _viewEnd;
  BOOL _dragging;
}

@end

@implementation DHHeatMap

- (instancetype)initWithFrame:(NSRect)frame
{
  if ((self = [super initWithFrame:frame])) {
    self.accessibilityElement = YES;
    self.accessibilityRole = NSAccessibilitySliderRole;
    self.accessibilityLabel = @"File overview";
    self.accessibilityHelp =
        @"Click or drag to navigate the visible byte window.";
    [self rebuildToolTip];
  }
  return self;
}

- (BOOL)isFlipped
{
  return YES;
}

- (void)setRegions:(const std::vector<MapRegion>&)regions
         totalSize:(uint64_t)size
{
  _regions = regions;
  _totalSize = size;
  if (!size)
    _dragging = NO;
  [self updateViewport];
  [self rebuildToolTip];
}

- (void)setMemoryView:(DHMemoryView*)view
{
  _memoryView = view;
  [self updateViewport];
}

- (void)updateViewport
{
  _viewStart = std::min(_memoryView.visibleByteStart, _totalSize);
  _viewEnd =
      std::max(_viewStart, std::min(_memoryView.visibleByteEnd, _totalSize));
  self.needsDisplay = YES;
}

- (void)viewDidChangeEffectiveAppearance
{
  [super viewDidChangeEffectiveAppearance];
  self.needsDisplay = YES;
}

- (void)setFrameSize:(NSSize)size
{
  [super setFrameSize:size];
  [self rebuildToolTip];
}

- (void)rebuildToolTip
{
  [self removeAllToolTips];
  if (self.bounds.size.width > 0 && self.bounds.size.height > 0)
    [self addToolTipRect:self.bounds owner:self userData:nullptr];
}

- (void)resetCursorRects
{
  [super resetCursorRects];
  [self addCursorRect:self.bounds cursor:NSCursor.pointingHandCursor];
}

- (uint64_t)offsetAt:(CGFloat)x
{
  CGFloat width = self.bounds.size.width;
  if (!_totalSize || width <= 1)
    return 0;
  long double fraction =
      std::clamp<CGFloat>(x - self.bounds.origin.x, 0, width - 1) / (width - 1);
  return static_cast<uint64_t>(fraction * (_totalSize - 1));
}

- (NSString*)view:(NSView*)view
    stringForToolTip:(NSToolTipTag)tag
               point:(NSPoint)point
            userData:(void*)data
{
  if (!_totalSize)
    return @"Empty file";
  uint64_t offset = [self offsetAt:point.x];
  const MapRegion* match = nullptr;
  // Maps normally arrive ordered; a linear lookup also handles callers that
  // supply sparse or overlapping regions without unsafe iterator arithmetic.
  for (const auto& region : _regions)
    if (region.start <= offset && offset < region.end) {
      match = &region;
      break;
    }
  NSString* label = match ? [NSString stringWithUTF8String:match->label.c_str()]
                          : @"Unmapped";
  if (!label)
    label = @"Unmapped";
  NSString* range =
      match
          ? [NSString
                stringWithFormat:@"\nRange [0x%llX, 0x%llX)",
                                 static_cast<unsigned long long>(match->start),
                                 static_cast<unsigned long long>(match->end)]
          : @"";
  return [NSString stringWithFormat:@"%@\nOffset 0x%llX (%llu)%@", label,
                                    static_cast<unsigned long long>(offset),
                                    static_cast<unsigned long long>(offset),
                                    range];
}

- (void)drawRect:(NSRect)dirtyRect
{
  NSColor* background =
      _memoryView.terminalTheme
          ? [NSColor colorWithSRGBRed:.055
                                green:.065
                                 blue:.08
                                alpha:_memoryView.translucent ? .24 : 1]
          : NSColor.textBackgroundColor;
  [background setFill];
  NSRectFillUsingOperation(dirtyRect, NSCompositingOperationCopy);
  CGFloat width = self.bounds.size.width;
  if (!_totalSize || width <= 0)
    return;
  auto pixel = [&](uint64_t offset) {
    return self.bounds.origin.x +
           static_cast<CGFloat>(
               static_cast<long double>(std::min(offset, _totalSize)) /
               _totalSize * width);
  };
  for (const auto& region : _regions) {
    CGFloat left = pixel(region.start), right = pixel(region.end);
    if (right <= left)
      continue;
    [[NSColor colorWithSRGBRed:region.color.red
                         green:region.color.green
                          blue:region.color.blue
                         alpha:1] setFill];
    NSRectFill(NSMakeRect(left, self.bounds.origin.y, right - left,
                          self.bounds.size.height));
  }
  uint64_t start = std::min(_viewStart, _totalSize);
  uint64_t end = std::max(start, std::min(_viewEnd, _totalSize));
  if (start >= end)
    return;
  CGFloat markerWidth =
      std::min(width, std::max<CGFloat>(2, pixel(end) - pixel(start)));
  CGFloat left = std::clamp(pixel(start), NSMinX(self.bounds),
                            NSMaxX(self.bounds) - markerWidth);
  NSRect marker = NSMakeRect(left, NSMinY(self.bounds) + 1, markerWidth,
                             std::max<CGFloat>(0, self.bounds.size.height - 2));
  [[NSColor.selectedControlColor colorWithAlphaComponent:.37] setFill];
  NSRectFill(marker);
  [NSColor.labelColor setStroke];
  [NSBezierPath strokeRect:NSInsetRect(marker, .5, .5)];
  if (markerWidth >= 4 && marker.size.height >= 6) {
    [NSColor.textBackgroundColor setStroke];
    [NSBezierPath strokeRect:NSInsetRect(marker, 1.5, 1.5)];
  }
}

- (void)navigateAt:(CGFloat)x
{
  if (!_memoryView || !_totalSize || self.bounds.size.width <= 0)
    return;
  // Clamp both endpoints before subtracting: a replaced document can deliver
  // its viewport before its new regions, and must never underflow this span.
  uint64_t start = std::min(_viewStart, _totalSize);
  uint64_t end = std::max(start, std::min(_viewEnd, _totalSize));
  uint64_t span = end - start;
  uint64_t offset = [self offsetAt:x];
  uint64_t half = span / 2;
  uint64_t centered = offset >= half ? offset - half : 0;
  uint64_t maximum = span ? _totalSize - span : _totalSize - 1;
  [_memoryView moveToOffset:std::min(centered, maximum) select:NO];
  [self updateViewport];
}

- (void)mouseDown:(NSEvent*)event
{
  if (!_memoryView || !_totalSize)
    return;
  _dragging = YES;
  [self navigateAt:[self convertPoint:event.locationInWindow fromView:nil].x];
}

- (void)mouseDragged:(NSEvent*)event
{
  if (_dragging)
    [self navigateAt:[self convertPoint:event.locationInWindow fromView:nil].x];
}

- (void)mouseUp:(NSEvent*)event
{
  if (_dragging)
    [self navigateAt:[self convertPoint:event.locationInWindow fromView:nil].x];
  _dragging = NO;
}

- (id)accessibilityValue
{
  return @(_viewStart);
}

- (id)accessibilityMinValue
{
  return @0;
}

- (id)accessibilityMaxValue
{
  return @(_totalSize ? _totalSize - 1 : 0);
}

- (void)setAccessibilityValue:(id)value
{
  if (![value respondsToSelector:@selector(unsignedLongLongValue)] ||
      !_totalSize)
    return;
  [_memoryView moveToOffset:std::min<uint64_t>([value unsignedLongLongValue],
                                               _totalSize - 1)
                     select:NO];
  [self updateViewport];
}

@end
