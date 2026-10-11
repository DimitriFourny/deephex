#pragma once

#import <AppKit/AppKit.h>
#include <cstdint>
#include <vector>

@class DHMemoryView;

@protocol DHMemoryViewDelegate <NSObject>
- (void)memoryViewSelectionChanged:(DHMemoryView*)view;
- (void)memoryViewDataChanged:(DHMemoryView*)view;
- (void)memoryViewViewportChanged:(DHMemoryView*)view;
- (void)memoryViewModifiedChanged:(DHMemoryView*)view;
@end

@interface DHMemoryView : NSView <NSMenuItemValidation>
@property(nonatomic, weak) id<DHMemoryViewDelegate> delegate;
@property(nonatomic) BOOL terminalTheme;
@property(nonatomic) BOOL translucent;
@property(nonatomic, readonly) NSInteger selectionStart;
@property(nonatomic, readonly) NSInteger selectionLength;
@property(nonatomic, readonly, getter=isModified) BOOL modified;
@property(nonatomic, readonly) uint64_t visibleByteStart;
@property(nonatomic, readonly) uint64_t visibleByteEnd;
- (void)setData:(std::vector<uint8_t>)data;
- (const std::vector<uint8_t>&)data;
- (void)selectRange:(NSInteger)start length:(NSInteger)length;
- (void)moveToOffset:(uint64_t)offset select:(BOOL)select;
- (void)markSaved;
- (IBAction)copy:(id)sender;
- (IBAction)paste:(id)sender;
- (IBAction)selectAll:(id)sender;
- (IBAction)zeroSelection:(id)sender;
- (IBAction)undo:(id)sender;
- (IBAction)redo:(id)sender;
@end
