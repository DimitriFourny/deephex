#pragma once

#import <AppKit/AppKit.h>
#include <cstdint>
#include <vector>
#include "standard_map.h"

@class DHMemoryView;

@interface DHHeatMap : NSView <NSViewToolTipOwner>
@property(nonatomic, weak) DHMemoryView* memoryView;
- (void)setRegions:(const std::vector<MapRegion>&)regions
         totalSize:(uint64_t)size;
- (void)updateViewport;
@end
