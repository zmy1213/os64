#ifndef OS64_PCI_HPP
#define OS64_PCI_HPP
#include <stdint.h>

struct PciDevice {
  uint8_t bus;
  uint8_t slot;
  uint8_t function;
  uint16_t vendor;
  uint16_t device;
};
// 教程采用 x86 传统配置空间访问（0xcf8 / 0xcfc），尚不支持 PCIe ECAM。
uint32_t pci_read32(const PciDevice& device, uint8_t offset);
uint16_t pci_read16(const PciDevice& device, uint8_t offset);
void pci_write16(const PciDevice& device, uint8_t offset, uint16_t value);
bool pci_find_device(uint16_t vendor, uint16_t device, PciDevice* result);
void pci_disable_msix(const PciDevice& device);
#endif
