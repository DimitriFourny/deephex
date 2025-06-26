#include "elf_map.h"

bool ElfMap::isSupported(const QVector<quint8>& data)
{
  // Check if the data starts with the ELF magic number
  return data.size() >= 4 && data[0] == 0x7f && data[1] == 'E' &&
         data[2] == 'L' && data[3] == 'F';
}

void ElfMap::calculateRegions(const QVector<quint8>& data,
                              QVector<MapRegion>& regions,
                              QColor font_color)
{
  // TODO
  StandardMap::calculateRegions(data, regions, font_color);
}