// Exercise the real firmware parser with bounded synthetic physical memory.
#include "cpu/topology.hpp"
#include "cpu/xapic.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
namespace {
constexpr size_t kRsdp = 0xe0000, kRoot = 0xe1000, kMadt = 0xe2000,
                 kMp = 0xf0000;
std::vector<uint8_t> ram(0x100000);
void require(bool ok, const char *message) {
  if (!ok) {
    std::cerr << "FAIL topology: " << message << '\n';
    std::exit(1);
  }
}
const uint8_t *read(uint64_t address, size_t size, void *) {
  if (address > ram.size() || size > ram.size() - address)
    return nullptr;
  return ram.data() + address;
}
void p16(size_t a, uint16_t n) {
  ram[a] = n;
  ram[a + 1] = n >> 8;
}
void p32(size_t a, uint32_t n) {
  p16(a, n);
  p16(a + 2, n >> 16);
}
void p64(size_t a, uint64_t n) {
  p32(a, n);
  p32(a + 4, n >> 32);
}
void sig(size_t a, const char *s, size_t n) {
  std::memcpy(ram.data() + a, s, n);
}
void sum(size_t a, size_t n, size_t offset) {
  ram[a + offset] = 0;
  uint8_t check = 0;
  for (size_t i = 0; i < n; ++i)
    check += ram[a + i];
  ram[a + offset] = uint8_t(-check);
}
void sdt(size_t a, const char *name, uint32_t n) {
  sig(a, name, 4);
  p32(a + 4, n);
  ram[a + 8] = 1;
  sum(a, n, 9);
}
void acpi(bool extended = false, unsigned cpus = 4) {
  std::fill(ram.begin(), ram.end(), 0);
  p16(0x40e, 0x9fc0);
  p16(0x413, 640);
  sig(kRsdp, "RSD PTR ", 8);
  p32(kRsdp + 16, kRoot);
  if (extended) {
    ram[kRsdp + 15] = 2;
    p32(kRsdp + 20, 36);
    p64(kRsdp + 24, kRoot);
  }
  const uint32_t length = 44 + cpus * 8;
  p32(kMadt + 36, 0xfee00000);
  for (unsigned i = 0; i < cpus; ++i) {
    size_t a = kMadt + 44 + i * 8;
    ram[a] = 0;
    ram[a + 1] = 8;
    ram[a + 3] = uint8_t(7 + i);
    p32(a + 4, 1);
  }
  sdt(kMadt, "APIC", length);
  if (extended) {
    p64(kRoot + 36, kMadt);
    sdt(kRoot, "XSDT", 44);
  } else {
    p32(kRoot + 36, kMadt);
    sdt(kRoot, "RSDT", 40);
  }
  sum(kRsdp, 20, 8);
  if (extended)
    sum(kRsdp, 36, 32);
}
void mp(unsigned cpus = 4, size_t floating = kRsdp) {
  std::fill(ram.begin(), ram.end(), 0);
  sig(floating, "_MP_", 4);
  p32(floating + 4, kMp);
  ram[floating + 8] = 1;
  ram[floating + 9] = 4;
  sum(floating, 16, 10);
  sig(kMp, "PCMP", 4);
  const uint16_t n = 44 + 20 * cpus;
  p16(kMp + 4, n);
  ram[kMp + 6] = 4;
  p16(kMp + 34, cpus);
  p32(kMp + 36, 0xfee00000);
  for (unsigned i = 0; i < cpus; ++i) {
    size_t a = kMp + 44 + i * 20;
    ram[a] = 0;
    ram[a + 1] = uint8_t(7 + i);
    ram[a + 3] = 1;
  }
  sum(kMp, n, 7);
}
bool find(CpuTopology &t) {
  return firmware_find_cpu_topology(read, nullptr, 7, &t);
}
void bad(const char *message) {
  CpuTopology t{};
  t.count = 99;
  t.local_apic_physical = 42;
  require(!find(t), message);
  require(t.count == 99 && t.local_apic_physical == 42,
          "failure publishes partial result");
}
} // namespace
int main() {
  CpuTopology t{};
  acpi();
  require(find(t) && t.from_acpi && t.count == 4 && t.apic_ids[0] == 7 &&
              t.apic_ids[3] == 10,
          "ACPI RSDT four cores");
  acpi(true, 6);
  require(find(t) && t.count == 4 && t.apic_ids[3] == 10, "XSDT CPU cap");
  acpi(true);
  p64(kRoot + 36, UINT64_MAX);
  sum(kRoot, 44, 9);
  bad("XSDT out-of-range physical pointer");
  acpi(true);
  p32(kRsdp + 20, 4097);
  sum(kRsdp, 20, 8);
  bad("oversize RSDP");
  acpi();
  ram[kRsdp + 8]++;
  bad("RSDP checksum");
  acpi();
  ram[kMadt + 9]++;
  bad("MADT checksum");
  acpi();
  p32(kRoot + 4, 35);
  bad("short SDT");
  acpi();
  p32(kRoot + 4, 0x100001);
  bad("oversize SDT");
  acpi();
  ram[kMadt + 45] = 0;
  sum(kMadt, 76, 9);
  bad("MADT zero length entry");
  acpi();
  ram[kMadt + 45] = 255;
  sum(kMadt, 76, 9);
  bad("MADT entry beyond end");
  acpi();
  ram[kMadt + 53] = 7;
  sum(kMadt, 76, 9);
  bad("MADT CPU wrong entry size");
  acpi();
  ram[kMadt + 55] = 7;
  sum(kMadt, 76, 9);
  bad("duplicate APIC IDs");
  acpi();
  ram[kMadt + 55] = 255;
  sum(kMadt, 76, 9);
  bad("MADT broadcast APIC ID");
  acpi();
  ram[kMadt + 47] = 255;
  sum(kMadt, 76, 9);
  require(!firmware_find_cpu_topology(read, nullptr, 255, &t),
          "MADT broadcast BSP");
  acpi();
  ram[kMadt + 55] = 254;
  sum(kMadt, 76, 9);
  require(find(t) && t.apic_ids[1] == 254, "last valid unicast APIC ID");
  acpi();
  p32(kMadt + 48, 0);
  sum(kMadt, 76, 9);
  bad("disabled BSP");
  acpi();
  p32(kMadt + 36, 0xfee00001);
  sum(kMadt, 76, 9);
  bad("unaligned APIC");
  acpi();
  p32(kMadt + 4, 45);
  sum(kMadt, 45, 9);
  bad("truncated MADT header");
  acpi();
  ram[kMadt + 44] = 42;
  sum(kMadt, 76, 9);
  bad("unknown entry removes BSP");
  acpi();
  p32(kMadt + 4, 88);
  ram[kMadt + 76] = 5;
  ram[kMadt + 77] = 12;
  p64(kMadt + 80, 0x100000000ULL);
  sum(kMadt, 88, 9);
  bad("xAPIC override beyond 32 bits");
  acpi();
  p32(kMadt + 4, 88);
  ram[kMadt + 76] = 5;
  ram[kMadt + 77] = 12;
  p64(kMadt + 80, 0xfec00000);
  sum(kMadt, 88, 9);
  require(find(t) && t.local_apic_physical == 0xfec00000,
          "valid APIC override");
  mp();
  require(find(t) && !t.from_acpi && t.count == 4, "MP fallback");
  mp(6);
  require(find(t) && t.count == 4, "MP CPU cap");
  mp(4, 0xffff0);
  require(find(t) && t.count == 4, "MP at final 16 BIOS bytes");
  mp();
  ram[kRsdp + 11] = 1;
  sum(kRsdp, 16, 10);
  bad("unsupported default MP configuration");
  mp();
  ram[kMp + 64] = 5;
  sum(kMp, 124, 7);
  bad("unknown MP entry type");
  mp();
  p16(kMp + 34, 5);
  sum(kMp, 124, 7);
  bad("MP count exceeds table");
  mp();
  p16(kMp + 34, 3);
  sum(kMp, 124, 7);
  bad("MP unconsumed trailing entry");
  mp();
  ram[kMp + 65] = 7;
  sum(kMp, 124, 7);
  bad("MP duplicate CPU");
  mp();
  ram[kMp + 65] = 255;
  sum(kMp, 124, 7);
  bad("MP broadcast APIC ID");
  mp();
  ram[kMp + 45] = 255;
  sum(kMp, 124, 7);
  require(!firmware_find_cpu_topology(read, nullptr, 255, &t),
          "MP broadcast BSP");
  require(xapic_unicast_id_valid(0) && xapic_unicast_id_valid(254) &&
              !xapic_unicast_id_valid(255) &&
              !xapic_unicast_id_valid(UINT32_MAX),
          "IPI unicast guard");
  require(xapic_base_supported(0xfee00900, 0xfee00000), "BSP LAPIC base");
  require(xapic_base_supported(0xfee00800, 0xfee00000), "AP LAPIC base");
  require(!xapic_base_supported(0xfee00c00, 0xfee00000), "x2APIC rejected");
  require(!xapic_base_supported(0x1fee00900, 0xfee00000),
          "high MSR base cannot alias low address");
  require(!xapic_base_supported(0x1fee00900, 0x1fee00000),
          "high LAPIC address unsupported");
  require(!xapic_base_supported(0xfee00900, 0xfec00000),
          "AP base must match shared mapping");
  require(!xapic_base_supported(0xfee00900, 0xfee00001) &&
              !xapic_base_supported(0x800, 0),
          "invalid physical LAPIC base");
  mp();
  ram[kMp + 47] = 0;
  sum(kMp, 124, 7);
  bad("MP disabled BSP");
  mp();
  p32(kRsdp + 4, 0xfffff0);
  sum(kRsdp, 16, 10);
  bad("MP out-of-range table");
  std::fill(ram.begin(), ram.end(), 0);
  bad("no tables");
  require(!firmware_find_cpu_topology(nullptr, nullptr, 7, &t), "null reader");
  require(!firmware_find_cpu_topology(read, nullptr, 7, nullptr),
          "null result");
  std::cout << "topology host: ACPI/MP bounds, checksums, CPU cap, duplicate "
               "IDs and partial-publication checks passed\n";
}
