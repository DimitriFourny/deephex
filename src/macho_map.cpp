#include "macho_map.h"
#include <QDebug>
#include <QVector>
#include <QtEndian>
#include <cstring>

// Mach-O magic numbers
#define MH_MAGIC 0xfeedface
#define MH_CIGAM 0xcefaedfe
#define MH_MAGIC_64 0xfeedfacf
#define MH_CIGAM_64 0xcffaedfe
#define FAT_MAGIC 0xcafebabe
#define FAT_CIGAM 0xbebafeca

// Load command types
#define LC_SEGMENT 0x1
#define LC_SEGMENT_64 0x19

// CPU types
#define CPU_TYPE_X86 0x7
#define CPU_TYPE_X86_64 0x1000007
#define CPU_TYPE_ARM 0xc
#define CPU_TYPE_ARM64 0x100000c

struct SegmentInfo {
  QString name;
  quint64 fileOffset;
  quint64 fileSize;
  quint64 vmAddress;
  quint64 vmSize;
};

struct ArchInfo {
  QString architecture;
  QVector<SegmentInfo> segments;
};

// Helper function to safely read data with bounds checking
template <typename T>
bool safeRead(const QVector<quint8>& data, quint32 offset, T& result)
{
  if (offset + sizeof(T) > static_cast<quint32>(data.size())) {
    return false;
  }
  memcpy(&result, data.constData() + offset, sizeof(T));
  return true;
}

// Helper function to safely read string with bounds checking
bool safeReadString(const QVector<quint8>& data,
                    quint32 offset,
                    quint32 maxLen,
                    QString& result)
{
  if (offset >= static_cast<quint32>(data.size())) {
    return false;
  }

  quint32 availableLen = data.size() - offset;
  quint32 readLen = qMin(maxLen, availableLen);

  // Find null terminator or use full length
  quint32 actualLen = 0;
  for (quint32 i = 0; i < readLen; ++i) {
    if (data[offset + i] == 0) {
      actualLen = i;
      break;
    }
    actualLen = i + 1;
  }

  result = QString::fromUtf8(
      reinterpret_cast<const char*>(data.constData() + offset), actualLen);
  return true;
}

// Helper function to determine endianness and return swapped value if needed
template <typename T>
T maybeSwap(T value, bool needSwap)
{
  if (!needSwap)
    return value;

  if (sizeof(T) == 2) {
    return qFromBigEndian<T>(value);
  }
  else if (sizeof(T) == 4) {
    return qFromBigEndian<T>(value);
  }
  else if (sizeof(T) == 8) {
    return qFromBigEndian<T>(value);
  }
  return value;
}

QString cpuTypeToString(quint32 cpuType)
{
  switch (cpuType) {
    case CPU_TYPE_X86:
      return "i386";
    case CPU_TYPE_X86_64:
      return "x86_64";
    case CPU_TYPE_ARM:
      return "arm";
    case CPU_TYPE_ARM64:
      return "arm64";
    default:
      return QString("unknown_%1").arg(cpuType);
  }
}

