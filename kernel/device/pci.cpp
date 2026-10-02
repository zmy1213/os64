#include "device/pci.hpp"

namespace {
uint32_t address(const PciDevice& d, uint8_t offset) {
  return 0x80000000U | (static_cast<uint32_t>(d.bus) << 16) |
         (static_cast<uint32_t>(d.slot) << 11) |
         (static_cast<uint32_t>(d.function) << 8) | (offset & 0xfc);
}
void select(const PciDevice& d, uint8_t offset) {
  const uint32_t value = address(d, offset);
  asm volatile("outl %0, %1" : : "a"(value), "Nd"(uint16_t{0xcf8}));
}
}
uint32_t pci_read32(const PciDevice& d, uint8_t offset) {
  select(d, offset);
  uint32_t value;
  asm volatile("inl %1, %0" : "=a"(value) : "Nd"(uint16_t{0xcfc}));
  return value;
}
uint16_t pci_read16(const PciDevice& d, uint8_t offset) {
  select(d, offset);
  uint16_t value;
  const uint16_t port = static_cast<uint16_t>(0xcfc + (offset & 2));
  asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}
void pci_write16(const PciDevice& d, uint8_t offset, uint16_t value) {
  select(d, offset);
  const uint16_t port = static_cast<uint16_t>(0xcfc + (offset & 2));
  // 只写 command 的 16 位，不能把相邻 status 的 write-one-to-clear 位一并回写。
  asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
bool pci_find_device(uint16_t vendor, uint16_t id, PciDevice* result) {
  if (result == nullptr) return false;
  for (uint16_t bus = 0; bus < 256; ++bus) {
    for (uint8_t slot = 0; slot < 32; ++slot) {
      PciDevice d{static_cast<uint8_t>(bus), slot, 0, 0, 0};
      const uint32_t first = pci_read32(d, 0);
      if ((first & 0xffff) == 0xffff) continue;
      const bool multifunction = (pci_read32(d, 0x0c) & 0x00800000U) != 0;
      for (uint8_t function = 0; function < (multifunction ? 8 : 1); ++function) {
        d.function = function;
        const uint32_t identity = function == 0 ? first : pci_read32(d, 0);
        d.vendor = static_cast<uint16_t>(identity);
        d.device = static_cast<uint16_t>(identity >> 16);
        if (d.vendor == vendor && d.device == id) { *result = d; return true; }
      }
    }
  }
  return false;
}
void pci_disable_msix(const PciDevice& d) {
  if ((pci_read16(d, 6) & 0x10) == 0) return;
  uint8_t offset = static_cast<uint8_t>(pci_read32(d, 0x34) & 0xfc);
  for (unsigned count = 0; offset >= 0x40 && count < 48; ++count) {
    const uint32_t capability = pci_read32(d, offset);
    if ((capability & 0xff) == 0x11) {
      const uint16_t control = pci_read16(d, static_cast<uint8_t>(offset + 2));
      pci_write16(d, static_cast<uint8_t>(offset + 2), static_cast<uint16_t>(control & ~0x8000));
      return;
    }
    const uint8_t next = static_cast<uint8_t>((capability >> 8) & 0xfc);
    if (next == offset) return;
    offset = next;
  }
}
