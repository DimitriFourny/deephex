#include "standard_map.h"

bool StandardMap::isSupported(const QVector<quint8>& data)
{
  return true;
}

void StandardMap::calculateRegions(const QVector<quint8>& data,
                                   QVector<MapRegion>& regions,
                                   QColor font_color)
{
  regions.clear();
  int nb_regions = 100;
  uint64_t bytes_per_region = data.size() / nb_regions;

  QColor ascii_color = QColor(0xeb9b9b);
  QColor zero_color = font_color.darker(200);

  for (int i = 0; i < nb_regions; i++) {
    // Calculate the start and end indices for each region
    uint64_t start = i * bytes_per_region;
    uint64_t end = start + bytes_per_region;
    if (end > data.size()) {
      end = data.size();
    }

    // For each region, calculate the number of zeroes and ASCII characters
    int nb_zero = 0;
    int nb_ascii = 0;
    for (int j = start; j < end; ++j) {
      char ch = static_cast<char>(data[j]);
      if (ch == 0) {
        nb_zero++;
      }
      else if (ch >= 32 && ch <= 126) {
        nb_ascii++;
      }
    }

    // Calculate the color
    bytes_per_region = end - start;
    QColor color = font_color;

    if (nb_zero >= bytes_per_region / 2) {
      color = zero_color;
    }
    else if (nb_ascii >= bytes_per_region / 2) {
      color = ascii_color;
    }

    regions.push_back({start, end, color});
  }
}