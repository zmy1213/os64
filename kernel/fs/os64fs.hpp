#ifndef OS64_OS64FS_HPP
#define OS64_OS64FS_HPP

#include <stddef.h>
#include <stdint.h>

#include "storage/block_device.hpp"

constexpr uint32_t kOs64FsVersion = 1;                      // 第一版只做只读挂载，所以先把版本固定成 1。
constexpr uint16_t kOs64FsTypeFile = 1;                     // 普通文件。
constexpr uint16_t kOs64FsTypeDirectory = 2;                // 目录。
constexpr uint32_t kOs64FsInvalidBlockIndex = 0xFFFFFFFFu;  // 直接块数组里用它表示“这个槽位还没用”。

// 这个 superblock 就是文件系统的“总说明书”。
// 内核挂载时先读它，才知道 inode 表和数据区分别从哪里开始。
struct __attribute__((packed)) Os64FsSuperblock {
  char signature[8];                  // 固定写成 "OS64FSV1"，先确认读到的是我们自己的文件系统。
  uint32_t version;                   // 当前文件系统格式版本。
  uint32_t total_sectors;             // 整个文件系统镜像一共有多少个扇区。
  uint32_t inode_table_start_sector;  // inode 表从哪个扇区开始。
  uint32_t inode_count;               // inode 表里一共有多少项。
  uint32_t inode_size;                // 每个 inode 占多少字节。
  uint32_t data_start_sector;         // 数据区从哪个扇区开始。
  uint32_t data_sector_count;         // 数据区一共有多少个扇区。
  uint32_t data_block_size;           // 逻辑数据块大小，这一轮用 128 字节，方便小文件复用一个扇区。
  uint32_t root_inode;                // 根目录 inode 号。
  char volume_name[16];               // 给这卷文件系统起个名字，shell 和日志里更容易看。
  uint8_t reserved[4];                // 先预留给后面扩格式。
};

// inode 描述的是“一个文件”或者“一个目录”。
// 第一版先只支持最简单的 direct blocks，不做间接块。
struct __attribute__((packed)) Os64FsInode {
  uint32_t inode_number;       // inode 自己的编号，方便做一致性检查。
  uint16_t type;               // 1=file, 2=dir。
  uint16_t link_count;         // 先保留最基本的链接计数概念。
  uint32_t size_bytes;         // 文件内容或目录项区域一共有多少字节。
  uint32_t direct_blocks[4];   // 最多先直接指向 4 个逻辑数据块。
  uint32_t reserved;           // 以后可以扩 flags / timestamps 之类的字段。
};

// 目录本质上就是“很多目录项顺序排在一起”的一个特殊文件。
struct __attribute__((packed)) Os64FsDirEntry {
  uint32_t inode_number;       // 这个名字最后指向哪个 inode。
  uint16_t type;               // 这个名字对应的是 file 还是 dir。
  uint8_t name_length;         // 名字实际用了多少个字符。
  uint8_t reserved;            // 先占位，保持结构体对齐简单。
  char name[24];               // 第一版先把单个名字限制在 24 字节内。
};

static_assert(sizeof(Os64FsSuperblock) == 64,
              "Os64FsSuperblock layout must stay 64 bytes");
static_assert(sizeof(Os64FsInode) == 32,
              "Os64FsInode layout must stay 32 bytes");
static_assert(sizeof(Os64FsDirEntry) == 32,
              "Os64FsDirEntry layout must stay 32 bytes");

struct Os64Fs {
  const BlockDevice* device;        // 当前挂载到哪个块设备上。
  Os64FsSuperblock superblock;      // 把 superblock 缓存在内存里，后面不用每次再读 sector 0。
  uint8_t inode_table_cache[512];   // v1 先把整个 inode 表限制在一个扇区内，逻辑更容易跟。
  bool mounted;                     // 只有校验通过后，shell 和内核其它模块才允许访问它。
};

bool initialize_os64fs(Os64Fs* filesystem, const BlockDevice* device);
bool os64fs_is_mounted(const Os64Fs* filesystem);
const Os64FsSuperblock* os64fs_superblock(const Os64Fs* filesystem);
const char* os64fs_inode_type_name(uint16_t type);
bool os64fs_read_inode(const Os64Fs* filesystem, uint32_t inode_number,
                       Os64FsInode* out_inode);
bool os64fs_lookup_path(const Os64Fs* filesystem, const char* path,
                        Os64FsInode* out_inode);
uint32_t os64fs_directory_entry_count(const Os64Fs* filesystem,
                                      const Os64FsInode* directory_inode);
bool os64fs_read_directory_entry(const Os64Fs* filesystem,
                                 const Os64FsInode* directory_inode,
                                 uint32_t entry_index,
                                 Os64FsDirEntry* out_entry);
bool os64fs_read_inode_data(const Os64Fs* filesystem,
                            const Os64FsInode* inode,
                            uint32_t offset,
                            void* buffer,
                            size_t bytes_to_read);

#endif
