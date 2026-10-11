#include "standard_map.h"
#include "macho_map.h"
#include "elf_map.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition " failed\n"; \
  std::exit(EXIT_FAILURE); } } while (false)

void put(std::vector<uint8_t>& data, size_t offset, uint64_t value,
         unsigned width, bool little = true)
{
  CHECK(offset <= data.size() && width <= data.size() - offset);
  for (unsigned i = 0; i < width; ++i)
    data[offset + i] = static_cast<uint8_t>(value >> (8 * (little ? i : width - 1 - i)));
}

std::vector<uint8_t> machO(bool little, bool is64)
{
  std::vector<uint8_t> data(160, 0);
  const size_t header = is64 ? 32 : 28;
  const unsigned command = is64 ? 72 : 56;
  put(data, 0, is64 ? 0xfeedfacf : 0xfeedface, 4, little);
  put(data, 4, is64 ? 0x0100000c : 12, 4, little);
  put(data, 16, 1, 4, little);
  put(data, 20, command, 4, little);
  put(data, header, is64 ? 0x19 : 1, 4, little);
  put(data, header + 4, command, 4, little);
  const std::string name = "__TEXT";
  std::copy(name.begin(), name.end(), data.begin() + header + 8);
  put(data, header + (is64 ? 40 : 32), 128, is64 ? 8 : 4, little);
  put(data, header + (is64 ? 48 : 36), 16, is64 ? 8 : 4, little);
  return data;
}

std::vector<uint8_t> elf(bool little, bool is64)
{
  std::vector<uint8_t> data(160, 0);
  data[0] = 0x7f; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
  data[4] = is64 ? 2 : 1; data[5] = little ? 1 : 2; data[6] = 1;
  const unsigned header = is64 ? 64 : 52;
  const unsigned entry = is64 ? 56 : 32;
  put(data, 20, 1, 4, little);
  put(data, is64 ? 32 : 28, header, is64 ? 8 : 4, little);
  put(data, is64 ? 52 : 40, header, 2, little);
  put(data, is64 ? 54 : 42, entry, 2, little);
  put(data, is64 ? 56 : 44, 1, 2, little);
  put(data, header, 1, 4, little);
  put(data, header + (is64 ? 8 : 4), 128, is64 ? 8 : 4, little);
  put(data, header + (is64 ? 32 : 16), 16, is64 ? 8 : 4, little);
  put(data, header + (is64 ? 40 : 20), 16, is64 ? 8 : 4, little);
  return data;
}

void coverage(const std::vector<MapRegion>& regions, uint64_t size)
{
  uint64_t end = 0;
  for (const auto& region : regions) {
    CHECK(region.start == end);
    CHECK(region.end > region.start);
    CHECK(region.end <= size);
    CHECK(!region.label.empty());
    CHECK(!region.legend_label.empty());
    CHECK(region.color.red >= 0 && region.color.red <= 1);
    CHECK(region.color.green >= 0 && region.color.green <= 1);
    CHECK(region.color.blue >= 0 && region.color.blue <= 1);
    end = region.end;
  }
  CHECK(end == size);
}

void fallback(StandardMap& parser, const std::vector<uint8_t>& data)
{
  std::vector<MapRegion> actual, expected;
  StandardMap standard;
  standard.calculateRegions(data, expected, MapColor());
  parser.calculateRegions(data, actual, MapColor());
  CHECK(actual.size() == expected.size());
  for (size_t i = 0; i < actual.size(); ++i) {
    CHECK(actual[i].start == expected[i].start);
    CHECK(actual[i].end == expected[i].end);
    CHECK(actual[i].label == expected[i].label);
    CHECK(actual[i].legend_label == expected[i].legend_label);
    CHECK(actual[i].color.red == expected[i].color.red);
    CHECK(actual[i].color.green == expected[i].color.green);
    CHECK(actual[i].color.blue == expected[i].color.blue);
  }
}

