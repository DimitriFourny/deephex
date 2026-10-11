#include "macho_map.h"

#include <algorithm>

namespace {
quint32 magicAt(const QVector<quint8>& data, quint64 offset)
{
  const quint64 size = static_cast<quint64>(data.size());
  if (offset > size || 4 > size - offset)
    return 0;
  const quint8* bytes = data.constData() + static_cast<qsizetype>(offset);
  return (quint32(bytes[0]) << 24) | (quint32(bytes[1]) << 16) |
         (quint32(bytes[2]) << 8) | bytes[3];
}

QString cpuName(quint64 cpu)
{
  switch (cpu) {
    case 7: return QStringLiteral("i386");
    case 0x01000007: return QStringLiteral("x86_64");
    case 12: return QStringLiteral("arm");
    case 0x0100000c: return QStringLiteral("arm64");
    case 0x0200000c: return QStringLiteral("arm64_32");
    case 18: return QStringLiteral("ppc");
    case 0x01000012: return QStringLiteral("ppc64");
    default: return QStringLiteral("CPU 0x%1").arg(cpu, 0, 16);
  }
}
}  // namespace

bool MachoMap::isSupported(const QVector<quint8>& data)
{
  switch (magicAt(data, 0)) {
    case 0xfeedface: case 0xcefaedfe:
    case 0xfeedfacf: case 0xcffaedfe:
    case 0xcafebabe: case 0xbebafeca:
    case 0xcafebabf: case 0xbfbafeca:
      return true;
    default:
      return false;
  }
}

void MachoMap::calculateRegions(const QVector<quint8>& data,
                                QVector<MapRegion>& regions,
                                QColor font_color)
{
  regions.clear();
  const quint64 size = static_cast<quint64>(data.size());
  const auto value = [&](quint64 offset, unsigned width, bool little) {
    quint64 result = 0;
    readUnsigned(data, offset, width, little, result);
    return result;
  };
  const auto parseThin = [&](quint64 base, quint64 length,
                             quint64 expected_cpu, bool check_cpu) {
    const quint32 magic = magicAt(data, base);
    const bool is64 = magic == 0xfeedfacf || magic == 0xcffaedfe;
    const bool little = magic == 0xcefaedfe || magic == 0xcffaedfe;
    if (!is64 && magic != 0xfeedface && magic != 0xcefaedfe)
      return false;
    const quint64 header_size = is64 ? 32 : 28;
    if (base > size || length > size - base || length < header_size)
      return false;
    const quint64 cpu = value(base + 4, 4, little);
    if (check_cpu && cpu != expected_cpu)
      return false;
    const QString architecture = cpuName(cpu);
    const quint64 count = value(base + 16, 4, little);
    const quint64 commands_size = value(base + 20, 4, little);
    if (commands_size > length - header_size || count > commands_size / 8)
      return false;
    const quint64 commands_end = header_size + commands_size;
    regions.append({base, base + header_size, QColor(143, 116, 187),
                    QStringLiteral("Mach-O %1 header")
                        .arg(architecture), QStringLiteral("Header")});
    quint64 cursor = header_size;
    for (quint64 i = 0; i < count; ++i) {
      if (cursor > commands_end || 8 > commands_end - cursor)
        return false;
      const quint64 command = value(base + cursor, 4, little);
      const quint64 command_size = value(base + cursor + 4, 4, little);
      if (command_size < 8 || command_size > commands_end - cursor ||
          command_size % (is64 ? 8 : 4) != 0)
        return false;
      if (command == 1 || command == 0x19) {
        const bool segment64 = command == 0x19;
        if (segment64 != is64)
          return false;
        const quint64 minimum = segment64 ? 72 : 56;
        if (command_size < minimum)
          return false;
        const quint64 sections = value(base + cursor + (segment64 ? 64 : 48),
                                        4, little);
        if (sections > (command_size - minimum) / (segment64 ? 80 : 68))
          return false;
        const quint64 offset = value(base + cursor + (segment64 ? 40 : 32),
                                      segment64 ? 8 : 4, little);
        const quint64 file_size = value(base + cursor + (segment64 ? 48 : 36),
                                         segment64 ? 8 : 4, little);
        if (file_size != 0) {
          if (offset > length || file_size > length - offset)
            return false;
          const char* name_bytes = reinterpret_cast<const char*>(
              data.constData() + static_cast<qsizetype>(base + cursor + 8));
          int name_length = 0;
          while (name_length < 16 && name_bytes[name_length] != '\0')
            ++name_length;
          QString name = QString::fromLatin1(name_bytes, name_length);
          if (name.isEmpty())
            name = QStringLiteral("Unnamed segment");
          regions.append({base + offset, base + offset + file_size,
                          QColor::fromHsv(static_cast<int>((i * 47 + 195) % 360),
                                          125, 190),
                          QStringLiteral("%1 — %2").arg(architecture, name),
                          name});
        }
      }
      cursor += command_size;
    }
    return cursor == commands_end;
  };
  const auto parse = [&]() {
    if (!isSupported(data))
      return false;
    const quint32 magic = magicAt(data, 0);
    const bool fat64 = magic == 0xcafebabf || magic == 0xbfbafeca;
    const bool fat = fat64 || magic == 0xcafebabe || magic == 0xbebafeca;
    if (!fat)
      return parseThin(0, size, 0, false);
    if (size < 8)
      return false;
    const bool little = magic == 0xbebafeca || magic == 0xbfbafeca;
    const quint64 count = value(4, 4, little);
    const quint64 entry_size = fat64 ? 32 : 20;
    if (count == 0 || count > (size - 8) / entry_size)
      return false;
    const quint64 table_end = 8 + count * entry_size;
    struct Architecture {
      quint64 offset;
      quint64 length;
      quint64 cpu;
    };
    QVector<Architecture> architectures;
    architectures.reserve(static_cast<qsizetype>(count));
    for (quint64 i = 0; i < count; ++i) {
      const quint64 entry = 8 + i * entry_size;
      const quint64 cpu = value(entry, 4, little);
      const quint64 offset = value(entry + 8, fat64 ? 8 : 4, little);
      const quint64 length = value(entry + (fat64 ? 16 : 12),
                                   fat64 ? 8 : 4, little);
      const quint64 alignment = value(entry + (fat64 ? 24 : 16), 4, little);
      if (offset < table_end || offset > size || length == 0 ||
          length > size - offset || alignment >= 64 ||
          offset % (quint64(1) << alignment) != 0)
        return false;
      architectures.append({offset, length, cpu});
    }
    std::sort(architectures.begin(), architectures.end(),
              [](const Architecture& a, const Architecture& b) {
      return a.offset < b.offset;
    });
    quint64 previous_end = table_end;
    for (const Architecture& architecture : architectures) {
      if (architecture.offset < previous_end)
        return false;
      previous_end = architecture.offset + architecture.length;
    }
    regions.append({0, table_end, QColor(157, 133, 191),
                    QStringLiteral("Universal Mach-O%1 architecture table (%2)")
                        .arg(fat64 ? QStringLiteral("64") : QString())
                        .arg(count), QStringLiteral("Architecture table")});
    for (const Architecture& architecture : architectures) {
      if (!parseThin(architecture.offset, architecture.length,
                     architecture.cpu, true))
        return false;
    }
    return true;
  };
  if (!parse()) {
    StandardMap::calculateRegions(data, regions, font_color);
    return;
  }
  completeRegions(regions, size, font_color);
}
