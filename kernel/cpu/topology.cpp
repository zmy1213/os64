#include "cpu/topology.hpp"
#include "cpu/xapic.hpp"
namespace {
uint16_t u16(const uint8_t *p) { return p[0] | (uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t(u16(p + 2)) << 16); }
uint64_t u64(const uint8_t *p) { return u32(p) | (uint64_t(u32(p + 4)) << 32); }
bool signature(const uint8_t *p, const char *text, size_t n) {
  if (!p)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (p[i] != uint8_t(text[i]))
      return false;
  return true;
}
bool checksum(const uint8_t *p, size_t n) {
  uint8_t sum = 0;
  for (size_t i = 0; i < n; ++i)
    sum = uint8_t(sum + p[i]);
  return sum == 0;
}
bool add_cpu(CpuTopology &topology, bool seen[256], uint8_t id) {
  if (!xapic_unicast_id_valid(id) || seen[id])
    return false;
  seen[id] = true;
  if (id != topology.apic_ids[0] && topology.count < kSmpMaxCpuCount)
    topology.apic_ids[topology.count++] = id;
  return true;
}
const uint8_t *sdt(FirmwareRead read, void *context, uint64_t physical,
                   uint32_t *size) {
  const auto *head = read(physical, 36, context);
  if (!head)
    return nullptr;
  *size = u32(head + 4);
  if (*size < 36 || *size > 1024 * 1024)
    return nullptr;
  const auto *whole = read(physical, *size, context);
  return whole && checksum(whole, *size) ? whole : nullptr;
}
bool madt(FirmwareRead read, void *context, uint64_t physical, uint8_t bsp,
          CpuTopology *output) {
  uint32_t length = 0;
  const auto *table = sdt(read, context, physical, &length);
  if (!signature(table, "APIC", 4) || length < 44)
    return false;
  CpuTopology result{};
  result.count = 1;
  result.apic_ids[0] = bsp;
  result.local_apic_physical = u32(table + 36);
  result.from_acpi = true;
  bool seen[256]{};
  for (uint32_t offset = 44; offset < length;) {
    if (length - offset < 2)
      return false;
    const auto *entry = table + offset;
    const uint8_t size = entry[1];
    if (size < 2 || size > length - offset)
      return false;
    if (entry[0] == 0) {
      if (size != 8)
        return false;
      if ((u32(entry + 4) & 1) && !add_cpu(result, seen, entry[3]))
        return false;
    } else if (entry[0] == 5) {
      if (size != 12)
        return false;
      result.local_apic_physical = u64(entry + 4);
    }
    offset += size;
  }
  if (!seen[bsp] || !result.local_apic_physical ||
      (result.local_apic_physical & 4095) ||
      result.local_apic_physical >= 0x100000000ULL)
    return false;
  *output = result;
  return true;
}
bool acpi(FirmwareRead read, void *context, const uint8_t *rsdp, uint8_t bsp,
          CpuTopology *output) {
  if (!checksum(rsdp, 20))
    return false;
  uint64_t root = u32(rsdp + 16);
  size_t width = 4;
  if (rsdp[15] >= 2) {
    const uint32_t length = u32(rsdp + 20);
    if (length < 36 || length > 4096 || !checksum(rsdp, length))
      return false;
    if (u64(rsdp + 24)) {
      root = u64(rsdp + 24);
      width = 8;
    }
  }
  uint32_t size = 0;
  const auto *table = sdt(read, context, root, &size);
  if (!signature(table, width == 8 ? "XSDT" : "RSDT", 4) || (size - 36) % width)
    return false;
  for (size_t offset = 36; offset < size; offset += width) {
    const uint64_t physical =
        width == 8 ? u64(table + offset) : u32(table + offset);
    const auto *head = read(physical, 4, context);
    if (signature(head, "APIC", 4) &&
        madt(read, context, physical, bsp, output))
      return true;
  }
  return false;
}
bool mp(FirmwareRead read, void *context, const uint8_t *floating, uint8_t bsp,
        CpuTopology *output) {
  if (floating[8] != 1 || (floating[9] != 1 && floating[9] != 4) ||
      !checksum(floating, 16) || floating[11] != 0)
    return false;
  const uint64_t physical = u32(floating + 4);
  const auto *head = read(physical, 44, context);
  if (!signature(head, "PCMP", 4))
    return false;
  const uint16_t length = u16(head + 4), count = u16(head + 34);
  if (length < 44 || (head[6] != 1 && head[6] != 4))
    return false;
  const auto *table = read(physical, length, context);
  if (!table || !checksum(table, length))
    return false;
  CpuTopology result{};
  result.count = 1;
  result.apic_ids[0] = bsp;
  result.local_apic_physical = u32(table + 36);
  bool seen[256]{};
  size_t offset = 44;
  for (uint16_t i = 0; i < count; ++i) {
    if (offset >= length)
      return false;
    const auto *entry = table + offset;
    const size_t size = entry[0] == 0 ? 20 : 8;
    if (entry[0] > 4 || size > length - offset)
      return false;
    if (entry[0] == 0 && (entry[3] & 1) && !add_cpu(result, seen, entry[1]))
      return false;
    offset += size;
  }
  if (offset != length || !seen[bsp] || !result.local_apic_physical ||
      (result.local_apic_physical & 4095))
    return false;
  *output = result;
  return true;
}
bool scan(FirmwareRead read, void *context, uint64_t start, uint64_t end,
          bool want_acpi, uint8_t bsp, CpuTopology *output) {
  for (uint64_t address = start; address + 16 <= end; address += 16) {
    const auto *data = read(address, want_acpi ? 20 : 16, context);
    if (!data)
      continue;
    if (want_acpi && signature(data, "RSD PTR ", 8)) {
      if (data[15] >= 2) {
        data = read(address, 36, context);
        if (!data)
          continue;
        const uint32_t length = u32(data + 20);
        if (length < 36 || length > 4096)
          continue;
        data = read(address, length, context);
        if (!data)
          continue;
      }
      if (acpi(read, context, data, bsp, output))
        return true;
    } else if (!want_acpi && signature(data, "_MP_", 4) &&
               mp(read, context, data, bsp, output))
      return true;
  }
  return false;
}
} // namespace
bool firmware_find_cpu_topology(FirmwareRead read, void *context, uint8_t bsp,
                                CpuTopology *output) {
  if (!read || !output)
    return false;
  const auto *ebda_data = read(0x40e, 2, context);
  const uint64_t ebda = ebda_data ? uint64_t(u16(ebda_data)) << 4 : 0;
  const auto *memory_data = read(0x413, 2, context);
  const uint64_t conventional =
      memory_data ? uint64_t(u16(memory_data)) * 1024 : 0;
  for (unsigned mode = 0; mode < 2; ++mode) {
    const bool want_acpi = mode == 0;
    if (ebda >= 0x80000 && ebda < 0xa0000 &&
        scan(read, context, ebda, ebda + 1024, want_acpi, bsp, output))
      return true;
    if (!want_acpi && conventional >= 1024 && conventional <= 0xa0000 &&
        scan(read, context, conventional - 1024, conventional, false, bsp,
             output))
      return true;
    if (scan(read, context, 0xe0000, 0x100000, want_acpi, bsp, output))
      return true;
  }
  return false;
}
