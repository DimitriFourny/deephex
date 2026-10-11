#include "elf_map.h"

#include <sstream>

bool ElfMap::isSupported(const std::vector<uint8_t>& data)
{
  return data.size() >= 4 && data[0] == 0x7f && data[1] == 'E' &&
         data[2] == 'L' && data[3] == 'F';
}

void ElfMap::calculateRegions(const std::vector<uint8_t>& data,
                              std::vector<MapRegion>& regions,
                              MapColor font_color)
{
  regions.clear();
  const uint64_t size = data.size();
  const auto parse = [&]() {
    if (!isSupported(data) || size < 16 || (data[4] != 1 && data[4] != 2) ||
        (data[5] != 1 && data[5] != 2) || data[6] != 1)
      return false;
    const bool is64 = data[4] == 2;
    const bool little = data[5] == 1;
    const uint64_t header_size = is64 ? 64 : 52;
    if (size < header_size)
      return false;
    const auto value = [&](uint64_t offset, unsigned width) {
      uint64_t result = 0;
      readUnsigned(data, offset, width, little, result);
      return result;
    };
    if (value(20, 4) != 1)
      return false;
    const uint64_t ehsize = value(is64 ? 52 : 40, 2);
    const uint64_t phoff = value(is64 ? 32 : 28, is64 ? 8 : 4);
    const uint64_t entry_size = value(is64 ? 54 : 42, 2);
    uint64_t count = value(is64 ? 56 : 44, 2);
    if (ehsize < header_size || ehsize > size)
      return false;
    if (count == 0xffff) {
      const uint64_t shoff = value(is64 ? 40 : 32, is64 ? 8 : 4);
      const uint64_t shsize = value(is64 ? 58 : 46, 2);
      const uint64_t minimum = is64 ? 64 : 40;
      if (shsize < minimum || shoff > size || shsize > size - shoff)
        return false;
      count = value(shoff + (is64 ? 44 : 28), 4);
    }
    if (count != 0 && (entry_size < (is64 ? 56u : 32u) || phoff < ehsize ||
                       phoff > size || count > (size - phoff) / entry_size))
      return false;
    regions.push_back({0, ehsize, MapColor::fromRgb(143, 116, 187),
                       std::string("ELF") + (is64 ? "64" : "32") + " header (" +
                           (little ? "little" : "big") + " endian)",
                       "Header"});
    if (count != 0)
      regions.push_back({phoff, phoff + count * entry_size,
                         MapColor::fromRgb(157, 133, 191),
                         "ELF program headers", "Program headers"});
    for (uint64_t i = 0; i < count; ++i) {
      const uint64_t base = phoff + i * entry_size;
      const uint64_t type = value(base, 4);
      const uint64_t offset = value(base + (is64 ? 8 : 4), is64 ? 8 : 4);
      const uint64_t length = value(base + (is64 ? 32 : 16), is64 ? 8 : 4);
      const uint64_t flags = value(base + (is64 ? 4 : 24), 4);
      if (length == 0)
        continue;
      if (offset > size || length > size - offset)
        return false;
      std::string name;
      switch (type) {
        case 0:
          name = "NULL";
          break;
        case 1:
          name = "LOAD";
          break;
        case 2:
          name = "DYNAMIC";
          break;
        case 3:
          name = "INTERP";
          break;
        case 4:
          name = "NOTE";
          break;
        case 5:
          name = "SHLIB";
          break;
        case 6:
          name = "PHDR";
          break;
        case 7:
          name = "TLS";
          break;
        case 0x6474e550:
          name = "GNU_EH_FRAME";
          break;
        case 0x6474e551:
          name = "GNU_STACK";
          break;
        case 0x6474e552:
          name = "GNU_RELRO";
          break;
        default: {
          std::ostringstream text;
          text << "Type 0x" << std::hex << type;
          name = text.str();
          break;
        }
      }
      const std::string permissions = std::string(flags & 4 ? "R" : "-") +
                                      (flags & 2 ? "W" : "-") +
                                      (flags & 1 ? "X" : "-");
      const std::string label =
          "ELF " + name + " #" + std::to_string(i) + " (" + permissions + ")";
      regions.push_back(
          {offset, offset + length,
           MapColor::fromHsv(static_cast<unsigned>((i * 47 + 195) % 360), 125,
                             190),
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