void distributionCoverage()
{
  StandardMap map;
  std::vector<MapRegion> regions;
  for (size_t size : {0, 1, 99, 101, 513, 4097, 1024 * 1024, 1024 * 1024 + 1}) {
    map.calculateRegions(std::vector<uint8_t>(size, 0x41), regions, MapColor());
    coverage(regions, size);
    CHECK(regions.size() == (size == 0 ? 0 : std::min<size_t>(256, (size - 1) / 4096 + 1)));
    for (const auto& region : regions) {
      CHECK(region.legend_label == "ASCII text");
      CHECK(region.label == "ASCII text (0% zero, 100% ASCII)");
    }
  }
  for (const auto& data : {std::vector<uint8_t>{0, 0x41},
                           std::vector<uint8_t>{0, 0x41, 0x41},
                           std::vector<uint8_t>{0, 0x80, 0x80}}) {
    map.calculateRegions(data, regions, MapColor());
    coverage(regions, data.size());
    const std::string expected = data.size() == 2 ? "Zero-filled" :
                                 data[1] == 0x41 ? "ASCII text" : "Binary";
    CHECK(regions[0].legend_label == expected);
  }
}

void executableRegions()
{
  for (bool little : {false, true}) for (bool is64 : {false, true}) {
    for (bool isMachO : {false, true}) {
      auto data = isMachO ? machO(little, is64) : elf(little, is64);
      std::vector<MapRegion> regions;
      MachoMap macho;
      ElfMap elfMap;
      StandardMap& map = isMachO ? static_cast<StandardMap&>(macho) : static_cast<StandardMap&>(elfMap);
      CHECK(isMachO ? MachoMap::isSupported(data) : ElfMap::isSupported(data));
      map.calculateRegions(data, regions, MapColor());
      coverage(regions, data.size());
      bool found = false;
      for (const auto& region : regions) {
        if (region.start == 128 && region.end == 144) {
          found = true;
          CHECK(region.label == (isMachO ? std::string(is64 ? "arm64" : "arm") + " — __TEXT"
                                        : "ELF LOAD #0 (---)"));
        }
      }
      CHECK(found);
      if (isMachO) put(data, (is64 ? 32 : 28) + 4, 0, 4, little);
      else put(data, (is64 ? 64 : 52) + (is64 ? 8 : 4),
               is64 ? std::numeric_limits<uint64_t>::max() : 0xffffffffu,
               is64 ? 8 : 4, little);
      fallback(map, data);
    }
  }
}

void universalMachO()
{
  MachoMap map;
  for (bool little : {false, true}) for (bool fat64 : {false, true}) {
    std::vector<uint8_t> data(672, 0);
    const unsigned entry = fat64 ? 32 : 20;
    put(data, 0, fat64 ? 0xcafebabf : 0xcafebabe, 4, little);
    put(data, 4, 2, 4, little);
    for (unsigned i = 0; i < 2; ++i) {
      const size_t offset = 256 + i * 256;
      const size_t table = 8 + i * entry;
      const auto thin = machO(i == 0, i == 0);
      std::copy(thin.begin(), thin.end(), data.begin() + offset);
      put(data, table, i == 0 ? 0x0100000c : 12, 4, little);
      put(data, table + 8, offset, fat64 ? 8 : 4, little);
      put(data, table + (fat64 ? 16 : 12), thin.size(), fat64 ? 8 : 4, little);
      put(data, table + (fat64 ? 24 : 16), 8, 4, little);
    }
    std::vector<MapRegion> regions;
    map.calculateRegions(data, regions, MapColor());
    coverage(regions, data.size());
    CHECK(regions.front().label == std::string("Universal Mach-O") + (fat64 ? "64" : "") +
                                      " architecture table (2)");
    for (uint64_t segment : {384, 640}) {
      CHECK(std::any_of(regions.begin(), regions.end(), [segment](const MapRegion& r) {
        return r.start == segment && r.end == segment + 16 && r.legend_label == "__TEXT";
      }));
    }
    auto malformed = data;
    put(malformed, 8 + entry + 8, 256, fat64 ? 8 : 4, little);
    fallback(map, malformed);
    malformed = data;
    put(malformed, 8, 7, 4, little);
    fallback(map, malformed);
    malformed = data;
    put(malformed, 8 + (fat64 ? 24 : 16), 64, 4, little);
    fallback(map, malformed);
  }
}

void truncatedExecutables()
{
  MachoMap macho;
  ElfMap elfMap;
  for (size_t size = 0; size < 160; ++size) {
    auto m = machO(true, true);
    auto e = elf(false, false);
    m.resize(size);
    e.resize(size);
    // Every prefix shorter than the segment's exclusive end is malformed.
    if (size < 144) {
      fallback(macho, m);
      fallback(elfMap, e);
    }
  }
}
}  // namespace

int main()
{
  distributionCoverage();
  executableRegions();
  universalMachO();
  truncatedExecutables();
  std::cout << "Map parser regression tests passed\n";
  return EXIT_SUCCESS;
}
