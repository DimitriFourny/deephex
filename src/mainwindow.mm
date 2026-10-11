#import "mainwindow.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <set>
#include "elf_map.h"
#import "heatmap.h"
#include "macho_map.h"

namespace {
constexpr size_t MaxFileSize = 256 * 1024 * 1024;
NSString* offsetText(uint64_t value)
{
  return [NSString stringWithFormat:@"0x%08llX", (unsigned long long)value];
}
NSString* text(const std::string& value)
{
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}
NSTextField* label(NSString* value, CGFloat size = 13, BOOL bold = NO)
{
  NSTextField* field = [NSTextField wrappingLabelWithString:value];
  field.font = bold ? [NSFont boldSystemFontOfSize:size]
                    : [NSFont systemFontOfSize:size];
  return field;
}
NSButton* button(NSString* title, id target, SEL action)
{
  return [NSButton buttonWithTitle:title target:target action:action];
}
NSStackView* stack(NSArray<NSView*>* views,
                   NSUserInterfaceLayoutOrientation orientation)
{
  NSStackView* view = [NSStackView stackViewWithViews:views];
  view.alignment = orientation == NSUserInterfaceLayoutOrientationVertical
                       ? NSLayoutAttributeLeading
                       : NSLayoutAttributeCenterY;
  view.orientation = orientation;
  view.spacing = 10;
  view.translatesAutoresizingMaskIntoConstraints = NO;
  return view;
}
NSMenuItem* menuItem(NSMenu* menu,
                     NSString* title,
                     SEL action,
                     NSString* key,
                     id target,
                     BOOL shift = NO)
{
  NSMenuItem* item = [menu addItemWithTitle:title
                                     action:action
                              keyEquivalent:key];
  item.target = target;
  item.keyEquivalentModifierMask =
      NSEventModifierFlagCommand | (shift ? NSEventModifierFlagShift : 0);
  return item;
}
NSMenu* submenu(NSMenu* menu, NSString* title)
{
  NSMenuItem* item = [menu addItemWithTitle:title action:nil keyEquivalent:@""];
  item.submenu = [[NSMenu alloc] initWithTitle:title];
  return item.submenu;
}
}  // namespace

@interface DHMainWindowController ()
- (void)updateOverview;
- (BOOL)usesTerminalAppearance;
- (void)refreshAppearance;
@end

