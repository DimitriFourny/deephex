#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct MapColor {
  double red = 1;
  double green = 1;
  double blue = 1;

  MapColor() = default;
  MapColor(double r, double g, double b) : red(r), green(g), blue(b) {}
  static MapColor fromRgb(unsigned r, unsigned g, unsigned b);
  static MapColor fromHsv(unsigned hue, unsigned saturation, unsigned value);
  double lightness() const;
  MapColor contrasting() const;
};

struct MapRegion {
  uint64_t start;
  uint64_t end;  // Exclusive file offset.
  MapColor color;
  std::string label;
  std::string
      legend_label;  // Short legend text; label retains range-specific details.
};

class StandardMap {
 public:
  virtual ~StandardMap() = default;
  virtual void calculateRegions(const std::vector<uint8_t>& data,
                                std::vector<MapRegion>& regions,
                                MapColor font_color);
  static bool isSupported(const std::vector<uint8_t>& data);

 protected:
  static bool readUnsigned(const std::vector<uint8_t>& data,
                           uint64_t offset,
                           unsigned width,
                           bool little_endian,
                           uint64_t& value);
  static void completeRegions(std::vector<MapRegion>& regions,
                              uint64_t size,
                              MapColor font_color);
};
