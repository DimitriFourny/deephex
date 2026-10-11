#include "standard_map.h"

#include <algorithm>

bool StandardMap::isSupported(const QVector<quint8>& data)
{
  Q_UNUSED(data);
  return true;
}

bool StandardMap::readUnsigned(const QVector<quint8>& data, quint64 offset,
                               unsigned width, bool little_endian,
                               quint64& value)
{
  const quint64 size = static_cast<quint64>(data.size());
  if (width == 0 || width > 8 || offset > size || width > size - offset)
    return false;
  value = 0;
  for (unsigned i = 0; i < width; ++i) {
    const unsigned shift = (little_endian ? i : width - 1 - i) * 8;
    value |= quint64(data[static_cast<qsizetype>(offset + i)]) << shift;
  }
  return true;
}

void StandardMap::completeRegions(QVector<MapRegion>& regions, quint64 size,
                                  QColor font_color)
{
  std::sort(regions.begin(), regions.end(),
             [](const MapRegion& a, const MapRegion& b) {
    return a.start != b.start ? a.start < b.start : a.end < b.end;
  });
  QVector<MapRegion> complete;
  complete.reserve(regions.size() + 1);
  quint64 cursor = 0;
  const QColor gap_color = font_color.lightness() < 128
                               ? font_color.lighter(180)
                               : font_color.darker(180);
  for (const MapRegion& region : regions) {
    const quint64 start = std::max(cursor, std::min(region.start, size));
    const quint64 end = std::min(region.end, size);
    if (end <= start)
      continue;
    if (start > cursor)
      complete.append({cursor, start, gap_color, QStringLiteral("Unmapped"), QStringLiteral("Unmapped")});
    complete.append({start, end, region.color, region.label, region.legend_label});
    cursor = end;
  }
  if (cursor < size)
    complete.append({cursor, size, gap_color, QStringLiteral("Unmapped"), QStringLiteral("Unmapped")});
  regions.swap(complete);
}

void StandardMap::calculateRegions(const QVector<quint8>& data,
                                   QVector<MapRegion>& regions,
                                   QColor font_color)
{
  regions.clear();
  const quint64 size = static_cast<quint64>(data.size());
  if (size == 0)
    return;
  // Aim for 4 KiB bins, balancing large files into at most 256 bins.
  const quint64 count = std::min<quint64>(256, (size - 1) / 4096 + 1);
  regions.reserve(static_cast<qsizetype>(count));
  for (quint64 i = 0; i < count; ++i) {
    const quint64 start = i * size / count;
    const quint64 end = (i + 1) * size / count;
    quint64 zeros = 0;
    quint64 ascii = 0;
    for (quint64 j = start; j < end; ++j) {
      const quint8 byte = data[static_cast<qsizetype>(j)];
      zeros += byte == 0;
      ascii += (byte >= 32 && byte <= 126) || byte == '\n' ||
               byte == '\r' || byte == '\t';
    }
    const quint64 length = end - start;
    QString kind = QStringLiteral("Binary");
    QColor color(92, 139, 196);
    if (zeros >= (length + 1) / 2) {
      kind = QStringLiteral("Zero-filled");
      color = font_color.lightness() < 128 ? QColor(164, 170, 177)
                                          : QColor(83, 90, 100);
    }
    else if (ascii >= (length + 1) / 2) {
      kind = QStringLiteral("ASCII text");
      color = QColor(90, 171, 129);
    }
    const QString label = QStringLiteral("%1 (%2% zero, %3% ASCII)")
                              .arg(kind)
                              .arg(zeros * 100 / length)
                              .arg(ascii * 100 / length);
    regions.append({start, end, color, label, kind});
  }
}
