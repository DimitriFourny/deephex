#pragma once

#include <QColor>
#include <QVector>

struct MapRegion {
  uint64_t start;
  uint64_t end;
  QColor color;
};

class StandardMap {
 public:
  StandardMap() = default;
  virtual ~StandardMap() = default;
  virtual void calculateRegions(const QVector<quint8>& data,
                                QVector<MapRegion>& regions,
                                QColor font_color);

  static bool isSupported(const QVector<quint8>& data);
};