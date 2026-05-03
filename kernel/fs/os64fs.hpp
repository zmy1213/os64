#ifndef OS64_OS64FS_HPP
#define OS64_OS64FS_HPP

#include <stddef.h>
#include <stdint.h>

#include "storage/block_device.hpp"

constexpr uint32_t kOs64FsVersion = 3;                      // 这一轮继续把磁盘格式升级到 v3，引入 inode/data 位图。
constexpr uint16_t kOs64FsTypeFile = 1;                     // 普通文件。
constexpr uint16_t kOs64FsTypeDirectory = 2;                // 目录。
constexpr uint32_t kOs64FsInvalidBlockIndex = 0xFFFFFFFFu;  // 直接块数组里用它表示“这个槽位还没用”。
constexpr size_t kOs64FsDirectBlockCount = 8;               // v2 先给每个 inode 8 个直接块，比 v1 的 4 个实用得多。
constexpr size_t kOs64FsDirectoryEntryNameCapacity = 56;    // 目录项名字容量从 24 提到 56，正常文件名不再那么容易撞墙。
constexpr uint32_t kOs64FsMaxInodeTableBytes = 8192;        // inode 表现在允许跨多个扇区，但仍然先缓存到一个固定上限里。
constexpr uint32_t kOs64FsMaxBitmapBytes = 4096;            // 先给 inode/data 位图各预留 4 KiB 缓存，足够覆盖当前教学镜像。
constexpr uint32_t kOs64FsMaxPathDepth = 16;                // 先继续保持固定路径栈深度，避免这一步又引入动态分配。

enum Os64FsMountError : uint32_t {
  kOs64FsMountOk = 0,                 // 没有错误，挂载成功。
  kOs64FsMountBadDevice = 1,          // 传进来的块设备本身就不可用。
  kOs64FsMountReadSuperblockFailed = 2,   // sector 0 没读出来，或者 superblock 拷贝失败。
  kOs64FsMountLayoutInvalid = 3,      // superblock 里的布局字段彼此对不上。
  kOs64FsMountReadInodeBitmapFailed = 4,  // inode 位图没读出来。
  kOs64FsMountReadDataBitmapFailed = 5,   // data 位图没读出来。
  kOs64FsMountReadInodeTableFailed = 6,   // inode 表没读出来。
  kOs64FsMountRootInodeInvalid = 7,   // 根 inode 不合法，或者不是目录。
  kOs64FsMountAllocationMismatch = 8, // 位图和 inode/data 实际占用情况对不上。
};

// 这个 superblock 就是文件系统的“总说明书”。
// 内核挂载时先读它，才知道 inode 表和数据区分别从哪里开始。
struct __attribute__((packed)) Os64FsSuperblock {
  char signature[8];                  // 固定写成 "OS64FSV3"，先确认读到的是我们自己的文件系统。
  uint32_t version;                   // 当前文件系统格式版本。
  uint32_t total_sectors;             // 整个文件系统镜像一共有多少个扇区。
  uint32_t inode_bitmap_start_sector; // inode 位图从哪个扇区开始；v3 开始不再靠“扫整张 inode 表猜谁被占用”。
  uint32_t inode_bitmap_sector_count; // inode 位图一共占多少个扇区。
  uint32_t data_bitmap_start_sector;  // data block 位图从哪个扇区开始。
  uint32_t data_bitmap_sector_count;  // data block 位图一共占多少个扇区。
  uint32_t inode_table_start_sector;  // inode 表从哪个扇区开始。
  uint32_t inode_table_sector_count;  // inode 表一共占多少个扇区；v2 不再限制成“只能 1 个扇区”。
  uint32_t inode_count;               // inode 表里一共有多少项。
  uint32_t inode_size;                // 每个 inode 占多少字节。
  uint32_t data_start_sector;         // 数据区从哪个扇区开始。
  uint32_t data_sector_count;         // 数据区一共有多少个扇区。
  uint32_t data_block_size;           // 逻辑数据块大小；v2 先回到更接近真实文件系统的 512 字节块。
  uint32_t root_inode;                // 根目录 inode 号。
  uint32_t directory_entry_size;      // 目录项大小也放进 superblock，避免内核把它写死成某个魔数。
  uint32_t free_inode_count;          // 当前剩余多少个可分配 inode；商业化布局里这是最基础的容量指标之一。
  uint32_t free_data_block_count;     // 当前还剩多少个空闲 data block。
  char volume_name[12];               // 卷名先压成 12 字节，整个 superblock 目前固定成 96 字节。
  uint32_t reserved0;                 // 先继续预留扩展位，后面可以接 journal / feature flags。
  uint32_t reserved1;                 // 再留一个槽位，避免下一次扩格式又得立刻打破布局。
};

