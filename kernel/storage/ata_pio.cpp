#include "storage/ata_pio.hpp"

namespace {
constexpr uint16_t kData = 0x1f0;
constexpr uint16_t kError = 0x1f1;
constexpr uint16_t kCount = 0x1f2;
constexpr uint16_t kLbaLow = 0x1f3;
constexpr uint16_t kLbaMid = 0x1f4;
constexpr uint16_t kLbaHigh = 0x1f5;
constexpr uint16_t kDrive = 0x1f6;
constexpr uint16_t kStatus = 0x1f7;
constexpr uint16_t kControl = 0x3f6;
constexpr uint32_t kPollLimit = 1000000;
constexpr uint32_t kLba28Sectors = 0x10000000;
constexpr uint8_t kBusy = 0x80;
constexpr uint8_t kDataRequest = 0x08;
constexpr uint8_t kFault = 0x20;
constexpr uint8_t kErrorBit = 0x01;

inline uint8_t in8(uint16_t port) {
  uint8_t value;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}
inline void out8(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
inline uint16_t in16(uint16_t port) {
  uint16_t value;
  asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}
inline void out16(uint16_t port, uint16_t value) {
  asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
void settle() {
  // Four alternate-status reads provide the ATA selection/command delay.
  for (uint32_t i = 0; i < 4; ++i) (void)in8(kControl);
}
bool wait_status(AtaPioDevice* disk, bool data_request, bool check_error = true) {
  for (uint32_t i = 0; i < kPollLimit; ++i) {
    const uint8_t status = in8(kStatus);
    disk->last_status = status;
    if (status == 0 || status == 0xff) return false;
    if (status & kBusy) continue;
    if (check_error && (status & (kFault | kErrorBit))) {
      disk->last_error = in8(kError);
      return false;
    }
    if (data_request ? ((status & kDataRequest) != 0)
                     : ((status & kDataRequest) == 0)) return true;
  }
  return false;
}
bool issue(AtaPioDevice* disk, uint32_t lba, uint8_t command) {
  if (disk == nullptr || !disk->ready || lba >= disk->sector_count ||
      lba >= kLba28Sectors) return false;
  out8(kDrive, static_cast<uint8_t>(0xe0 | ((lba >> 24) & 0x0f)));
  settle();
  // A previous command's ERR must not prevent issuing the next command.
  if (!wait_status(disk, false, false)) return false;
  out8(kCount, 1);
  out8(kLbaLow, static_cast<uint8_t>(lba));
  out8(kLbaMid, static_cast<uint8_t>(lba >> 8));
  out8(kLbaHigh, static_cast<uint8_t>(lba >> 16));
  out8(kStatus, command);
  settle();
  return wait_status(disk, true);
}
bool read_adapter(const void* context, uint32_t sector, void* buffer, size_t size) {
  return ata_pio_read_sector(const_cast<AtaPioDevice*>(
      static_cast<const AtaPioDevice*>(context)), sector, buffer, size);
}
bool write_adapter(void* context, uint32_t sector, const void* buffer, size_t size) {
  return ata_pio_write_sector(static_cast<AtaPioDevice*>(context), sector, buffer, size);
}
bool flush_adapter(void* context) {
  return ata_pio_flush(static_cast<AtaPioDevice*>(context));
}
}  // namespace

bool initialize_ata_pio_primary_master(AtaPioDevice* disk) {
  if (disk == nullptr) return false;
  *disk = {};
  out8(kControl, 0x02);  // nIEN: synchronous PIO needs no IDE IRQ handler.
  out8(kDrive, 0xa0);
  settle();
  if (!wait_status(disk, false, false)) return false;
  out8(kCount, 0);
  out8(kLbaLow, 0);
  out8(kLbaMid, 0);
  out8(kLbaHigh, 0);
  out8(kStatus, 0xec);  // IDENTIFY DEVICE.
  settle();
  if (!wait_status(disk, true) || in8(kLbaMid) != 0 || in8(kLbaHigh) != 0) return false;
  uint16_t identify[256];
  for (uint32_t i = 0; i < 256; ++i) identify[i] = in16(kData);
  settle();
  if (!wait_status(disk, false) || (identify[0] & 0x8000) ||
      (identify[49] & 0x0200) == 0) return false;
  // Word 106 is meaningful only with validity bits 01. Reject 4 KiB logical
  // sectors instead of transferring 512 bytes and silently corrupting them.
  if ((identify[106] & 0xc000) == 0x4000 && (identify[106] & 0x1000)) {
    const uint32_t words = static_cast<uint32_t>(identify[117]) |
                           (static_cast<uint32_t>(identify[118]) << 16);
    if (words != 256) return false;
  }
  disk->sector_count = static_cast<uint32_t>(identify[60]) |
                       (static_cast<uint32_t>(identify[61]) << 16);
  if (disk->sector_count == 0) return false;
  if (disk->sector_count > kLba28Sectors) disk->sector_count = kLba28Sectors;
  disk->ready = true;
  return true;
}

bool initialize_block_device_from_ata_pio(BlockDevice* device, AtaPioDevice* disk) {
  if (device == nullptr) return false;
  *device = {};
  if (disk == nullptr || !disk->ready || disk->sector_count == 0) return false;
  device->context = disk;
  device->sector_count = disk->sector_count;
  device->sector_size = 512;
  device->read_sector = read_adapter;
  device->write_sector = write_adapter;
  device->flush = flush_adapter;
  device->ready = true;
  return true;
}

bool ata_pio_read_sector(AtaPioDevice* disk, uint32_t lba, void* buffer, size_t size) {
  if (buffer == nullptr || size < 512 || !issue(disk, lba, 0x20)) return false;
  auto* bytes = static_cast<uint8_t*>(buffer);
  for (uint32_t i = 0; i < 256; ++i) {
    const uint16_t word = in16(kData);
    bytes[i * 2] = static_cast<uint8_t>(word);
    bytes[i * 2 + 1] = static_cast<uint8_t>(word >> 8);
  }
  settle();
  return wait_status(disk, false);
}

bool ata_pio_write_sector(AtaPioDevice* disk, uint32_t lba, const void* buffer, size_t size) {
  if (buffer == nullptr || size < 512 || !issue(disk, lba, 0x30)) return false;
  const auto* bytes = static_cast<const uint8_t*>(buffer);
  for (uint32_t i = 0; i < 256; ++i) {
    out16(kData, static_cast<uint16_t>(bytes[i * 2]) |
                  (static_cast<uint16_t>(bytes[i * 2 + 1]) << 8));
  }
  settle();
  return wait_status(disk, false);
}

bool ata_pio_flush(AtaPioDevice* disk) {
  if (disk == nullptr || !disk->ready) return false;
  out8(kDrive, 0xe0);
  settle();
  if (!wait_status(disk, false, false)) return false;
  out8(kStatus, 0xe7);  // FLUSH CACHE: successful fs writes survive a reboot.
  settle();
  return wait_status(disk, false);
}
