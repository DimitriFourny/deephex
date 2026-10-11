#pragma once
#import <AppKit/AppKit.h>
#import "memoryview.h"

@interface DHMainWindowController : NSWindowController <NSWindowDelegate,
                                                        DHMemoryViewDelegate,
                                                        NSTableViewDataSource,
                                                        NSTableViewDelegate,
                                                        NSToolbarDelegate,
                                                        NSToolbarItemValidation,
                                                        NSSplitViewDelegate,
                                                        NSMenuItemValidation>
@property(nonatomic, readonly) DHMemoryView* editor;
- (BOOL)openFile:(NSString*)path;
- (BOOL)saveFile:(NSString*)path;
- (BOOL)maybeSave;
- (void)installMenus;
- (void)openDocument:(id)sender;
- (void)saveDocument:(id)sender;
- (void)saveDocumentAs:(id)sender;
- (void)showFind:(id)sender;
- (void)findNext:(id)sender;
- (void)findPrevious:(id)sender;
- (void)goToOffset:(id)sender;
- (void)selectAppearance:(id)sender;
@end
