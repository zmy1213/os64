// Host regression: compile with os64fs.cpp, file.cpp, directory.cpp, vfs.cpp,
// block_device.cpp, boot_volume.cpp and runtime.cpp; pass boot_volume.bin.
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "fs/file.hpp"
#include "fs/vfs.hpp"

void* kmalloc(size_t size) { return std::malloc(size); }
bool kfree(void* pointer) { std::free(pointer); return true; }
struct HostDisk {
  std::vector<uint8_t> bytes;
  uint32_t writes = 0;
  uint32_t fail_write = 0;
  bool fail_flush = false;
  uint32_t fail_after_write = 0;
  mutable uint32_t reads = 0;
  uint32_t fail_read = 0;
};
static bool read_sector(const void* context, uint32_t sector, void* buffer, size_t size) {
  const auto& disk = *static_cast<const HostDisk*>(context);
  if (size < 512 || sector >= disk.bytes.size() / 512) return false;
  if (++disk.reads == disk.fail_read) return false;
  std::memcpy(buffer, disk.bytes.data() + sector * 512, 512);
  return true;
}
static bool write_sector(void* context, uint32_t sector, const void* buffer, size_t size) {
  auto& disk = *static_cast<HostDisk*>(context);
  if (size < 512 || sector >= disk.bytes.size() / 512) return false;
  ++disk.writes;
  if (disk.writes == disk.fail_write ||
      (disk.fail_after_write != 0 && disk.writes >= disk.fail_after_write)) {
    // Failure may be reported after part of a sector has changed.
    std::memcpy(disk.bytes.data() + sector * 512, buffer, 128);
    return false;
  }
  std::memcpy(disk.bytes.data() + sector * 512, buffer, 512);
  return true;
}
static bool flush(void* context) {
  auto& disk = *static_cast<HostDisk*>(context);
  if (disk.fail_flush) { disk.fail_flush = false; return false; }
  return true;
}
static BlockDevice device_for(HostDisk* disk) {
  return {disk, 0, static_cast<uint32_t>(disk->bytes.size() / 512), 512, true,
          read_sector, write_sector, flush};
}
static void require(bool value, const char* message) {
  if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static void check_remount(BlockDevice* device) {
  Os64Fs filesystem;
  require(initialize_os64fs(&filesystem, device), "remount allocation maps");
}
static std::string read_file(Os64Fs* filesystem, const char* path) {
  FileHandle handle;
  require(file_open(filesystem, path, &handle), "open file");
  std::string text(handle.inode.size_bytes, '\0');
  if (!text.empty()) require(file_read(&handle, text.data(), text.size()) == text.size(), "read file");
  require(file_close(&handle), "close file");
  return text;
}

int main(int argc, char** argv) {
  require(argc == 2, "pass boot_volume.bin");
  std::ifstream input(argv[1], std::ios::binary);
  require(input.good(), "open fixture");
  HostDisk disk{{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()}};
  const auto fixture = disk.bytes;
  BlockDevice device = device_for(&disk);
  Os64Fs filesystem;
  require(initialize_os64fs(&filesystem, &device), "mount fixture");
  const auto original = disk.bytes;
  require(!os64fs_write_file(&filesystem, "/readme.txt", nullptr, 7), "reject null buffer");
  std::string too_big(69633, 'B');
  require(!os64fs_write_file(&filesystem, "/readme.txt", too_big.data(), too_big.size()), "reject file size overflow");
  require(!os64fs_write_file(&filesystem, "/overflow.txt", too_big.data(), too_big.size()), "reject oversized new file");
  require(!os64fs_append_file(&filesystem, "/readme.txt", too_big.data(), SIZE_MAX), "reject size_t overflow");
  require(disk.bytes == original, "invalid writes leave original disk unchanged");
  check_remount(&device);

  Os64FsStats stats;
  require(os64fs_query_stats(&filesystem, &stats), "query capacity");
  std::string full_disk((stats.free_data_blocks + 1) * 512, 'F');
  require(!os64fs_write_file(&filesystem, "/full.txt", full_disk.data(), full_disk.size()), "ENOSPC new file");
  require(!os64fs_write_file(&filesystem, "/readme.txt", full_disk.data(), full_disk.size()), "ENOSPC replacement");
  require(disk.bytes == original, "ENOSPC leaves all sectors unchanged");
  check_remount(&device);

  require(os64fs_create_directory(&filesystem, "/persist"), "create directory");
  std::string indirect(10000, 'I');
  require(os64fs_write_file(&filesystem, "/persist/file", indirect.data(), indirect.size()), "write indirect blocks");
  require(read_file(&filesystem, "/persist/file") == indirect, "read indirect blocks");
  check_remount(&device);
  FileHandle handle;
  require(file_open(&filesystem, "/persist/file", &handle), "open before replacement");
  const std::string replacement(5300, 'R');
  require(os64fs_write_file(&filesystem, "/persist/file", replacement.data(), replacement.size()), "replace open file");
  FileStat stat;
  require(file_handle_stat(&handle, &stat) && stat.size_bytes == replacement.size(), "open stat refresh");
  std::string refreshed(replacement.size(), '\0');
  require(file_read(&handle, refreshed.data(), refreshed.size()) == refreshed.size() && refreshed == replacement, "open read refresh");
  const char suffix[] = "tail";
  require(os64fs_append_file(&filesystem, "/persist/file", suffix, 4), "append");
  require(file_seek(&handle, replacement.size() + 4), "open seek refresh");
  require(file_close(&handle), "close refreshed handle");
  require(!os64fs_unlink(&filesystem, "/persist"), "reject nonempty directory removal");
  require(os64fs_unlink(&filesystem, "/persist/file"), "unlink indirect file");
  require(os64fs_unlink(&filesystem, "/persist"), "unlink empty directory");
  check_remount(&device);

  require(os64fs_query_stats(&filesystem, &stats), "query post-delete capacity");
  for (int i = 0; i < 12; ++i) {
    require(os64fs_write_file(&filesystem, "/cycle", indirect.data(), indirect.size()), "cycle write");
    require(os64fs_unlink(&filesystem, "/cycle"), "cycle unlink");
  }
  Os64FsStats after;
  require(os64fs_query_stats(&filesystem, &after) &&
          after.free_inodes == stats.free_inodes && after.free_data_blocks == stats.free_data_blocks,
          "no inode or indirect block leaks");
  for (int i = 0; i < 200; ++i) {
    const std::string path = "/inode-" + std::to_string(i);
    const auto before = disk.bytes;
    if (!os64fs_create_file(&filesystem, path.c_str())) {
      require(disk.bytes == before, "inode exhaustion leaves disk unchanged");
      require(os64fs_query_stats(&filesystem, &after) && after.free_inodes == 0, "inode exhaustion reached");
      break;
    }
  }
  check_remount(&device);

  // Fail each position in the commit, including a partially written sector.
  for (uint32_t fault = 1; fault <= 40; ++fault) {
    disk = HostDisk{fixture};
    device = device_for(&disk);
    require(initialize_os64fs(&filesystem, &device), "mount fault fixture");
    disk.fail_write = fault;
    const bool written = os64fs_write_file(&filesystem, "/fault", indirect.data(), indirect.size());
    if (!written) require(disk.bytes == fixture, "I/O error restores every attempted sector");
    check_remount(&device);
  }
  disk = HostDisk{fixture};
  device = device_for(&disk);
  require(initialize_os64fs(&filesystem, &device), "mount flush fixture");
  disk.fail_flush = true;
  require(!os64fs_write_file(&filesystem, "/flush-fault", indirect.data(), indirect.size()), "flush error reported");
  require(disk.bytes == fixture, "flush error rollback");
  check_remount(&device);

  disk = HostDisk{fixture};
  auto* larger_sb = reinterpret_cast<Os64FsSuperblock*>(disk.bytes.data());
  const uint32_t extra_sectors = 1024 - larger_sb->total_sectors;
  larger_sb->total_sectors += extra_sectors;
  larger_sb->data_sector_count += extra_sectors;
  larger_sb->free_data_block_count += extra_sectors;
  disk.bytes.resize(1024 * 512);
  device = device_for(&disk);
  require(initialize_os64fs(&filesystem, &device), "mount larger file-boundary fixture");
  std::string maximum_file(69632, 'M');
  require(os64fs_write_file(&filesystem, "/maximum", maximum_file.data(), maximum_file.size()), "maximum file write");
  require(read_file(&filesystem, "/maximum") == maximum_file, "maximum direct/indirect capacity");
  const auto maximum_disk = disk.bytes;
  require(!os64fs_append_file(&filesystem, "/maximum", suffix, 1), "maximum file append rejected");
  require(disk.bytes == maximum_disk, "maximum append leaves disk unchanged");
  require(os64fs_unlink(&filesystem, "/maximum"), "maximum file reclaimed");
  check_remount(&device);

  disk = HostDisk{fixture};
  device = device_for(&disk);
  require(initialize_os64fs(&filesystem, &device), "mount path-depth fixture");
  std::string nested;
  for (uint32_t depth = 2; depth <= 16; ++depth) {
    nested += "/d";
    require(os64fs_create_directory(&filesystem, nested.c_str()), "maximum valid path depth");
  }
  const auto depth_disk = disk.bytes;
  nested += "/d";
  require(!os64fs_create_directory(&filesystem, nested.c_str()), "excessive path depth rejected");
  require(disk.bytes == depth_disk, "path-depth failure rollback");
  check_remount(&device);

  for (uint32_t fault = 1; fault <= 80; ++fault) {
    disk = HostDisk{fixture};
    device = device_for(&disk);
    require(initialize_os64fs(&filesystem, &device), "mount read fault fixture");
    disk.reads = 0;
    disk.fail_read = fault;
    const bool written = os64fs_write_file(&filesystem, "/read-fault", indirect.data(), indirect.size());
    if (!written) require(disk.bytes == fixture, "read failure leaves disk unchanged");
    disk.fail_read = 0;
    check_remount(&device);
  }
  disk = HostDisk{fixture};
  device = device_for(&disk);
  require(initialize_os64fs(&filesystem, &device), "mount permanent fault fixture");
  require(file_open(&filesystem, "/readme.txt", &handle), "open before permanent device error");
  VfsMount mount;
  VfsFile vfs_file;
  VfsDirectory vfs_directory;
  require(initialize_vfs(&mount, &filesystem) &&
          vfs_open_file(&mount, "/readme.txt", &vfs_file) &&
          vfs_open_directory(&mount, "/", &vfs_directory), "open VFS before permanent device error");
  disk.fail_after_write = 1;
  require(!os64fs_write_file(&filesystem, "/permanent-fault", indirect.data(), indirect.size()), "permanent error returned");
  require(!os64fs_is_mounted(&filesystem), "failed rollback invalidates mount");
  require(file_close(&handle), "unmounted file handle can close");
  require(vfs_close_file(&vfs_file) && vfs_close_directory(&vfs_directory),
          "unmounted VFS handles can close");

  for (uint32_t corruption = 0; corruption < 4; ++corruption) {
    disk = HostDisk{fixture};
    auto* sb = reinterpret_cast<Os64FsSuperblock*>(disk.bytes.data());
    auto* root = reinterpret_cast<Os64FsInode*>(
        disk.bytes.data() + sb->inode_table_start_sector * 512 + sb->root_inode * 64);
    auto* entries = reinterpret_cast<Os64FsDirEntry*>(
        disk.bytes.data() + (sb->data_start_sector + root->direct_blocks[0]) * 512);
    switch (corruption) {
      case 0: entries[0].inode_number = 0; break;
      case 1: entries[0].type = kOs64FsTypeDirectory; break;
      case 2: entries[0].name[0] = '/'; break;
      case 3: --root->size_bytes; break;
    }
    device = device_for(&disk);
    require(!initialize_os64fs(&filesystem, &device), "reject corrupt directory reference/layout");
  }

  for (uint32_t value : {0xffffffffu, 0x80000000u}) {
    disk = HostDisk{fixture};
    auto* sb = reinterpret_cast<Os64FsSuperblock*>(disk.bytes.data());
    sb->inode_count = value;
    device = device_for(&disk);
    require(!initialize_os64fs(&filesystem, &device), "reject overflowing inode count");
    disk = HostDisk{fixture};
    sb = reinterpret_cast<Os64FsSuperblock*>(disk.bytes.data());
    sb->data_sector_count = value;
    device = device_for(&disk);
    require(!initialize_os64fs(&filesystem, &device), "reject overflowing sector extent");
  }
  std::cout << "storage host regression passed: remount, ENOSPC, indirect reuse, stale handles, I/O rollback, flush, layout bounds\n";
}
