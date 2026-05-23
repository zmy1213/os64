#include "task/elf_loader.hpp"

#include "fs/file.hpp"
#include "memory/paging.hpp"
#include "runtime/runtime.hpp"

namespace {

constexpr uint8_t kElfMagic0 = 0x7F;
constexpr uint8_t kElfMagic1 = 'E';
constexpr uint8_t kElfMagic2 = 'L';
constexpr uint8_t kElfMagic3 = 'F';
constexpr uint8_t kElfClass64 = 2;
constexpr uint8_t kElfDataLittleEndian = 1;
constexpr uint8_t kElfCurrentVersion = 1;
constexpr uint16_t kElfTypeExecutable = 2;
constexpr uint16_t kElfMachineX86_64 = 0x3E;

// ELF loader 里的对齐 helper 本质上都在服务“按页映射”：
// ELF 段的起止位置不一定页对齐，但页表映射一定是按 4 KiB 页做的。
uint64_t align_down(uint64_t value, uint64_t alignment) {
  if (alignment == 0) {
    return value;
  }

  return value & ~(alignment - 1);
}

uint64_t align_up(uint64_t value, uint64_t alignment) {
  if (alignment == 0) {
    return value;
  }

  return (value + alignment - 1) & ~(alignment - 1);
}

bool page_is_directly_accessible_in_boot_identity_map(uint64_t physical_address) {
  // 当前教学内核还依赖“低 2 MiB 恒等映射”这层早期假设：
  // 只有落在这段范围里的物理页，内核此刻才能直接把“物理地址当指针”去访问它。
  return physical_address != 0 && physical_address < kPagingBootIdentityLimit;
}

bool elf_header_is_valid(const Elf64FileHeader* header, size_t file_size_bytes) {
  // 先校验“这是不是我们当前愿意支持的 ELF”：
  // - 64 位
  // - little-endian
  // - x86_64
  // - executable
  // - program header 数量在当前教学实现能承受的范围内
  if (header == nullptr || file_size_bytes < sizeof(Elf64FileHeader)) {
    return false;
  }

  if (header->ident[0] != kElfMagic0 ||
      header->ident[1] != kElfMagic1 ||
      header->ident[2] != kElfMagic2 ||
      header->ident[3] != kElfMagic3 ||
      header->ident[4] != kElfClass64 ||
      header->ident[5] != kElfDataLittleEndian ||
      header->ident[6] != kElfCurrentVersion) {
    return false;
  }

  if (header->type != kElfTypeExecutable ||
      header->machine != kElfMachineX86_64 ||
      header->version != kElfCurrentVersion ||
      header->header_size != sizeof(Elf64FileHeader) ||
      header->program_header_entry_size != sizeof(Elf64ProgramHeader) ||
      header->program_header_count == 0 ||
      header->program_header_count > kElfLoaderMaxProgramHeaders) {
    return false;
  }

  if (header->program_header_offset > file_size_bytes) {
    return false;
  }

  const size_t total_program_header_bytes =
      static_cast<size_t>(header->program_header_count) *
      sizeof(Elf64ProgramHeader);
  return total_program_header_bytes <=
         (file_size_bytes - static_cast<size_t>(header->program_header_offset));
}

bool loadable_segment_is_valid(const AddressSpace* user_space,
                               const Elf64ProgramHeader* segment,
                               size_t file_size_bytes) {
  // 这里只接受真正会映射进内存的 PT_LOAD 段，
  // 并且要求：
  // - memory_size >= file_size
  // - 文件偏移不越界
  // - 目标虚拟地址区间完全落在用户地址空间窗口内
  if (user_space == nullptr || !user_space->ready || segment == nullptr) {
    return false;
  }

  if (segment->type != kElfProgramTypeLoad ||
      segment->memory_size == 0 ||
      segment->memory_size < segment->file_size ||
      segment->offset > file_size_bytes ||
      segment->file_size > (file_size_bytes - segment->offset)) {
    return false;
  }

  const uint64_t segment_end =
      segment->virtual_address + segment->memory_size;
  if (segment_end < segment->virtual_address ||
      segment->virtual_address < user_space->user_region_base ||
      segment_end > user_space->user_region_limit) {
    return false;
  }

  const uint64_t load_base =
      align_down(segment->virtual_address, kPagingPageSize);
  const uint64_t load_limit =
      align_up(segment_end, kPagingPageSize);
  if (load_limit < load_base) {
    return false;
  }

  const uint64_t page_count =
      (load_limit - load_base) / kPagingPageSize;
  return page_count > 0 && page_count <= kElfLoaderMaxLoadablePages;
}

bool entry_belongs_to_any_loadable_segment(
    const Elf64ProgramHeader* program_headers,
    uint16_t program_header_count,
    uint64_t entry_point) {
  // 最终 RIP 不能随便指到 ELF 文件里任意地方，
  // 至少要落在某个真正可加载的 PT_LOAD 段范围内。
  if (program_headers == nullptr || program_header_count == 0) {
    return false;
  }

  for (uint16_t i = 0; i < program_header_count; ++i) {
    const Elf64ProgramHeader& segment = program_headers[i];
    if (segment.type != kElfProgramTypeLoad || segment.memory_size == 0) {
      continue;
    }

    const uint64_t segment_end =
        segment.virtual_address + segment.memory_size;
    if (entry_point >= segment.virtual_address &&
        entry_point < segment_end) {
      return true;
    }
  }

  return false;
}

}  // namespace

