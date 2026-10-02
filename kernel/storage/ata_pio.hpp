#ifndef OS64_ATA_PIO_HPP
#define OS64_ATA_PIO_HPP

#include "storage/block_device.hpp"

// Only the dedicated primary-master IDE data disk is probed. The boot image
// remains a floppy; this driver never scans other disks or formats a device.
struct AtaPioDevice {
  uint32_t sector_count;
  uint8_t last_status;
  uint8_t last_error;
  bool ready;
};

bool initialize_ata_pio_primary_master(AtaPioDevice* disk);
bool initialize_block_device_from_ata_pio(BlockDevice* device, AtaPioDevice* disk);
bool ata_pio_read_sector(AtaPioDevice* disk, uint32_t lba, void* buffer,
                         size_t buffer_size);
bool ata_pio_write_sector(AtaPioDevice* disk, uint32_t lba, const void* buffer,
                          size_t buffer_size);
bool ata_pio_flush(AtaPioDevice* disk);

#endif
