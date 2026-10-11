#pragma once

#include <QColor>
#include <QString>
#include <QVector>

struct MapRegion {
  quint64 start;
  quint64 end;  // Exclusive file offset.
  QColor color;
  QString label;
  QString legend_label;  // Short legend text; label retains range-specific details.
};

class StandardMap {
 public:
  StandardMap() = default;
  virtual ~StandardMap() = default;
  virtual void calculateRegions(const QVector<quint8>& data,
                                QVector<MapRegion>& regions,
                                QColor font_color);

  static bool isSupported(const QVector<quint8>& data);

 protected:
  static bool readUnsigned(const QVector<quint8>& data, quint64 offset,
                           unsigned width, bool little_endian, quint64& value);
  static void completeRegions(QVector<MapRegion>& regions, quint64 size,
                              QColor font_color);
};
