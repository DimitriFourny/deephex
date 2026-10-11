#include "elf_map.h"

bool ElfMap::isSupported(const QVector<quint8>& data)
{
  return data.size() >= 4 && data[0] == 0x7f && data[1] == 'E' &&
         data[2] == 'L' && data[3] == 'F';
}

void ElfMap::calculateRegions(const QVector<quint8>& data,
                              QVector<MapRegion>& regions,
                              QColor font_color)
{
  regions.clear();
  const quint64 size = static_cast<quint64>(data.size());
  const auto parse = [&]() {
    if (!isSupported(data) || size < 16 ||
        (data[4] != 1 && data[4] != 2) ||
        (data[5] != 1 && data[5] != 2) || data[6] != 1)
      return false;
    const bool is64 = data[4] == 2;
    const bool little = data[5] == 1;
    const quint64 header_size = is64 ? 64 : 52;
    if (size < header_size)
      return false;
    const auto value = [&](quint64 offset, unsigned width) {
      quint64 result = 0;
      readUnsigned(data, offset, width, little, result);
      return result;
    };
    if (value(20, 4) != 1)
      return false;
    const quint64 ehsize = value(is64 ? 52 : 40, 2);
    const quint64 phoff = value(is64 ? 32 : 28, is64 ? 8 : 4);
    const quint64 entry_size = value(is64 ? 54 : 42, 2);
    quint64 count = value(is64 ? 56 : 44, 2);
    if (ehsize < header_size || ehsize > size)
      return false;
    if (count == 0xffff) {
      const quint64 shoff = value(is64 ? 40 : 32, is64 ? 8 : 4);
      const quint64 shsize = value(is64 ? 58 : 46, 2);
      const quint64 minimum = is64 ? 64 : 40;
      if (shsize < minimum || shoff > size || shsize > size - shoff)
        return false;
      count = value(shoff + (is64 ? 44 : 28), 4);
    }
    if (count != 0 && (entry_size < (is64 ? 56u : 32u) ||
                       phoff < ehsize || phoff > size ||
                       count > (size - phoff) / entry_size))
      return false;
    regions.append({0, ehsize, QColor(143, 116, 187),
                    QStringLiteral("ELF%1 header (%2 endian)")
                        .arg(is64 ? 64 : 32)
                        .arg(little ? QStringLiteral("little")
                                    : QStringLiteral("big")), QStringLiteral("Header")});
    if (count != 0)
      regions.append({phoff, phoff + count * entry_size, QColor(157, 133, 191),
                      QStringLiteral("ELF program headers"), QStringLiteral("Program headers")});
    for (quint64 i = 0; i < count; ++i) {
      const quint64 base = phoff + i * entry_size;
      const quint64 type = value(base, 4);
      const quint64 offset = value(base + (is64 ? 8 : 4), is64 ? 8 : 4);
      const quint64 length = value(base + (is64 ? 32 : 16), is64 ? 8 : 4);
      const quint64 flags = value(base + (is64 ? 4 : 24), 4);
      if (length == 0)
        continue;
      if (offset > size || length > size - offset)
        return false;
      QString name;
      switch (type) {
        case 0: name = QStringLiteral("NULL"); break;
        case 1: name = QStringLiteral("LOAD"); break;
        case 2: name = QStringLiteral("DYNAMIC"); break;
        case 3: name = QStringLiteral("INTERP"); break;
        case 4: name = QStringLiteral("NOTE"); break;
        case 5: name = QStringLiteral("SHLIB"); break;
        case 6: name = QStringLiteral("PHDR"); break;
        case 7: name = QStringLiteral("TLS"); break;
        case 0x6474e550: name = QStringLiteral("GNU_EH_FRAME"); break;
        case 0x6474e551: name = QStringLiteral("GNU_STACK"); break;
        case 0x6474e552: name = QStringLiteral("GNU_RELRO"); break;
        default: name = QStringLiteral("Type 0x%1").arg(type, 0, 16); break;
      }
      const QString permissions = QStringLiteral("%1%2%3")
                                      .arg(flags & 4 ? "R" : "-")
                                      .arg(flags & 2 ? "W" : "-")
                                      .arg(flags & 1 ? "X" : "-");
      const QString label = QStringLiteral("ELF %1 #%2 (%3)")
                                .arg(name).arg(i).arg(permissions);
      regions.append({offset, offset + length,
                      QColor::fromHsv(static_cast<int>((i * 47 + 195) % 360),
                                      125, 190),
                      label, label});
    }
    return true;
  };
  if (!parse()) {
    StandardMap::calculateRegions(data, regions, font_color);
    return;
  }
  completeRegions(regions, size, font_color);
}