bool load_elf_user_program(PageAllocator* allocator,
                           AddressSpace* user_space,
                           const Os64Fs* filesystem,
                           const char* path,
                           LoadedUserElfProgram* out_program) {
  if (allocator == nullptr ||
      user_space == nullptr ||
      filesystem == nullptr ||
      path == nullptr ||
      out_program == nullptr) {
    return false;
  }

  memory_set(out_program, 0, sizeof(*out_program));

  FileHandle file_handle;
  memory_set(&file_handle, 0, sizeof(file_handle));

  // 整个 ELF 装载主线可以先记成 6 步：
  // 1. 从文件系统把 ELF 文件读出来
  // 2. 校验 ELF header / program header
  // 3. 统计有哪些 PT_LOAD 段要真正映射
  // 4. 给每个段覆盖到的页分配物理页并挂进用户地址空间
  // 5. 把文件里的真实字节拷进这些用户页
  // 6. 把 entry point、页数、首段信息这些结果回填给调用方
  // 先通过文件层把 ELF 文件整体读出来。
  if (!file_open(filesystem, path, &file_handle)) {
    return false;
  }

  FileStat file_stat_result;
  if (!file_handle_stat(&file_handle, &file_stat_result) ||
      file_stat_result.size_bytes == 0 ||
      file_stat_result.size_bytes > kPagingPageSize) {
    (void)file_close(&file_handle);
    return false;
  }

  const uint64_t staging_physical_page = alloc_page(allocator);
  if (!page_is_directly_accessible_in_boot_identity_map(staging_physical_page)) {
    (void)file_close(&file_handle);
    return false;
  }

  // 当前教学版本先把整个 ELF 文件读进 1 张 staging page，
  // 这样后面解析 header 和按字节拷段内容都很直观。
  auto* const staging_buffer =
      reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(staging_physical_page));
  memory_set(staging_buffer, 0, kPagingPageSize);
  const size_t bytes_read =
      file_read(&file_handle, staging_buffer, file_stat_result.size_bytes);
  const bool close_ok = file_close(&file_handle);
  if (!close_ok || bytes_read != file_stat_result.size_bytes) {
    return false;
  }

  const auto* const file_header =
      reinterpret_cast<const Elf64FileHeader*>(staging_buffer);
  if (!elf_header_is_valid(file_header, file_stat_result.size_bytes)) {
    return false;
  }

  const auto* const program_headers =
      reinterpret_cast<const Elf64ProgramHeader*>(
          staging_buffer + file_header->program_header_offset);

  // 第 1 遍扫描：只做“验证 + 规划映射”。
  // 这里会统计：
  // - 一共有多少个 PT_LOAD 段
  // - 总共要映射多少页
  // - 第一个 loadable segment 是谁
  const Elf64ProgramHeader* first_loadable_segment = nullptr;
  uint32_t loadable_segment_count = 0;
  uint32_t total_page_count = 0;
  uint64_t first_page_physical = 0;
  for (uint16_t i = 0; i < file_header->program_header_count; ++i) {
    if (program_headers[i].type != kElfProgramTypeLoad) {
      continue;
    }

    if (!loadable_segment_is_valid(user_space, &program_headers[i],
                                   file_stat_result.size_bytes)) {
      return false;
    }

    ++loadable_segment_count;
    if (first_loadable_segment == nullptr) {
      first_loadable_segment = &program_headers[i];
    }

    const uint64_t segment_end =
        program_headers[i].virtual_address + program_headers[i].memory_size;
    const uint64_t load_base =
        align_down(program_headers[i].virtual_address, kPagingPageSize);
    const uint64_t load_limit =
        align_up(segment_end, kPagingPageSize);
    const uint32_t page_count = static_cast<uint32_t>(
        (load_limit - load_base) / kPagingPageSize);
    if (page_count == 0 ||
        total_page_count + page_count > kElfLoaderMaxLoadablePages) {
      return false;
    }

    const uint64_t page_flags =
        (program_headers[i].flags & kElfProgramFlagWrite) != 0
            ? kPageWritable
            : 0;

    // 这里先解决“这个段需要住在哪些用户页里”。
    // 先把房子准备好，后面再往里面搬文件字节。
    // 先把这个段覆盖到的每一页都分出来并映射进用户地址空间。
    for (uint32_t page_index = 0; page_index < page_count; ++page_index) {
      const uint64_t physical_page = alloc_page(allocator);
      if (!page_is_directly_accessible_in_boot_identity_map(physical_page)) {
        return false;
      }

      memory_set(reinterpret_cast<void*>(static_cast<uintptr_t>(physical_page)),
                 0, kPagingPageSize);
      if (!address_space_map_user_page(user_space, allocator,
                                       load_base + page_index * kPagingPageSize,
                                       physical_page, page_flags)) {
        return false;
      }

      if (first_page_physical == 0) {
        first_page_physical = physical_page;
      }
      ++total_page_count;
    }

    // 第 2 步再把文件里真正存在的字节拷进去。
    // `memory_size > file_size` 的尾巴天然就保持 0，
    // 这就是最小版 `.bss` 零填充效果。
    for (uint64_t byte_index = 0; byte_index < program_headers[i].file_size;
         ++byte_index) {
      const uint64_t virtual_address =
          program_headers[i].virtual_address + byte_index;
      const uint64_t physical_address =
          address_space_resolve_mapping(user_space, virtual_address);
      if (!page_is_directly_accessible_in_boot_identity_map(physical_address)) {
        return false;
      }

      // 注意这里是“按 ELF 里声明的虚拟地址一个字节一个字节放进去”，
      // 不是简单把整个文件原样拷到一片连续内存里。
      auto* const destination =
          reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(physical_address));
      *destination = staging_buffer[program_headers[i].offset + byte_index];
    }
  }

  if (loadable_segment_count == 0 ||
      first_loadable_segment == nullptr ||
      !entry_belongs_to_any_loadable_segment(program_headers,
                                             file_header->program_header_count,
                                             file_header->entry)) {
    return false;
  }

  // 最后再把“调用方最关心的结果”收口输出：
  // entry point、第一段信息、总段数、总页数、首张物理页等。
  out_program->inode_number = file_stat_result.inode_number;
  out_program->file_size_bytes = file_stat_result.size_bytes;
  out_program->entry_point = file_header->entry;
  out_program->segment_virtual_address = first_loadable_segment->virtual_address;
  out_program->segment_file_offset = first_loadable_segment->offset;
  out_program->segment_file_size = first_loadable_segment->file_size;
  out_program->segment_memory_size = first_loadable_segment->memory_size;
  out_program->first_page_physical = first_page_physical;
  out_program->loadable_segment_count = loadable_segment_count;
  out_program->mapped_page_count = total_page_count;
  out_program->segment_flags = first_loadable_segment->flags;
  return true;
}