// inode 描述的是“一个文件”或者“一个目录”。
// v2 继续保持只读，但把文件寻址能力升级成：
// - 8 个直接块
// - 1 个单级间接块
// 这样磁盘格式已经更接近真正会长期扩展的文件系统，而不是演示用玩具布局。
struct __attribute__((packed)) Os64FsInode {
  uint32_t inode_number;                                // inode 自己的编号，方便做一致性检查。
  uint16_t type;                                        // 1=file, 2=dir。
  uint16_t link_count;                                  // 先保留最基本的链接计数概念。
  uint32_t size_bytes;                                  // 文件内容或目录项区域一共有多少字节。
  uint32_t mode;                                        // 先把“权限/模式位”这个扩展位占上，哪怕现在只读还没真正使用。
  uint32_t direct_blocks[kOs64FsDirectBlockCount];      // 文件的前 8 个逻辑块直接记在 inode 里。
  uint32_t indirect_block;                              // 超过 8 块后，转去这个单级间接块里找更多块号。
  uint32_t block_count;                                 // 这份 inode 当前一共挂了多少个“数据块”，便于快速校验和 stat。
  uint32_t reserved0;                                   // 以后可以扩时间戳、uid/gid 等字段。
  uint32_t reserved1;                                   // 继续预留一个槽位，先把磁盘格式骨架搭大一点。
};

// 目录本质上就是“很多目录项顺序排在一起”的一个特殊文件。
struct __attribute__((packed)) Os64FsDirEntry {
  uint32_t inode_number;                               // 这个名字最后指向哪个 inode。
  uint16_t type;                                       // 这个名字对应的是 file 还是 dir。
  uint8_t name_length;                                 // 名字实际用了多少个字符。
  uint8_t flags;                                       // 先预留给目录项标志位，比如以后做隐藏/系统文件等。
  char name[kOs64FsDirectoryEntryNameCapacity];        // 名字容量直接扩到 56 字节。
};

static_assert(sizeof(Os64FsSuperblock) == 96,
              "Os64FsSuperblock layout must stay 96 bytes");
static_assert(sizeof(Os64FsInode) == 64,
              "Os64FsInode layout must stay 64 bytes");
static_assert(sizeof(Os64FsDirEntry) == 64,
              "Os64FsDirEntry layout must stay 64 bytes");

// 这是给 shell / 调试日志看的“文件系统容量快照”。
// 它不直接暴露磁盘位图细节，但会把最重要的已用/空闲统计透出去。
struct Os64FsStats {
  uint32_t total_inodes;          // inode 表总槽位数，含 0 号保留槽位。
  uint32_t allocatable_inodes;    // 真正允许文件/目录使用的 inode 数；当前等于 total_inodes - 1。
  uint32_t used_inodes;           // 已被 inode bitmap 标成占用、并且通过一致性校验的 inode 数。
  uint32_t free_inodes;           // 当前还能继续分配多少 inode。
  uint32_t total_data_blocks;     // 数据区一共有多少个逻辑 block。
  uint32_t used_data_blocks;      // 已被文件内容/目录内容/间接块占用的 block 数。
  uint32_t free_data_blocks;      // 当前空闲 block 数。
};