// Parse single Mach-O architecture
bool parseSingleMachO(const QVector<quint8>& data,
                      quint32 offset,
                      ArchInfo& archInfo)
{
  if (offset >= static_cast<quint32>(data.size())) {
    return false;
  }

  quint32 magic;
  if (!safeRead(data, offset, magic)) {
    return false;
  }

  bool is64Bit = false;
  bool needSwap = false;

  switch (magic) {
    case MH_MAGIC:
      is64Bit = false;
      needSwap = false;
      break;
    case MH_CIGAM:
      is64Bit = false;
      needSwap = true;
      break;
    case MH_MAGIC_64:
      is64Bit = true;
      needSwap = false;
      break;
    case MH_CIGAM_64:
      is64Bit = true;
      needSwap = true;
      break;
    default:
      return false;
  }

  // Read Mach-O header
  quint32 headerSize = is64Bit ? 32 : 28;
  if (offset + headerSize > static_cast<quint32>(data.size())) {
    return false;
  }

  quint32 cpuType, cpuSubType, fileType, ncmds, sizeofcmds, flags;
  quint32 reserved = 0;  // Only for 64-bit

  if (!safeRead(data, offset + 4, cpuType))
    return false;
  if (!safeRead(data, offset + 8, cpuSubType))
    return false;
  if (!safeRead(data, offset + 12, fileType))
    return false;
  if (!safeRead(data, offset + 16, ncmds))
    return false;
  if (!safeRead(data, offset + 20, sizeofcmds))
    return false;
  if (!safeRead(data, offset + 24, flags))
    return false;
  if (is64Bit && !safeRead(data, offset + 28, reserved))
    return false;

  // Apply byte swapping if needed
  cpuType = maybeSwap(cpuType, needSwap);
  ncmds = maybeSwap(ncmds, needSwap);
  sizeofcmds = maybeSwap(sizeofcmds, needSwap);

  archInfo.architecture = cpuTypeToString(cpuType);

  // Parse load commands
  quint32 cmdOffset = offset + headerSize;

  for (quint32 i = 0; i < ncmds; ++i) {
    if (cmdOffset + 8 > static_cast<quint32>(data.size())) {
      break;
    }

    quint32 cmd, cmdsize;
    if (!safeRead(data, cmdOffset, cmd))
      break;
    if (!safeRead(data, cmdOffset + 4, cmdsize))
      break;

    cmd = maybeSwap(cmd, needSwap);
    cmdsize = maybeSwap(cmdsize, needSwap);

    if (cmdOffset + cmdsize > static_cast<quint32>(data.size())) {
      break;
    }

    if (cmd == LC_SEGMENT || cmd == LC_SEGMENT_64) {
      SegmentInfo segInfo;

      // Read segment name (16 bytes)
      if (!safeReadString(data, cmdOffset + 8, 16, segInfo.name)) {
        cmdOffset += cmdsize;
        continue;
      }

      if (cmd == LC_SEGMENT_64) {
        // 64-bit segment
        quint64 vmaddr, vmsize, fileoff, filesize;
        if (!safeRead(data, cmdOffset + 24, vmaddr))
          break;
        if (!safeRead(data, cmdOffset + 32, vmsize))
          break;
        if (!safeRead(data, cmdOffset + 40, fileoff))
          break;
        if (!safeRead(data, cmdOffset + 48, filesize))
          break;

        segInfo.vmAddress = maybeSwap(vmaddr, needSwap);
        segInfo.vmSize = maybeSwap(vmsize, needSwap);
        segInfo.fileOffset = offset + maybeSwap(fileoff, needSwap);
        segInfo.fileSize = maybeSwap(filesize, needSwap);
      }
      else {
        // 32-bit segment
        quint32 vmaddr, vmsize, fileoff, filesize;
        if (!safeRead(data, cmdOffset + 24, vmaddr))
          break;
        if (!safeRead(data, cmdOffset + 28, vmsize))
          break;
        if (!safeRead(data, cmdOffset + 32, fileoff))
          break;
        if (!safeRead(data, cmdOffset + 36, filesize))
          break;

        segInfo.vmAddress = maybeSwap(vmaddr, needSwap);
        segInfo.vmSize = maybeSwap(vmsize, needSwap);
        segInfo.fileOffset = maybeSwap(fileoff, needSwap);
        segInfo.fileSize = maybeSwap(filesize, needSwap);
      }

      // Validate segment bounds
      if (segInfo.fileOffset + segInfo.fileSize <=
          static_cast<quint64>(data.size())) {
        archInfo.segments.append(segInfo);
      }
    }

    cmdOffset += cmdsize;
  }

  return true;
}

