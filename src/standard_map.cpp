#include "standard_map.h"

#include <algorithm>
#include <cmath>
#include <utility>

MapColor MapColor::fromRgb(unsigned r, unsigned g, unsigned b)
{
  return MapColor(r / 255.0, g / 255.0, b / 255.0);
}

MapColor MapColor::fromHsv(unsigned hue, unsigned saturation, unsigned value)
{
  const double h = (hue % 360) / 60.0;
  const double v = value / 255.0;
  const double c = v * saturation / 255.0;
  const double x = c * (1 - std::abs(std::fmod(h, 2.0) - 1));
  const double m = v - c;
  switch (static_cast<unsigned>(h)) {
    case 0: return MapColor(c + m, x + m, m);
    case 1: return MapColor(x + m, c + m, m);
    case 2: return MapColor(m, c + m, x + m);
    case 3: return MapColor(m, x + m, c + m);
    case 4: return MapColor(x + m, m, c + m);
    default: return MapColor(c + m, m, x + m);
  }
}

double MapColor::lightness() const
{
  return (std::min({red, green, blue}) + std::max({red, green, blue})) / 2;
}

MapColor MapColor::contrasting() const
{
  // The old palette uses HSV value scaling at 180 percent for unmapped gaps.
  const double value = std::max({red, green, blue});
  if (lightness() >= 128.0 / 255.0)
    return MapColor(red / 1.8, green / 1.8, blue / 1.8);
  const double increased = value * 1.8;
  if (increased <= 1)
    return MapColor(red * 1.8, green * 1.8, blue * 1.8);
  const double minimum = std::min({red, green, blue});
  const double saturation = value == 0 ? 0 : (value - minimum) / value;
  const double adjusted = std::max(0.0, saturation - (increased - 1));
  const auto channel = [&](double component) {
    return saturation == 0 ? 1.0 : 1 - (1 - component / value) * adjusted / saturation;
  };
  return MapColor(channel(red), channel(green), channel(blue));
}

bool StandardMap::isSupported(const std::vector<uint8_t>&)
{
  return true;
}

bool StandardMap::readUnsigned(const std::vector<uint8_t>& data, uint64_t offset,
                               unsigned width, bool little_endian,
                               uint64_t& value)
{
  const uint64_t size = data.size();
  if (width == 0 || width > 8 || offset > size || width > size - offset)
    return false;
  value = 0;
  for (unsigned i = 0; i < width; ++i) {
    const unsigned shift = (little_endian ? i : width - 1 - i) * 8;
    value |= uint64_t(data[static_cast<size_t>(offset + i)]) << shift;
  }
  return true;
}

void StandardMap::completeRegions(std::vector<MapRegion>& regions, uint64_t size,
                                  MapColor font_color)
{
  std::sort(regions.begin(), regions.end(),
            [](const MapRegion& a, const MapRegion& b) {
    return a.start != b.start ? a.start < b.start : a.end < b.end;
  });
  std::vector<MapRegion> complete;
  complete.reserve(regions.size() + 1);
  uint64_t cursor = 0;
  const MapColor gap_color = font_color.contrasting();
  for (MapRegion& region : regions) {
    const uint64_t start = std::max(cursor, std::min(region.start, size));
    const uint64_t end = std::min(region.end, size);
    if (end <= start)
      continue;
    if (start > cursor)
      complete.push_back({cursor, start, gap_color, "Unmapped", "Unmapped"});
    complete.push_back({start, end, region.color, std::move(region.label),
                        std::move(region.legend_label)});
    cursor = end;
  }
  if (cursor < size)
    complete.push_back({cursor, size, gap_color, "Unmapped", "Unmapped"});
  regions.swap(complete);
}

void StandardMap::calculateRegions(const std::vector<uint8_t>& data,
                                   std::vector<MapRegion>& regions,
                                   MapColor font_color)
{
  regions.clear();
  const uint64_t size = data.size();
  if (size == 0)
    return;
  // Aim for 4 KiB bins, balancing large files into at most 256 bins.
  const uint64_t count = std::min<uint64_t>(256, (size - 1) / 4096 + 1);
  regions.reserve(static_cast<size_t>(count));
  for (uint64_t i = 0; i < count; ++i) {
    const uint64_t start = i * size / count;
    const uint64_t end = (i + 1) * size / count;
    uint64_t zeros = 0;
    uint64_t ascii = 0;
    for (uint64_t j = start; j < end; ++j) {
      const uint8_t byte = data[static_cast<size_t>(j)];
      zeros += byte == 0;
      ascii += (byte >= 32 && byte <= 126) || byte == '\n' ||
               byte == '\r' || byte == '\t';
    }
    const uint64_t length = end - start;
    std::string kind = "Binary";
    MapColor color = MapColor::fromRgb(92, 139, 196);
    if (zeros >= (length + 1) / 2) {
      kind = "Zero-filled";
      color = font_color.lightness() < 128.0 / 255.0
                  ? MapColor::fromRgb(164, 170, 177)
                  : MapColor::fromRgb(83, 90, 100);
    }
    else if (ascii >= (length + 1) / 2) {
      kind = "ASCII text";
      color = MapColor::fromRgb(90, 171, 129);
    }
    std::string label = kind + " (" + std::to_string(zeros * 100 / length) +
                        "% zero, " + std::to_string(ascii * 100 / length) + "% ASCII)";
    regions.push_back({start, end, color, std::move(label), std::move(kind)});
  }
}