enum Os64FsValidationDebugCode : uint32_t {
  kOs64FsValidationDebugOk = 0,                 // 最近一次一致性校验没有发现问题。
  kOs64FsValidationDebugInode0Bitmap = 1,       // 0 号保留 inode 在位图里没有标成占用。
  kOs64FsValidationDebugInodeCacheRange = 2,    // 某个 inode 号映射不到缓存范围。
  kOs64FsValidationDebugFreedInodeNotZero = 3,  // 位图说“空闲”，但 inode 槽里还留着脏数据。
  kOs64FsValidationDebugAllocatedInodeBad = 4,  // 位图说“已分配”，但 inode 内容本身非法。
  kOs64FsValidationDebugDataBlockReference = 5, // inode 引用了非法块，或者两个 inode/块槽重复引用同一块。
  kOs64FsValidationDebugBitmapMismatch = 6,     // data bitmap 和 inode 实际引用图不一致。
  kOs64FsValidationDebugFreeCountMismatch = 7,  // superblock 里的空闲计数和实际扫描结果不一致。
};

struct Os64FsValidationDebug {
  uint32_t code;       // 最近一次校验失败类型；0 表示没有失败。
  uint32_t index;      // 出问题的 inode 号或 data block 号。
  uint32_t expected;   // 期望值，含义取决于 code。
  uint32_t actual;     // 实际值，含义取决于 code。
};

struct Os64Fs {
  BlockDevice* device;              // 当前挂载到哪个块设备上；v3 开始文件系统已经需要把 metadata 刷回去，所以这里不再只读。
  Os64FsSuperblock superblock;      // 把 superblock 缓存在内存里，后面不用每次再读 sector 0。
  uint32_t inode_bitmap_bytes;      // inode 位图真正缓存了多少字节。
  uint32_t data_bitmap_bytes;       // data 位图真正缓存了多少字节。
  uint8_t inode_bitmap_cache[kOs64FsMaxBitmapBytes];
                                    // v3 开始，inode 是否“已分配”先以位图为准。
  uint8_t data_bitmap_cache[kOs64FsMaxBitmapBytes];
                                    // data block 位图用来判断哪些块已经被文件系统占用。
  uint32_t inode_table_bytes;       // 当前这份 inode 表真正有多少字节被缓存下来了。
  uint8_t inode_table_cache[kOs64FsMaxInodeTableBytes];
                                    // inode 表现在允许跨多个扇区，只要总大小不超过这个缓存上限。
  Os64FsStats stats;                // 挂载成功后顺手缓存一份容量统计，shell `disk` 可以直接拿来展示。
  Os64FsValidationDebug validation_debug;
                                    // 最近一次 mount/validate 里第一处不一致的诊断信息，只存在内存里，不写入磁盘格式。
  uint32_t mount_error;             // 如果挂载失败，这里保留最后一次失败的阶段，方便定位是格式问题还是一致性问题。
  bool mounted;                     // 只有校验通过后，shell 和内核其它模块才允许访问它。
};

bool initialize_os64fs(Os64Fs* filesystem, BlockDevice* device);
bool os64fs_is_mounted(const Os64Fs* filesystem);
const Os64FsSuperblock* os64fs_superblock(const Os64Fs* filesystem);
const char* os64fs_inode_type_name(uint16_t type);
bool os64fs_query_stats(const Os64Fs* filesystem, Os64FsStats* out_stats);
bool os64fs_query_validation_debug(const Os64Fs* filesystem,
                                   Os64FsValidationDebug* out_debug);
uint32_t os64fs_mount_error(const Os64Fs* filesystem);
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
bool os64fs_create_file(Os64Fs* filesystem, const char* path);
bool os64fs_create_directory(Os64Fs* filesystem, const char* path);
bool os64fs_write_file(Os64Fs* filesystem, const char* path,
                       const void* buffer, size_t bytes_to_write);
bool os64fs_append_file(Os64Fs* filesystem, const char* path,
                        const void* buffer, size_t bytes_to_write);
bool os64fs_unlink(Os64Fs* filesystem, const char* path);
bool os64fs_sync(Os64Fs* filesystem);

#endif
