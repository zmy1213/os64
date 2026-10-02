#ifndef OS64_ELF_LOADER_HPP
#define OS64_ELF_LOADER_HPP

#include <stddef.h>
#include <stdint.h>

#include "fs/os64fs.hpp"
#include "memory/address_space.hpp"
#include "memory/page_allocator.hpp"

// 这一轮 ELF loader 先升级到：
// - 64 位 little-endian ELF
// - x86_64
// - ET_EXEC
// - 有界的多段用户程序（最多 1 MiB 段内存）
//
// 大小上限让错误 ELF 不能耗尽内核资源；文件上限同时受 OS64FS 格式约束。
// 装载流程是：
// 文件系统 -> ELF 头 -> 多段映射 -> ring3 入口
// 这条主链讲清楚。
constexpr uint32_t kElfLoaderMaxProgramHeaders = 8;
constexpr uint32_t kElfLoaderMaxLoadablePages = 256;
// 文件系统当前能保存的最大文件；装载缓冲区来自内核堆，不再挤进一页。
constexpr uint32_t kElfLoaderMaxFileBytes = 69632;
constexpr uint64_t kUserStackBytes = 16 * kPageSize;
constexpr uint64_t kUserStackBottom = kUserAddressSpaceDefaultStackTop - kUserStackBytes;
constexpr uint64_t kUserStackGuardBase = kUserStackBottom - kPageSize;

constexpr uint32_t kElfProgramTypeLoad = 1;
constexpr uint32_t kElfProgramFlagExecute = 0x1;
constexpr uint32_t kElfProgramFlagWrite = 0x2;
constexpr uint32_t kElfProgramFlagRead = 0x4;

struct __attribute__((packed)) Elf64FileHeader {
  uint8_t ident[16];                    // ELF 魔数、位数、大小端等“身份信息”都放在这里。
  uint16_t type;                        // 这是什么 ELF：可执行文件、重定位文件、共享库等。
  uint16_t machine;                     // 目标架构；这里我们只接受 x86_64。
  uint32_t version;                     // ELF 格式版本，当前通常就是 1。
  uint64_t entry;                       // 最后真正要把 RIP 送到哪一个虚拟地址开始执行。
  uint64_t program_header_offset;       // program header 表在整个文件里的起始偏移。
  uint64_t section_header_offset;       // section header 表偏移；这版 loader 实际不会用到它。
  uint32_t flags;                       // 架构相关标志；x86_64 通常保持 0。
  uint16_t header_size;                 // 整个 ELF 文件头自身大小。
  uint16_t program_header_entry_size;   // 每条 program header 记录占多少字节。
  uint16_t program_header_count;        // 一共有多少条 program header。
  uint16_t section_header_entry_size;   // 每条 section header 记录占多少字节。
  uint16_t section_header_count;        // 一共有多少条 section header。
  uint16_t section_header_string_index; // “节名字字符串表”在 section header 数组里的下标；当前 loader 不关心。
};

struct __attribute__((packed)) Elf64ProgramHeader {
  uint32_t type;              // 这条段记录是什么类型；最关键的是 PT_LOAD，表示要真的装进内存。
  uint32_t flags;             // 这段的权限位：可读/可写/可执行。
  uint64_t offset;            // 这段内容在 ELF 文件里的起始偏移。
  uint64_t virtual_address;   // 这段最终应该映射到用户虚拟地址空间的哪里。
  uint64_t physical_address;  // 在现代系统里通常没什么实际意义；这版教学 loader 也不依赖它。
  uint64_t file_size;         // 文件里真实存在多少字节需要拷进去。
  uint64_t memory_size;       // 运行时内存里这段总共应该占多少字节；比 file_size 大出的部分通常就是 BSS。
  uint64_t alignment;         // 这段期望的对齐要求；loader 会结合页对齐去处理。
};

static_assert(sizeof(Elf64FileHeader) == 64,
              "Elf64FileHeader layout must stay 64 bytes");
static_assert(sizeof(Elf64ProgramHeader) == 56,
              "Elf64ProgramHeader layout must stay 56 bytes");

struct LoadedUserElfProgram {
  uint32_t inode_number;              // 这份 ELF 文件在 OS64FS 里的 inode 编号。
  uint64_t file_size_bytes;           // 整个 ELF 文件一共有多少字节。
  uint64_t entry_point;               // 最后真的要把 RIP 送到哪里。
  uint64_t segment_virtual_address;   // 这里继续记第 1 个 PT_LOAD 段的起始虚拟地址，方便串口日志保持稳定。
  uint64_t segment_file_offset;       // 第 1 个 PT_LOAD 段在 ELF 文件里的起始偏移。
  uint64_t segment_file_size;         // 第 1 个 PT_LOAD 段里真实有多少字节要拷进去。
  uint64_t segment_memory_size;       // 第 1 个 PT_LOAD 段最终在内存里占多少字节（包含 BSS 零填充）。
  uint64_t first_page_physical;       // 整个程序第 1 张被映射成用户段的物理页地址，方便日志观察。
  uint32_t loadable_segment_count;    // 当前 ELF 里一共有多少个 PT_LOAD 段。
  uint32_t mapped_page_count;         // 所有 PT_LOAD 段合起来最终一共映射了多少张用户页。
  uint32_t segment_flags;             // 第 1 个 PT_LOAD 段的 PF_R / PF_W / PF_X。
  uint64_t image_end;                 // 所有段结束位置向上对齐；用户堆从这里开始。
};

// 把 OS64FS 里的一个 ELF 文件真正装进用户地址空间。
// 这一步会做几件关键事情：
// 1. 打开文件并读进内核堆缓冲区
// 2. 校验 ELF header / program header
// 3. 为每个 PT_LOAD 段分配并映射用户页
// 4. 把文件里的字节拷进映射后的用户页
// 5. 把 entry point 和段信息整理成 `LoadedUserElfProgram`
bool load_elf_user_program(PageAllocator* allocator,
                           AddressSpace* user_space,
                           const Os64Fs* filesystem,
                           const char* path,
                           LoadedUserElfProgram* out_program);

#endif