// Main function to parse Mach-O data
QVector<ArchInfo> parseMachOSegments(const QVector<quint8>& data)
{
  QVector<ArchInfo> result;

  if (data.size() < 4) {
    qDebug() << "Data too small to contain Mach-O header";
    return result;
  }

  quint32 magic;
  if (!safeRead(data, 0, magic)) {
    return result;
  }

  // Check if it's a universal binary
  if (magic == FAT_MAGIC || magic == FAT_CIGAM) {
    bool needSwap = (magic == FAT_CIGAM);

    if (data.size() < 8) {
      qDebug() << "Universal binary header too small";
      return result;
    }

    quint32 nfat_arch;
    if (!safeRead(data, 4, nfat_arch)) {
      return result;
    }

    nfat_arch = maybeSwap(nfat_arch, needSwap);

    if (8 + nfat_arch * 20 > static_cast<quint32>(data.size())) {
      qDebug() << "Universal binary architecture table extends beyond data";
      return result;
    }

    // Parse each architecture
    for (quint32 i = 0; i < nfat_arch; ++i) {
      quint32 archOffset = 8 + i * 20;

      quint32 cpuType, cpuSubType, offset, size, align;
      if (!safeRead(data, archOffset, cpuType))
        continue;
      if (!safeRead(data, archOffset + 4, cpuSubType))
        continue;
      if (!safeRead(data, archOffset + 8, offset))
        continue;
      if (!safeRead(data, archOffset + 12, size))
        continue;
      if (!safeRead(data, archOffset + 16, align))
        continue;

      offset = maybeSwap(offset, needSwap);
      size = maybeSwap(size, needSwap);

      // Validate architecture bounds
      if (offset + size > static_cast<quint32>(data.size())) {
        qDebug() << "Architecture" << i << "extends beyond data bounds";
        continue;
      }

      ArchInfo archInfo;
      if (parseSingleMachO(data, offset, archInfo)) {
        result.append(archInfo);
      }
    }
  }
  else {
    // Single architecture Mach-O
    ArchInfo archInfo;
    if (parseSingleMachO(data, 0, archInfo)) {
      result.append(archInfo);
    }
  }

  return result;
}

bool MachoMap::isSupported(const QVector<quint8>& data)
{
  if (data.size() < 4) {
    return false;
  }

  quint32 magic;
  if (!safeRead(data, 0, magic)) {
    return false;
  }

  // Check for universal binary (FAT) magic
  if (magic == FAT_MAGIC || magic == FAT_CIGAM) {
    // Verify we have enough data for the FAT header
    if (data.size() < 8) {
      return false;
    }

    bool needSwap = (magic == FAT_CIGAM);
    quint32 nfat_arch;
    if (!safeRead(data, 4, nfat_arch)) {
      return false;
    }

    nfat_arch = maybeSwap(nfat_arch, needSwap);

    // Sanity check: reasonable number of architectures (typically 1-4)
    if (nfat_arch == 0 || nfat_arch > 16) {
      return false;
    }

    // Verify we have enough data for the architecture table
    if (8 + nfat_arch * 20 > static_cast<quint32>(data.size())) {
      return false;
    }
  }

  return true;
}

QColor getDistinguishableColorRGB(int index)
{
  // First few colors manually chosen for maximum distinction
  static const std::vector<QColor> distinctColors = {
      QColor(255, 0, 0),      // Red
      QColor(0, 0, 255),      // Blue
      QColor(0, 255, 0),      // Green
      QColor(255, 255, 0),    // Yellow
      QColor(255, 0, 255),    // Magenta
      QColor(0, 255, 255),    // Cyan
      QColor(128, 0, 128),    // Purple
      QColor(255, 165, 0),    // Orange
      QColor(128, 128, 128),  // Gray
      QColor(165, 42, 42),    // Brown
      QColor(255, 192, 203),  // Pink
      QColor(0, 128, 0),      // Dark Green
      QColor(0, 0, 128),      // Navy
      QColor(128, 0, 0),      // Maroon
      QColor(128, 128, 0),    // Olive
      QColor(0, 128, 128)     // Teal
  };
  return distinctColors[index % distinctColors.size()];
}

void MachoMap::calculateRegions(const QVector<quint8>& data,
                                QVector<MapRegion>& regions,
                                QColor font_color)
{
  // StandardMap::calculateRegions(data, regions, font_color);

  QVector<ArchInfo> architectures = parseMachOSegments(data);

  if (architectures.isEmpty()) {
    qDebug() << "No valid Mach-O architectures found";
    return;
  }

  qDebug() << "Found" << architectures.size() << "architecture(s):";

  int colorIndex = 0;

  for (const ArchInfo& arch : architectures) {
    qDebug() << "\nArchitecture:" << arch.architecture;
    qDebug() << "Segments:" << arch.segments.size();

    for (const SegmentInfo& seg : arch.segments) {
      qDebug() << QString(
                      "  %1: offset=0x%2, size=0x%3, vmaddr=0x%4, vmsize=0x%5")
                      .arg(seg.name)
                      .arg(seg.fileOffset, 0, 16)
                      .arg(seg.fileSize, 0, 16)
                      .arg(seg.vmAddress, 0, 16)
                      .arg(seg.vmSize, 0, 16);

      regions.push_back({seg.fileOffset, seg.fileOffset + seg.fileSize,
                         getDistinguishableColorRGB(colorIndex++)});
    }
  }
}