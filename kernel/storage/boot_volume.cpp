#include "storage/boot_volume.hpp"

#include "runtime/runtime.hpp"

bool initialize_boot_volume(BootVolume* volume, const BootInfo* boot_info) {
  if (volume == nullptr || boot_info == nullptr) {
    return false;
  }

  volume->base = nullptr;
  volume->start_lba = 0;
  volume->sector_count = 0;
  volume->sector_size = 0;
  volume->ready = false;

  if (boot_info->boot_volume_ptr == 0 ||
      boot_info->boot_volume_sector_count == 0 ||
      boot_info->boot_volume_sector_size != kBootVolumeSectorSize) {
    return false;
  }

  volume->base = reinterpret_cast<const uint8_t*>(
      static_cast<uintptr_t>(boot_info->boot_volume_ptr));
  volume->start_lba = boot_info->boot_volume_start_lba;
  volume->sector_count = boot_info->boot_volume_sector_count;
  volume->sector_size = boot_info->boot_volume_sector_size;
  volume->ready = true;
  return true;
}

bool boot_volume_is_ready(const BootVolume* volume) {
  return volume != nullptr && volume->ready;
}

uint64_t boot_volume_total_bytes(const BootVolume* volume) {
  if (!boot_volume_is_ready(volume)) {
    return 0;
  }

  return static_cast<uint64_t>(volume->sector_count) * volume->sector_size;
}

bool boot_volume_read_sector(const BootVolume* volume, uint32_t sector_index,
                             void* buffer, size_t buffer_size) {
  if (!boot_volume_is_ready(volume) || buffer == nullptr ||
      buffer_size < volume->sector_size ||
      sector_index >= volume->sector_count) {
    return false;
  }

  const uint32_t byte_offset = sector_index * volume->sector_size;
  memory_copy(buffer, volume->base + byte_offset, volume->sector_size);
  return true;
}
