#pragma once
#include "standard_map.h"

class MachoMap : public StandardMap {
 public:
  void calculateRegions(const std::vector<uint8_t>& data,
                        std::vector<MapRegion>& regions,
                        MapColor font_color) override;

  static bool isSupported(const std::vector<uint8_t>& data);
};