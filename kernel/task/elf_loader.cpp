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
  return physical_address != 0 && physical_address < kPagingBootIdentityLimit;
}

bool elf_header_is_valid(const Elf64FileHeader* header, size_t file_size_bytes) {
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

    for (uint64_t byte_index = 0; byte_index < program_headers[i].file_size;
         ++byte_index) {
      const uint64_t virtual_address =
          program_headers[i].virtual_address + byte_index;
      const uint64_t physical_address =
          address_space_resolve_mapping(user_space, virtual_address);
      if (!page_is_directly_accessible_in_boot_identity_map(physical_address)) {
        return false;
      }

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
