#include "macho_map.h"

#include <algorithm>
#include <sstream>
namespace {
uint32_t magicAt(const std::vector<uint8_t>& data, uint64_t offset)
{
  const uint64_t size = data.size();
  if (offset > size || 4 > size - offset)
    return 0;
  const uint8_t* bytes = data.data() + static_cast<size_t>(offset);
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
         (uint32_t(bytes[2]) << 8) | bytes[3];
}

std::string cpuName(uint64_t cpu)
{
  switch (cpu) {
    case 7: return "i386";
    case 0x01000007: return "x86_64";
    case 12: return "arm";
    case 0x0100000c: return "arm64";
    case 0x0200000c: return "arm64_32";
    case 18: return "ppc";
    case 0x01000012: return "ppc64";
    default: {
      std::ostringstream text;
      text << "CPU 0x" << std::hex << cpu;
      return text.str();
    }
  }
}
}  // namespace

bool MachoMap::isSupported(const std::vector<uint8_t>& data)
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

void MachoMap::calculateRegions(const std::vector<uint8_t>& data,
                                std::vector<MapRegion>& regions,
                                MapColor font_color)
{
  regions.clear();
  const uint64_t size = data.size();
  const auto value = [&](uint64_t offset, unsigned width, bool little) {
    uint64_t result = 0;
    readUnsigned(data, offset, width, little, result);
    return result;
  };
  const auto parseThin = [&](uint64_t base, uint64_t length,
                             uint64_t expected_cpu, bool check_cpu) {
    const uint32_t magic = magicAt(data, base);
    const bool is64 = magic == 0xfeedfacf || magic == 0xcffaedfe;
    const bool little = magic == 0xcefaedfe || magic == 0xcffaedfe;
    if (!is64 && magic != 0xfeedface && magic != 0xcefaedfe)
      return false;
    const uint64_t header_size = is64 ? 32 : 28;
    if (base > size || length > size - base || length < header_size)
      return false;
    const uint64_t cpu = value(base + 4, 4, little);
    if (check_cpu && cpu != expected_cpu)
      return false;
    const std::string architecture = cpuName(cpu);
    const uint64_t count = value(base + 16, 4, little);
    const uint64_t commands_size = value(base + 20, 4, little);
    if (commands_size > length - header_size || count > commands_size / 8)
      return false;
    const uint64_t commands_end = header_size + commands_size;
    regions.push_back({base, base + header_size, MapColor::fromRgb(143, 116, 187),
                       "Mach-O " + architecture + " header", "Header"});
    uint64_t cursor = header_size;
    for (uint64_t i = 0; i < count; ++i) {
      if (cursor > commands_end || 8 > commands_end - cursor)
        return false;
      const uint64_t command = value(base + cursor, 4, little);
      const uint64_t command_size = value(base + cursor + 4, 4, little);
      if (command_size < 8 || command_size > commands_end - cursor ||
          command_size % (is64 ? 8 : 4) != 0)
        return false;
      if (command == 1 || command == 0x19) {
        const bool segment64 = command == 0x19;
        if (segment64 != is64)
          return false;
        const uint64_t minimum = segment64 ? 72 : 56;
        if (command_size < minimum)
          return false;
        const uint64_t sections = value(base + cursor + (segment64 ? 64 : 48),
                                        4, little);
        if (sections > (command_size - minimum) / (segment64 ? 80 : 68))
          return false;
        const uint64_t offset = value(base + cursor + (segment64 ? 40 : 32),
                                      segment64 ? 8 : 4, little);
        const uint64_t file_size = value(base + cursor + (segment64 ? 48 : 36),
                                         segment64 ? 8 : 4, little);
        if (file_size != 0) {
          if (offset > length || file_size > length - offset)
            return false;
          const char* name_bytes = reinterpret_cast<const char*>(
              data.data() + static_cast<size_t>(base + cursor + 8));
          size_t name_length = 0;
          while (name_length < 16 && name_bytes[name_length] != '\0')
            ++name_length;
          // Mach-O names are Latin-1 bytes; preserve their UTF-8 display text.
          std::string name;
          for (size_t j = 0; j < name_length; ++j) {
            const unsigned char byte = static_cast<unsigned char>(name_bytes[j]);
            if (byte >= 128) {
              name.push_back(static_cast<char>(0xc0 | (byte >> 6)));
              name.push_back(static_cast<char>(0x80 | (byte & 0x3f)));
            } else {
              name.push_back(static_cast<char>(byte));
            }
          }
          if (name.empty())
            name = "Unnamed segment";
          regions.push_back({base + offset, base + offset + file_size,
                             MapColor::fromHsv(static_cast<unsigned>((i * 47 + 195) % 360),
                                               125, 190),
                             architecture + " — " + name, name});
        }
      }
      cursor += command_size;
    }
    return cursor == commands_end;
  };
  const auto parse = [&]() {
    if (!isSupported(data))
      return false;
    const uint32_t magic = magicAt(data, 0);
    const bool fat64 = magic == 0xcafebabf || magic == 0xbfbafeca;
    const bool fat = fat64 || magic == 0xcafebabe || magic == 0xbebafeca;
    if (!fat)
      return parseThin(0, size, 0, false);
    if (size < 8)
      return false;
    const bool little = magic == 0xbebafeca || magic == 0xbfbafeca;
    const uint64_t count = value(4, 4, little);
    const uint64_t entry_size = fat64 ? 32 : 20;
    if (count == 0 || count > (size - 8) / entry_size)
      return false;
    const uint64_t table_end = 8 + count * entry_size;
    struct Architecture {
      uint64_t offset;
      uint64_t length;
      uint64_t cpu;
    };
    std::vector<Architecture> architectures;
    architectures.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
      const uint64_t entry = 8 + i * entry_size;
      const uint64_t cpu = value(entry, 4, little);
      const uint64_t offset = value(entry + 8, fat64 ? 8 : 4, little);
      const uint64_t length = value(entry + (fat64 ? 16 : 12),
                                   fat64 ? 8 : 4, little);
      const uint64_t alignment = value(entry + (fat64 ? 24 : 16), 4, little);
      if (offset < table_end || offset > size || length == 0 ||
          length > size - offset || alignment >= 64 ||
          offset % (uint64_t(1) << alignment) != 0)
        return false;
      architectures.push_back({offset, length, cpu});
    }
    std::sort(architectures.begin(), architectures.end(),
              [](const Architecture& a, const Architecture& b) {
      return a.offset < b.offset;
    });
    uint64_t previous_end = table_end;
    for (const Architecture& architecture : architectures) {
      if (architecture.offset < previous_end)
        return false;
      previous_end = architecture.offset + architecture.length;
    }
    regions.push_back({0, table_end, MapColor::fromRgb(157, 133, 191),
                       std::string("Universal Mach-O") + (fat64 ? "64" : "") +
                           " architecture table (" + std::to_string(count) + ")",
                       "Architecture table"});
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