@interface DHWorkbenchView : NSVisualEffectView
@property(nonatomic, weak) DHMainWindowController* controller;
@property(nonatomic) BOOL terminal;
@property(nonatomic) BOOL translucent;
@end
@implementation DHWorkbenchView
- (instancetype)initWithFrame:(NSRect)frame
{
  if ((self = [super initWithFrame:frame]))
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
  return self;
}
- (void)viewDidChangeEffectiveAppearance
{
  [super viewDidChangeEffectiveAppearance];
  [self.controller refreshAppearance];
}
- (void)drawRect:(NSRect)rect
{
  [super drawRect:rect];
  NSColor* color = self.terminal
                       ? [NSColor colorWithSRGBRed:0.082
                                             green:0.086
                                              blue:0.11
                                             alpha:self.translucent ? 0.08 : 1]
                       : NSColor.windowBackgroundColor;
  [color setFill];
  NSRectFillUsingOperation(rect, NSCompositingOperationSourceOver);
}
- (NSArray<NSURL*>*)droppedURLs:(id<NSDraggingInfo>)sender
{
  return [sender.draggingPasteboard
      readObjectsForClasses:@[ NSURL.class ]
                    options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
  return [self droppedURLs:sender].count == 1 ? NSDragOperationCopy
                                              : NSDragOperationNone;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
  NSArray<NSURL*>* urls = [self droppedURLs:sender];
  return urls.count == 1 && [self.controller openFile:urls.firstObject.path];
}
@end

@implementation DHMainWindowController {
  DHMemoryView* _editor;
  DHHeatMap* _overview;
  DHWorkbenchView* _root;
  NSStackView *_content, *_findBar, *_sidebar;
  NSSplitView* _split;
  NSScrollView *_editorScroll, *_regionScroll;
  NSTextField *_fileLabel, *_detailLabel, *_legend, *_status, *_searchStatus;
  NSSearchField* _searchInput;
  NSPopUpButton *_searchMode, *_byteOrder;
  NSTextView* _inspector;
  NSTableView* _regionTable;
  NSMenu *_recentMenu, *_appearanceMenu;
  NSMenuItem *_transparencyItem, *_inspectorItem;
  NSString *_path, *_lastQuery;
  NSDate* _diskModified;
  unsigned long long _diskSize;
  NSInteger _appearance, _lastMode, _lastMatch;
  BOOL _transparencyPreferred, _loading, _cancelLoading;
  NSTimer* _overviewTimer;
  std::vector<MapRegion> _regions;
}
- (instancetype)init
{
  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 1180, 760)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable |
                          NSWindowStyleMaskFullSizeContentView
                  backing:NSBackingStoreBuffered
                    defer:NO];
  if (!(self = [super initWithWindow:window]))
    return nil;
  window.delegate = self;
  window.title = @"DeepHex";
  window.minSize = NSMakeSize(780, 480);
  window.titlebarAppearsTransparent = YES;
  window.releasedWhenClosed = NO;
  [window center];
  [window setFrameAutosaveName:@"DeepHexWindow"];
  _lastMatch = -1;
  _lastMode = -1;
  NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
  [defaults registerDefaults:@{
    @"terminalTransparency" : @YES,
    @"appearance" : @0,
    @"showInspector" : @YES
  }];
  NSInteger storedAppearance = [defaults integerForKey:@"appearance"];
  _appearance = std::clamp<NSInteger>(storedAppearance, 0, 2);
  if (_appearance != storedAppearance)
    [defaults setInteger:_appearance forKey:@"appearance"];
  _transparencyPreferred = [defaults boolForKey:@"terminalTransparency"];
  _root = [[DHWorkbenchView alloc] initWithFrame:window.contentView.bounds];
  _root.controller = self;
  _root.material = NSVisualEffectMaterialHUDWindow;
  _root.blendingMode = NSVisualEffectBlendingModeBehindWindow;
  _root.state = NSVisualEffectStateActive;
  window.contentView = _root;
  _fileLabel = label(@"Open a file to explore", 21, YES);
  _detailLabel = label(@"Drop a file here, or press ⌘O. Changes overwrite "
                       @"bytes without shifting offsets.");
  _detailLabel.textColor = NSColor.secondaryLabelColor;
  NSStackView* titles = stack(@[ _fileLabel, _detailLabel ],
                              NSUserInterfaceLayoutOrientationVertical);
  NSStackView* heading =
      stack(@[ titles, button(@"Open File…", self, @selector(openDocument:)) ],
            NSUserInterfaceLayoutOrientationHorizontal);
  heading.alignment = NSLayoutAttributeCenterY;
  [titles setContentHuggingPriority:NSLayoutPriorityDefaultLow
                     forOrientation:NSLayoutConstraintOrientationHorizontal];
  _overview = [[DHHeatMap alloc] initWithFrame:NSMakeRect(0, 0, 900, 24)];
  _legend = label(@"");
  _legend.font = [NSFont systemFontOfSize:11];
  _searchMode = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [_searchMode addItemsWithTitles:@[ @"Hex bytes", @"UTF-8 text" ]];
  _searchInput = [[NSSearchField alloc] initWithFrame:NSZeroRect];
  _searchInput.placeholderString = @"Hex pairs or case-sensitive UTF-8 text";
  _searchInput.target = self;
  _searchInput.action = @selector(findNext:);
  [_searchInput
      setContentHuggingPriority:NSLayoutPriorityDefaultLow
                 forOrientation:NSLayoutConstraintOrientationHorizontal];
  _searchStatus = label(@"");
  _findBar = stack(
      @[
        _searchMode, _searchInput, _searchStatus,
        button(@"Previous", self, @selector(findPrevious:)),
        button(@"Next", self, @selector(findNext:)),
        button(@"Done", self, @selector(hideFind:))
      ],
      NSUserInterfaceLayoutOrientationHorizontal);
  _findBar.alignment = NSLayoutAttributeCenterY;
  _findBar.hidden = YES;
  _editor = [[DHMemoryView alloc] initWithFrame:NSMakeRect(0, 0, 900, 600)];
  _editor.delegate = self;
  _overview.memoryView = _editor;
  _editorScroll =
      [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 860, 600)];
  _editorScroll.documentView = _editor;
  _editorScroll.hasVerticalScroller = YES;
  _editorScroll.hasHorizontalScroller = YES;
  _editorScroll.autohidesScrollers = YES;
  _editorScroll.borderType = NSBezelBorder;
  _byteOrder = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [_byteOrder addItemsWithTitles:@[ @"Little endian", @"Big endian" ]];
  _byteOrder.target = self;
  _byteOrder.action = @selector(byteOrderChanged:);
  _inspector = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 260, 320)];
  _inspector.editable = NO;
  _inspector.selectable = YES;
  _inspector.drawsBackground = NO;
  _inspector.font = [NSFont monospacedSystemFontOfSize:12
                                                weight:NSFontWeightRegular];
  _inspector.textContainerInset = NSMakeSize(6, 6);
  NSScrollView* inspectorScroll =
      [[NSScrollView alloc] initWithFrame:NSZeroRect];
  inspectorScroll.documentView = _inspector;
  inspectorScroll.hasVerticalScroller = YES;
  inspectorScroll.drawsBackground = NO;
  _inspector.verticallyResizable = YES;
  _inspector.horizontallyResizable = NO;
  _inspector.autoresizingMask = NSViewWidthSizable;
  _inspector.textContainer.widthTracksTextView = YES;
  _regionTable = [[NSTableView alloc] initWithFrame:NSZeroRect];
  for (NSString* name in @[ @"Region", @"Offset" ]) {
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:name];
    column.title = name;
    column.width = [name isEqual:@"Region"] ? 170 : 100;
    [_regionTable addTableColumn:column];
  }
  _regionTable.dataSource = self;
  _regionTable.delegate = self;
  _regionTable.usesAlternatingRowBackgroundColors = YES;
  _regionTable.target = self;
  _regionTable.doubleAction = @selector(navigateRegion:);
  _regionScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  _regionScroll.documentView = _regionTable;
  _regionScroll.hasVerticalScroller = YES;
  _regionScroll.hasHorizontalScroller = YES;
  _sidebar = stack(
      @[
        label(@"DATA INSPECTOR", 12, YES), _byteOrder, inspectorScroll,
        label(@"FILE REGIONS", 12, YES), _regionScroll
      ],
      NSUserInterfaceLayoutOrientationVertical);
  _sidebar.edgeInsets = NSEdgeInsetsMake(0, 12, 0, 0);
  _sidebar.hidden = ![defaults boolForKey:@"showInspector"];
  _sidebar.spacing = 8;
  _split = [[NSSplitView alloc] initWithFrame:NSMakeRect(0, 0, 1140, 540)];
  _split.vertical = YES;
  _split.delegate = self;
  _split.dividerStyle = NSSplitViewDividerStyleThin;
  [_split addSubview:_editorScroll];
  [_split addSubview:_sidebar];
  _split.autosaveName = @"DeepHexSplit";
  _sidebar.translatesAutoresizingMaskIntoConstraints = YES;
  _sidebar.frame = NSMakeRect(870, 0, 270, 540);
  _editorScroll.frame = NSMakeRect(0, 0, 869, 540);
  _status = label(@"Ready");
  NSStackView* statusRow =
      stack(@[ _status, label(@"OVERWRITE · HEX / ASCII", 11) ],
            NSUserInterfaceLayoutOrientationHorizontal);
  [_status setContentHuggingPriority:NSLayoutPriorityDefaultLow
                      forOrientation:NSLayoutConstraintOrientationHorizontal];
  _content =
      stack(@[ heading, _overview, _legend, _findBar, _split, statusRow ],
            NSUserInterfaceLayoutOrientationVertical);
  [_root addSubview:_content];
  for (NSView* view in _content.arrangedSubviews) {
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [view.widthAnchor constraintEqualToAnchor:_content.widthAnchor].active =
        YES;
  }
  [NSLayoutConstraint activateConstraints:@[
    [_content.leadingAnchor constraintEqualToAnchor:_root.leadingAnchor
                                           constant:20],
    [_content.trailingAnchor constraintEqualToAnchor:_root.trailingAnchor
                                            constant:-20],
    [_content.topAnchor
        constraintEqualToAnchor:_root.safeAreaLayoutGuide.topAnchor
                       constant:16],
    [_content.bottomAnchor constraintEqualToAnchor:_root.bottomAnchor
                                          constant:-12],
    [_overview.heightAnchor constraintEqualToConstant:24],
    [inspectorScroll.heightAnchor constraintGreaterThanOrEqualToConstant:70],
    [_regionScroll.heightAnchor constraintGreaterThanOrEqualToConstant:40]
  ]];
  [_split setContentHuggingPriority:1
                     forOrientation:NSLayoutConstraintOrientationVertical];
  NSLayoutConstraint* inspectorHeight =
      [inspectorScroll.heightAnchor constraintEqualToConstant:240];
  inspectorHeight.priority = NSLayoutPriorityDefaultLow;
  inspectorHeight.active = YES;
  [inspectorScroll
      setContentHuggingPriority:2
                 forOrientation:NSLayoutConstraintOrientationVertical];
  [_regionScroll
      setContentHuggingPriority:1
                 forOrientation:NSLayoutConstraintOrientationVertical];
  for (NSView* view in _sidebar.arrangedSubviews) {
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [view.widthAnchor constraintEqualToAnchor:_sidebar.widthAnchor constant:-12]
        .active = YES;
  }
  NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"DeepHexToolbar"];
  toolbar.delegate = self;
  toolbar.displayMode = NSToolbarDisplayModeIconAndLabel;
  toolbar.allowsUserCustomization = YES;
  toolbar.autosavesConfiguration = YES;
  window.toolbar = toolbar;
  [NSWorkspace.sharedWorkspace.notificationCenter
      addObserver:self
         selector:@selector(accessibilityChanged:)
             name:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
           object:nil];
  [self applyAppearance];
  [self updateDocument];
  [self updateSelection];
  return self;
}
- (void)dealloc
{
  [_overviewTimer invalidate];
  [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self];
}
- (DHMemoryView*)editor
{
  return _editor;
}
- (void)installMenus
{
  NSMenu* main = [[NSMenu alloc] initWithTitle:@"Main"];
  NSMenu* app = submenu(main, @"DeepHex");
  menuItem(app, @"About DeepHex", @selector(orderFrontStandardAboutPanel:), @"",
           NSApp);
  [app addItem:NSMenuItem.separatorItem];
  NSMenu* services = submenu(app, @"Services");
  NSApp.servicesMenu = services;
  menuItem(app, @"Hide DeepHex", @selector(hide:), @"h", NSApp);
  NSMenuItem* hideOthers = menuItem(
      app, @"Hide Others", @selector(hideOtherApplications:), @"h", NSApp);
  hideOthers.keyEquivalentModifierMask |= NSEventModifierFlagOption;
  menuItem(app, @"Show All", @selector(unhideAllApplications:), @"", NSApp);
  [app addItem:NSMenuItem.separatorItem];
  menuItem(app, @"Quit DeepHex", @selector(terminate:), @"q", NSApp);
  NSMenu* file = submenu(main, @"File");
  menuItem(file, @"Open…", @selector(openDocument:), @"o", self);
  _recentMenu = submenu(file, @"Open Recent");
  [file addItem:NSMenuItem.separatorItem];
  menuItem(file, @"Save", @selector(saveDocument:), @"s", self);
  menuItem(file, @"Save As…", @selector(saveDocumentAs:), @"s", self, YES);
  menuItem(file, @"Close Window", @selector(performClose:), @"w", self.window);
  NSMenu* edit = submenu(main, @"Edit");
  menuItem(edit, @"Undo", @selector(undo:), @"z", nil);
  menuItem(edit, @"Redo", @selector(redo:), @"z", nil, YES);
  [edit addItem:NSMenuItem.separatorItem];
  menuItem(edit, @"Copy as Hex", @selector(copy:), @"c", nil);
  menuItem(edit, @"Paste / Overwrite", @selector(paste:), @"v", nil);
  menuItem(edit, @"Select All", @selector(selectAll:), @"a", nil);
  menuItem(edit, @"Zero Selected Bytes", @selector(zeroSelection:), @"", nil);
  [edit addItem:NSMenuItem.separatorItem];
  menuItem(edit, @"Find…", @selector(showFind:), @"f", self);
  menuItem(edit, @"Find Next", @selector(findNext:), @"g", self);
  menuItem(edit, @"Find Previous", @selector(findPrevious:), @"g", self, YES);
  menuItem(edit, @"Go to Offset…", @selector(goToOffset:), @"l", self);
  NSMenu* view = submenu(main, @"View");
  _inspectorItem =
      menuItem(view, @"Show Inspector", @selector(toggleInspector:), @"", self);
  _appearanceMenu = submenu(view, @"Appearance");
  NSInteger i = 0;
  for (NSString* name in @[ @"System", @"Light", @"Terminal" ]) {
    NSMenuItem* item = menuItem(_appearanceMenu, name,
                                @selector(selectAppearance:), @"", self);
    item.tag = i++;
  }
  [_appearanceMenu addItem:NSMenuItem.separatorItem];
  _transparencyItem = menuItem(_appearanceMenu, @"Translucent Background",
                               @selector(toggleTransparency:), @"", self);
  NSMenu* window = submenu(main, @"Window");
  NSApp.windowsMenu = window;
  menuItem(window, @"Minimize", @selector(performMiniaturize:), @"m", nil);
  menuItem(window, @"Zoom", @selector(performZoom:), @"", nil);
  NSApp.mainMenu = main;
  [self updateRecentFiles];
  [self applyAppearance];
}
- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:
    (NSToolbar*)toolbar
{
  (void)toolbar;
  return @[
    @"Open", @"Save", @"Undo", @"Redo", @"Find", @"Offset",
    NSToolbarFlexibleSpaceItemIdentifier, NSToolbarSpaceItemIdentifier
  ];
}
- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:
    (NSToolbar*)toolbar
{
  (void)toolbar;
  return @[
    @"Open", @"Save", @"Undo", @"Redo", NSToolbarFlexibleSpaceItemIdentifier,
    @"Find", @"Offset"
  ];
}
- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar
        itemForItemIdentifier:(NSToolbarItemIdentifier)identifier
    willBeInsertedIntoToolbar:(BOOL)inserted
{
  (void)toolbar;
  (void)inserted;
  NSDictionary* symbols = @{
    @"Open" : @"folder",
    @"Save" : @"square.and.arrow.down",
    @"Undo" : @"arrow.uturn.backward",
    @"Redo" : @"arrow.uturn.forward",
    @"Find" : @"magnifyingglass",
    @"Offset" : @"number"
  };
  NSDictionary* actions = @{
    @"Open" : @"openDocument:",
    @"Save" : @"saveDocument:",
    @"Undo" : @"undo:",
    @"Redo" : @"redo:",
    @"Find" : @"showFind:",
    @"Offset" : @"goToOffset:"
  };
  NSToolbarItem* item =
      [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
  item.label = identifier;
  item.image = [NSImage imageWithSystemSymbolName:symbols[identifier]
                         accessibilityDescription:identifier];
  item.target = ([identifier isEqual:@"Undo"] || [identifier isEqual:@"Redo"])
                    ? nil
                    : self;
  item.action = NSSelectorFromString(actions[identifier]);
  return item;
}
- (BOOL)validateMenuItem:(NSMenuItem*)item
{
  if (item.action == @selector(saveDocument:))
    return _path && _editor.modified && !_loading;
  if (item.action == @selector(saveDocumentAs:) ||
      item.action == @selector(showFind:) ||
      item.action == @selector(findNext:) ||
      item.action == @selector(findPrevious:) ||
      item.action == @selector(goToOffset:))
    return _path && !_loading;
  if (item.action == @selector(toggleTransparency:))
    return [self usesTerminalAppearance];
  if (item.action == @selector(openDocument:) ||
      item.action == @selector(openRecent:))
    return !_loading;
  return YES;
}
- (BOOL)validateToolbarItem:(NSToolbarItem*)item
{
  if (item.action == @selector(saveDocument:))
    return _path && _editor.modified && !_loading;
  if (item.action == @selector(showFind:) ||
      item.action == @selector(goToOffset:))
    return _path && !_loading;
  return !_loading;
}
- (void)warn:(NSString*)title message:(NSString*)message
{
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = title;
  alert.informativeText = message;
  [alert runModal];
}
- (void)openDocument:(id)sender
{
  (void)sender;
  if (_loading)
    return;
  NSOpenPanel* panel = NSOpenPanel.openPanel;
  panel.canChooseDirectories = NO;
  panel.allowsMultipleSelection = NO;
  if ([panel runModal] == NSModalResponseOK)
    [self openFile:panel.URL.path];
}
- (void)cancelOpen:(id)sender
{
  (void)sender;
  _cancelLoading = YES;
}
- (BOOL)openFile:(NSString*)path
{
  if (_loading || ![self maybeSave])
    return NO;
  NSString* absolute = path.stringByStandardizingPath;
  if (!absolute.isAbsolutePath)
    absolute = [[NSFileManager.defaultManager currentDirectoryPath]
        stringByAppendingPathComponent:absolute];
  int fd = open(absolute.fileSystemRepresentation, O_RDONLY);
  if (fd < 0) {
    [self warn:@"Cannot Open File" message:@(strerror(errno))];
    return NO;
  }
  struct stat info;
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
      (uint64_t)info.st_size > MaxFileSize) {
    close(fd);
    [self warn:@"Cannot Open File"
        message:@"Choose a regular file up to 256 MiB."];
    return NO;
  }
  std::vector<uint8_t> bytes;
  try {
    bytes.resize(static_cast<size_t>(info.st_size));
  } catch (const std::bad_alloc&) {
    close(fd);
    [self warn:@"Cannot Open File"
        message:@"Not enough memory to open this file."];
    return NO;
  }
  _loading = YES;
  _cancelLoading = NO;
  NSPanel* progress =
      [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 350, 100)
                                 styleMask:NSWindowStyleMaskTitled
                                   backing:NSBackingStoreBuffered
                                     defer:NO];
  progress.title = @"Opening file…";
  NSProgressIndicator* indicator =
      [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 55, 310, 16)];
  indicator.indeterminate = NO;
  indicator.maxValue = bytes.size();
  [progress.contentView addSubview:indicator];
  NSButton* cancel = button(@"Cancel", self, @selector(cancelOpen:));
  cancel.frame = NSMakeRect(245, 10, 85, 30);
  [progress.contentView addSubview:cancel];
  CFAbsoluteTime started = CFAbsoluteTimeGetCurrent();
  BOOL shown = NO;
  size_t count = 0;
  NSString* error = nil;
  while (count < bytes.size()) {
    ssize_t n = read(fd, bytes.data() + count,
                     std::min<size_t>(1024 * 1024, bytes.size() - count));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      error = n < 0 ? @(strerror(errno))
                    : @"The file changed while it was being read.";
      break;
    }
    count += n;
    indicator.doubleValue = count;
    if (!shown && CFAbsoluteTimeGetCurrent() - started > 0.5) {
      [self.window beginSheet:progress completionHandler:nil];
      shown = YES;
    }
    if (shown) {
      [NSRunLoop.currentRunLoop
          runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
      if (_cancelLoading)
        break;
    }
  }
  close(fd);
  if (shown) {
    [self.window endSheet:progress];
    [progress orderOut:nil];
  }
  _loading = NO;
  if (error) {
    [self warn:@"Cannot Read File" message:error];
    return NO;
  }
  if (_cancelLoading)
    return NO;
  _path = absolute;
  [self recordDiskState];
  [_editor setData:std::move(bytes)];
  _lastMatch = -1;
  _searchStatus.stringValue = @"";
  [self updateOverview];
  [self updateDocument];
  [self updateSelection];
  [self rememberFile];
  [self showWindow:nil];
  [self.window makeFirstResponder:_editor];
  return YES;
}
- (void)recordDiskState
{
  NSDictionary* attrs =
      [NSFileManager.defaultManager attributesOfItemAtPath:_path error:nil];
  _diskModified = attrs[NSFileModificationDate];
  _diskSize = [attrs[NSFileSize] unsignedLongLongValue];
}
- (BOOL)saveFile:(NSString*)path
{
  if (!_path || _loading)
    return NO;
  NSString* target = path.stringByStandardizingPath;
  if (!target.isAbsolutePath)
    target = [[NSFileManager.defaultManager currentDirectoryPath]
        stringByAppendingPathComponent:target];
  if ([target isEqual:_path]) {
    NSDictionary* attrs =
        [NSFileManager.defaultManager attributesOfItemAtPath:_path error:nil];
    if (!attrs || [attrs[NSFileSize] unsignedLongLongValue] != _diskSize ||
        ![attrs[NSFileModificationDate] isEqual:_diskModified]) {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"File Changed on Disk";
      alert.informativeText = @"The original file changed outside DeepHex. "
                              @"Replace it with your edited bytes?";
      [alert addButtonWithTitle:@"Cancel"];
      [alert addButtonWithTitle:@"Replace"];
      if ([alert runModal] != NSAlertSecondButtonReturn)
        return NO;
    }
  }
  const auto& bytes = [_editor data];
  NSData* data = [NSData dataWithBytesNoCopy:const_cast<uint8_t*>(bytes.data())
                                      length:bytes.size()
                                freeWhenDone:NO];
  NSError* error = nil;
  if (![data writeToFile:target options:NSDataWritingAtomic error:&error]) {
    [self warn:@"Cannot Save File" message:error.localizedDescription];
    return NO;
  }
  _path = target;
  [self recordDiskState];
  [_editor markSaved];
  [self updateDocument];
  [self rememberFile];
  return YES;
}
- (void)saveDocument:(id)sender
{
  (void)sender;
  if (_path)
    [self saveFile:_path];
}
- (void)saveDocumentAs:(id)sender
{
  (void)sender;
  if (!_path || _loading)
    return;
  NSSavePanel* panel = NSSavePanel.savePanel;
  panel.nameFieldStringValue = _path.lastPathComponent;
  panel.directoryURL =
      [NSURL fileURLWithPath:_path.stringByDeletingLastPathComponent];
  if ([panel runModal] == NSModalResponseOK)
    [self saveFile:panel.URL.path];
}
- (BOOL)maybeSave
{
  if (_loading)
    return NO;
  if (!_editor.modified)
    return YES;
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Unsaved Changes";
  alert.informativeText = [NSString
      stringWithFormat:@"Save changes to %@?", _path.lastPathComponent];
  [alert addButtonWithTitle:@"Save"];
  [alert addButtonWithTitle:@"Discard"];
  [alert addButtonWithTitle:@"Cancel"];
  NSModalResponse answer = [alert runModal];
  return answer == NSAlertSecondButtonReturn ||
         (answer == NSAlertFirstButtonReturn && [self saveFile:_path]);
}
- (BOOL)windowShouldClose:(NSWindow*)sender
{
  (void)sender;
  return [self maybeSave];
}
- (void)windowWillClose:(NSNotification*)notification
{
  (void)notification;
  [self.window saveFrameUsingName:@"DeepHexWindow"];
  _path = nil;
  _diskModified = nil;
  _diskSize = 0;
  [_editor setData:{}];
  _findBar.hidden = YES;
  _searchInput.stringValue = @"";
  _searchStatus.stringValue = @"";
  _detailLabel.stringValue = @"Drop a file here, or press ⌘O. Changes "
                             @"overwrite bytes without shifting offsets.";
  [self updateOverview];
  [self updateDocument];
  [self updateSelection];
}
- (void)showFind:(id)sender
{
  (void)sender;
  _findBar.hidden = NO;
  [self.window makeFirstResponder:_searchInput];
  [_searchInput selectText:nil];
}
- (void)hideFind:(id)sender
{
  (void)sender;
  _findBar.hidden = YES;
  [self.window makeFirstResponder:_editor];
}
- (void)findNext:(id)sender
{
  (void)sender;
  [self findBackwards:NO];
}
- (void)findPrevious:(id)sender
{
  (void)sender;
  [self findBackwards:YES];
}
- (void)findBackwards:(BOOL)backwards
{
  if (!_path)
    return;
  _findBar.hidden = NO;
  NSString* query = _searchInput.stringValue;
  std::vector<uint8_t> needle;
  if (_searchMode.indexOfSelectedItem == 1) {
    NSData* utf8 = [query dataUsingEncoding:NSUTF8StringEncoding];
    const auto* begin = static_cast<const uint8_t*>(utf8.bytes);
    if (utf8.length)
      needle.assign(begin, begin + utf8.length);
  }
  else {
    int high = -1;
    for (NSUInteger i = 0; i < query.length; ++i) {
      unichar c = [query characterAtIndex:i];
      if ([NSCharacterSet.whitespaceAndNewlineCharacterSet characterIsMember:c])
        continue;
      int digit = c >= '0' && c <= '9'   ? c - '0'
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                         : -1;
      if (digit < 0) {
        _searchStatus.stringValue = @"Use hex pairs (00–FF)";
        return;
      }
      if (high < 0)
        high = digit;
      else {
        needle.push_back((high << 4) | digit);
        high = -1;
      }
    }
    if (high >= 0) {
      _searchStatus.stringValue = @"Incomplete hex pair";
      return;
    }
  }
  if (needle.empty()) {
    _searchStatus.stringValue = @"Enter a pattern";
    return;
  }
  const auto& bytes = [_editor data];
  NSInteger cursor = _editor.selectionStart;
  BOOL repeat = [_lastQuery isEqual:query] &&
                _lastMode == _searchMode.indexOfSelectedItem &&
                cursor == _lastMatch;
  NSInteger found = -1;
  BOOL wrapped = NO;
  auto forward = [&](size_t from) -> NSInteger {
    if (from > bytes.size())
      return -1;
    auto it = std::search(bytes.begin() + from, bytes.end(), needle.begin(),
                          needle.end());
    return it == bytes.end() ? -1 : it - bytes.begin();
  };
  auto reverse = [&](NSInteger from) -> NSInteger {
    if (needle.size() > bytes.size() || from < 0)
      return -1;
    size_t end =
        std::min<size_t>(from, bytes.size() - needle.size()) + needle.size();
    auto it = std::find_end(bytes.begin(), bytes.begin() + end, needle.begin(),
                            needle.end());
    return it == bytes.begin() + end ? -1 : it - bytes.begin();
  };
  if (backwards) {
    found = reverse(cursor - (repeat ? 1 : 0));
    if (found < 0) {
      found = reverse(bytes.size());
      wrapped = found >= 0;
    }
  }
  else {
    found = forward(std::max<NSInteger>(0, cursor + (repeat ? 1 : 0)));
    if (found < 0) {
      found = forward(0);
      wrapped = found >= 0;
    }
  }
  if (found < 0) {
    _searchStatus.stringValue = @"No match";
    return;
  }
  [_editor selectRange:found length:needle.size()];
  _lastMatch = found;
  _lastQuery = query;
  _lastMode = _searchMode.indexOfSelectedItem;
  _searchStatus.stringValue =
      [NSString stringWithFormat:@"%@%@", wrapped ? @"Wrapped · " : @"",
                                 offsetText(found)];
}
- (void)goToOffset:(id)sender
{
  (void)sender;
  if (!_path)
    return;
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Go to Offset";
  alert.informativeText = @"Decimal offset, or hexadecimal prefixed with 0x:";
  NSTextField* input =
      [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 280, 26)];
  input.stringValue =
      offsetText(std::max<NSInteger>(0, _editor.selectionStart));
  alert.accessoryView = input;
  [alert addButtonWithTitle:@"Go"];
  [alert addButtonWithTitle:@"Cancel"];
  alert.window.initialFirstResponder = input;
  if ([alert runModal] != NSAlertFirstButtonReturn)
    return;
  NSString* value = [input.stringValue
      stringByTrimmingCharactersInSet:NSCharacterSet
                                          .whitespaceAndNewlineCharacterSet];
  NSScanner* scanner = [NSScanner scannerWithString:value];
  scanner.charactersToBeSkipped = nil;
  unsigned long long offset = 0;
  BOOL valid = NO;
  if ([value.lowercaseString hasPrefix:@"0x"]) {
    scanner.scanLocation = 2;
    valid = [scanner scanHexLongLong:&offset];
  }
  else if (value.length &&
           [value
               rangeOfCharacterFromSet:NSCharacterSet.decimalDigitCharacterSet
                                           .invertedSet]
                   .location == NSNotFound) {
    errno = 0;
    const char* start = value.UTF8String;
    char* end = nullptr;
    offset = strtoull(start, &end, 10);
    valid = errno != ERANGE && *end == '\0';
    scanner.scanLocation = value.length;
  }
  if (!valid || !scanner.isAtEnd || offset >= [_editor data].size()) {
    [self warn:@"Invalid Offset" message:@"Enter an offset within the file."];
    return;
  }
  [_editor moveToOffset:offset select:YES];
  [self.window makeFirstResponder:_editor];
}
- (void)updateDocument
{
  BOOL dirty = _editor.modified;
  self.window.documentEdited = dirty;
  self.window.representedURL = _path ? [NSURL fileURLWithPath:_path] : nil;
  self.window.title =
      _path ? [_path.lastPathComponent stringByAppendingString:@" — DeepHex"]
            : @"DeepHex";
  _fileLabel.stringValue =
      _path ? _path.lastPathComponent : @"Open a file to explore";
  _fileLabel.toolTip = _path;
  if (_path) {
    const auto& bytes = [_editor data];
    NSString* kind = MachoMap::isSupported(bytes) ? @"Mach-O executable"
                     : ElfMap::isSupported(bytes) ? @"ELF executable"
                                                  : @"Binary file";
    _detailLabel.stringValue = [NSString
        stringWithFormat:
            @"%@ · %@ bytes · %@", kind,
            [NSNumberFormatter
                localizedStringFromNumber:@(bytes.size())
                              numberStyle:NSNumberFormatterDecimalStyle],
            dirty ? @"Unsaved changes" : @"Saved"];
  }
  [self.window.toolbar validateVisibleItems];
}
- (void)byteOrderChanged:(id)sender
{
  (void)sender;
  [self updateSelection];
}
- (void)updateSelection
{
  NSInteger start = _editor.selectionStart, length = _editor.selectionLength;
  if (start < 0 || !length) {
    _status.stringValue = _path ? @"Empty file · 0 bytes" : @"Ready";
    _inspector.string = @"Select a byte to inspect its values.\n\nHex: type "
                        @"0–9 / A–F\nASCII: click the text column\nTab: switch "
                        @"editing pane\nShift + arrows: extend selection";
    return;
  }
  _status.stringValue = [NSString
      stringWithFormat:@"Offset %@ · %ld byte%@ selected", offsetText(start),
                       (long)length, length == 1 ? @"" : @"s"];
  const auto& bytes = [_editor data];
  size_t available = bytes.size() - start;
  BOOL little = _byteOrder.indexOfSelectedItem == 0;
  auto readValue = [&](int width) {
    uint64_t value = 0;
    for (int i = 0; i < width; ++i)
      value |= uint64_t(bytes[start + i]) << ((little ? i : width - i - 1) * 8);
    return value;
  };
  NSMutableString* values = [NSMutableString
      stringWithFormat:@"Offset   %@\nBinary   ", offsetText(start)];
  for (int bit = 7; bit >= 0; --bit)
    [values appendFormat:@"%d", (bytes[start] >> bit) & 1];
  [values appendString:@"\n"];
  for (int width : {1, 2, 4, 8}) {
    if (available < size_t(width)) {
      [values
          appendFormat:@"UInt%-2d   —\nInt%-2d    —\n", width * 8, width * 8];
      continue;
    }
    uint64_t value = readValue(width);
    int64_t signedValue;
    if (width == 8)
      std::memcpy(&signedValue, &value, sizeof(value));
    else
      signedValue = value & (uint64_t(1) << (width * 8 - 1))
                        ? int64_t(value) - (int64_t(1) << (width * 8))
                        : int64_t(value);
    [values appendFormat:@"UInt%-2d   %llu\nInt%-2d    %lld\n", width * 8,
                         (unsigned long long)value, width * 8,
                         (long long)signedValue];
  }
  if (available >= 4) {
    uint32_t bits = readValue(4);
    float value;
    std::memcpy(&value, &bits, 4);
    [values appendFormat:@"Float32  %.9g\n", value];
  }
  else
    [values appendString:@"Float32  —\n"];
  if (available >= 8) {
    uint64_t bits = readValue(8);
    double value;
    std::memcpy(&value, &bits, 8);
    [values appendFormat:@"Float64  %.17g", value];
  }
  else
    [values appendString:@"Float64  —"];
  _inspector.string = values;
}
- (void)memoryViewSelectionChanged:(DHMemoryView*)view
{
  (void)view;
  [self updateSelection];
}
- (void)memoryViewModifiedChanged:(DHMemoryView*)view
{
  (void)view;
  [self updateDocument];
}
- (void)memoryViewViewportChanged:(DHMemoryView*)view
{
  (void)view;
  [_overview updateViewport];
}
- (void)memoryViewDataChanged:(DHMemoryView*)view
{
  (void)view;
  [self updateSelection];
  [self updateDocument];
  _lastMatch = -1;
  _searchStatus.stringValue = @"";
  [_overviewTimer invalidate];
  __weak DHMainWindowController* weakSelf = self;
  _overviewTimer =
      [NSTimer scheduledTimerWithTimeInterval:0.2
                                      repeats:NO
                                        block:^(NSTimer* timer) {
                                          (void)timer;
                                          [weakSelf updateOverview];
                                        }];
}
- (void)updateOverview
{
  if (!_editor)
    return;
  const auto& bytes = [_editor data];
  std::unique_ptr<StandardMap> map;
  if (MachoMap::isSupported(bytes))
    map = std::make_unique<MachoMap>();
  else if (ElfMap::isSupported(bytes))
    map = std::make_unique<ElfMap>();
  else
    map = std::make_unique<StandardMap>();
  BOOL dark =
      [[self.window.effectiveAppearance bestMatchFromAppearancesWithNames:@[
        NSAppearanceNameAqua, NSAppearanceNameDarkAqua
      ]] isEqual:NSAppearanceNameDarkAqua];
  map->calculateRegions(
      bytes, _regions,
      dark ? MapColor(0.85, 0.85, 0.85) : MapColor(0.15, 0.15, 0.15));
  [_overview setRegions:_regions totalSize:bytes.size()];
  [_regionTable reloadData];
  NSMutableAttributedString* legend = [[NSMutableAttributedString alloc] init];
  std::set<std::string> seen;
  for (const auto& region : _regions) {
    if (!seen.insert(region.legend_label).second)
      continue;
    NSColor* color = [NSColor colorWithSRGBRed:region.color.red
                                         green:region.color.green
                                          blue:region.color.blue
                                         alpha:1];
    [legend
        appendAttributedString:[[NSAttributedString alloc]
                                   initWithString:@"■ "
                                       attributes:@{
                                         NSForegroundColorAttributeName : color
                                       }]];
    [legend appendAttributedString:
                [[NSAttributedString alloc]
                    initWithString:[text(region.legend_label)
                                       stringByAppendingString:@"    "]
                        attributes:@{
                          NSForegroundColorAttributeName : dark
                              ? NSColor.lightGrayColor
                              : NSColor.secondaryLabelColor
                        }]];
  }
  if (!_regions.empty())
    [legend
        appendAttributedString:[[NSAttributedString alloc]
                                   initWithString:@"▣ Visible bytes"
                                       attributes:@{
                                         NSForegroundColorAttributeName : dark
                                             ? NSColor.lightGrayColor
                                             : NSColor.secondaryLabelColor
                                       }]];
  _legend.attributedStringValue = legend;
  _legend.hidden = _regions.empty();
}
- (NSInteger)numberOfRowsInTableView:(NSTableView*)table
{
  (void)table;
  return _regions.size();
}
- (NSView*)tableView:(NSTableView*)table
    viewForTableColumn:(NSTableColumn*)column
                   row:(NSInteger)row
{
  NSTextField* field = [table makeViewWithIdentifier:column.identifier
                                               owner:self];
  if (!field) {
    field = [NSTextField labelWithString:@""];
    field.identifier = column.identifier;
    field.font = [NSFont systemFontOfSize:11];
  }
  const auto& region = _regions[row];
  field.stringValue = [column.identifier isEqual:@"Region"]
                          ? text(region.label)
                          : offsetText(region.start);
  field.toolTip = [NSString
      stringWithFormat:@"%llu bytes · %@–%@",
                       (unsigned long long)(region.end - region.start),
                       offsetText(region.start), offsetText(region.end - 1)];
  return field;
}
- (void)navigateRegion:(id)sender
{
  (void)sender;
  NSInteger row = _regionTable.clickedRow;
  if (row >= 0 && size_t(row) < _regions.size()) {
    [_editor moveToOffset:_regions[row].start select:YES];
    [self.window makeFirstResponder:_editor];
  }
}
- (void)toggleInspector:(id)sender
{
  (void)sender;
  _sidebar.hidden = !_sidebar.hidden;
  [_split adjustSubviews];
  _inspectorItem.state =
      _sidebar.hidden ? NSControlStateValueOff : NSControlStateValueOn;
  [NSUserDefaults.standardUserDefaults setBool:!_sidebar.hidden
                                        forKey:@"showInspector"];
}
- (CGFloat)splitView:(NSSplitView*)split
    constrainMinCoordinate:(CGFloat)coordinate
               ofSubviewAt:(NSInteger)index
{
  (void)split;
  (void)coordinate;
  (void)index;
  return 360;
}
- (CGFloat)splitView:(NSSplitView*)split
    constrainMaxCoordinate:(CGFloat)coordinate
               ofSubviewAt:(NSInteger)index
{
  (void)coordinate;
  (void)index;
  return NSWidth(split.bounds) - 240;
}
- (void)splitView:(NSSplitView*)split resizeSubviewsWithOldSize:(NSSize)oldSize
{
  (void)oldSize;
  CGFloat width = NSWidth(split.bounds), height = NSHeight(split.bounds);
  CGFloat sidebarWidth = _sidebar.hidden
                             ? 0
                             : std::clamp(NSWidth(_sidebar.frame), CGFloat(240),
                                          std::max(CGFloat(240), width - 360));
  CGFloat editorWidth =
      width - sidebarWidth - (_sidebar.hidden ? 0 : split.dividerThickness);
  _editorScroll.frame = NSMakeRect(0, 0, editorWidth, height);
  _sidebar.frame =
      NSMakeRect(editorWidth + split.dividerThickness, 0, sidebarWidth, height);
}
- (void)selectAppearance:(id)sender
{
  _appearance = std::clamp<NSInteger>([sender tag], 0, 2);
  [NSUserDefaults.standardUserDefaults setInteger:_appearance
                                           forKey:@"appearance"];
  [self applyAppearance];
}
- (void)toggleTransparency:(id)sender
{
  (void)sender;
  _transparencyPreferred = !_transparencyPreferred;
  [NSUserDefaults.standardUserDefaults setBool:_transparencyPreferred
                                        forKey:@"terminalTransparency"];
  [self applyAppearance];
}
- (BOOL)usesTerminalAppearance
{
  return _appearance == 2 ||
         (_appearance == 0 &&
          [[self.window.effectiveAppearance bestMatchFromAppearancesWithNames:@[
            NSAppearanceNameAqua, NSAppearanceNameDarkAqua
          ]] isEqual:NSAppearanceNameDarkAqua]);
}
- (void)accessibilityChanged:(NSNotification*)notification
{
  (void)notification;
  [self applyAppearance];
}
- (void)applyAppearance
{
  self.window.appearance =
      _appearance == 0
          ? nil
          : [NSAppearance appearanceNamed:_appearance == 1
                                              ? NSAppearanceNameAqua
                                              : NSAppearanceNameDarkAqua];
  [self refreshAppearance];
}
- (void)refreshAppearance
{
  if (!_editor)
    return;
  BOOL terminal = [self usesTerminalAppearance];
  BOOL translucent =
      terminal && _transparencyPreferred &&
      !NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceTransparency;
  _root.terminal = terminal;
  _root.translucent = translucent;
  _root.state =
      translucent ? NSVisualEffectStateActive : NSVisualEffectStateInactive;
  self.window.opaque = !translucent;
  self.window.backgroundColor =
      translucent ? NSColor.clearColor : NSColor.windowBackgroundColor;
  _editor.terminalTheme = terminal;
  _editor.translucent = translucent;
  _editorScroll.drawsBackground = !translucent;
  _editorScroll.backgroundColor = terminal ? [NSColor colorWithSRGBRed:0.082
                                                                 green:0.086
                                                                  blue:0.11
                                                                 alpha:1]
                                           : NSColor.textBackgroundColor;
  _regionScroll.drawsBackground = !translucent;
  _regionTable.backgroundColor =
      translucent ? NSColor.clearColor : NSColor.controlBackgroundColor;
  _regionTable.usesAlternatingRowBackgroundColors = !translucent;
  _inspector.textColor = terminal ? [NSColor colorWithSRGBRed:0.90
                                                        green:0.74
                                                         blue:0.41
                                                        alpha:1]
                                  : NSColor.textColor;
  _fileLabel.textColor = terminal ? [NSColor colorWithSRGBRed:0.13
                                                        green:0.72
                                                         blue:0.87
                                                        alpha:1]
                                  : NSColor.labelColor;
  _detailLabel.textColor = NSColor.secondaryLabelColor;
  for (NSMenuItem* item in _appearanceMenu.itemArray)
    if (item.action == @selector(selectAppearance:))
      item.state = item.tag == _appearance ? NSControlStateValueOn
                                           : NSControlStateValueOff;
  _transparencyItem.state =
      _transparencyPreferred ? NSControlStateValueOn : NSControlStateValueOff;
  _inspectorItem.state =
      _sidebar.hidden ? NSControlStateValueOff : NSControlStateValueOn;
  _root.needsDisplay = YES;
  [self updateOverview];
}
- (void)rememberFile
{
  NSMutableArray<NSString*>* recent =
      [[NSUserDefaults.standardUserDefaults stringArrayForKey:@"recentFiles"]
          mutableCopy]
          ?: [NSMutableArray array];
  [recent removeObject:_path];
  [recent insertObject:_path atIndex:0];
  while (recent.count > 8)
    [recent removeLastObject];
  [NSUserDefaults.standardUserDefaults setObject:recent forKey:@"recentFiles"];
  [NSDocumentController.sharedDocumentController
      noteNewRecentDocumentURL:[NSURL fileURLWithPath:_path]];
  [self updateRecentFiles];
}
- (void)updateRecentFiles
{
  [_recentMenu removeAllItems];
  for (NSString* path in
       [NSUserDefaults.standardUserDefaults stringArrayForKey:@"recentFiles"]) {
    NSMenuItem* item = menuItem(_recentMenu, path.lastPathComponent,
                                @selector(openRecent:), @"", self);
    item.representedObject = path;
    item.toolTip = path;
  }
  if (_recentMenu.numberOfItems)
    [_recentMenu addItem:NSMenuItem.separatorItem];
  menuItem(_recentMenu, @"Clear Menu", @selector(clearRecent:), @"", self);
}
- (void)openRecent:(NSMenuItem*)sender
{
  [self openFile:sender.representedObject];
}
- (void)clearRecent:(id)sender
{
  (void)sender;
  [NSUserDefaults.standardUserDefaults removeObjectForKey:@"recentFiles"];
  [NSDocumentController.sharedDocumentController clearRecentDocuments:nil];
  [self updateRecentFiles];
}
@end
