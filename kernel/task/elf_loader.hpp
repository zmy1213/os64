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
// - 很小的多段用户程序
//
// 这里先把它限制成“最多几个 PT_LOAD 段、最多几十页”，
// 目的是先把：
// 文件系统 -> ELF 头 -> 多段映射 -> ring3 入口
// 这条主链讲清楚。
constexpr uint32_t kElfLoaderMaxProgramHeaders = 8;
constexpr uint32_t kElfLoaderMaxLoadablePages = 32;

constexpr uint32_t kElfProgramTypeLoad = 1;
constexpr uint32_t kElfProgramFlagExecute = 0x1;
constexpr uint32_t kElfProgramFlagWrite = 0x2;
constexpr uint32_t kElfProgramFlagRead = 0x4;

struct __attribute__((packed)) Elf64FileHeader {
  uint8_t ident[16];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint64_t entry;
  uint64_t program_header_offset;
  uint64_t section_header_offset;
  uint32_t flags;
  uint16_t header_size;
  uint16_t program_header_entry_size;
  uint16_t program_header_count;
  uint16_t section_header_entry_size;
  uint16_t section_header_count;
  uint16_t section_header_string_index;
};

struct __attribute__((packed)) Elf64ProgramHeader {
  uint32_t type;
  uint32_t flags;
  uint64_t offset;
  uint64_t virtual_address;
  uint64_t physical_address;
  uint64_t file_size;
  uint64_t memory_size;
  uint64_t alignment;
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
};

bool load_elf_user_program(PageAllocator* allocator,
                           AddressSpace* user_space,
                           const Os64Fs* filesystem,
                           const char* path,
                           LoadedUserElfProgram* out_program);

#endif
