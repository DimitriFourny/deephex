#pragma once
#include "standard_map.h"

class MachoMap : public StandardMap {
 public:
  virtual void calculateRegions(const QVector<quint8>& data,
                                QVector<MapRegion>& regions,
                                QColor font_color) override;

  static bool isSupported(const QVector<quint8>& data);
};