// 宿主工具复用真正的 OS64FS 实现，只在内存副本里更新 /bin。
// 所有更新和重新挂载都成功后，外层脚本才替换镜像文件。
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include "fs/file.hpp"
#include "fs/directory.hpp"

void* kmalloc(size_t size) { return std::malloc(size); }
bool kfree(void* pointer) { std::free(pointer); return true; }
using Bytes = std::vector<uint8_t>;
static bool read_sector(const void* context, uint32_t sector, void* buffer, size_t size) {
  const auto& disk = *static_cast<const Bytes*>(context);
  if (size < 512 || sector >= disk.size() / 512) return false;
  std::memcpy(buffer, disk.data() + sector * 512, 512);
  return true;
}
static bool write_sector(void* context, uint32_t sector, const void* buffer, size_t size) {
  auto& disk = *static_cast<Bytes*>(context);
  if (size < 512 || sector >= disk.size() / 512) return false;
  std::memcpy(disk.data() + sector * 512, buffer, 512);
  return true;
}
static Bytes read_bytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::string("Cannot read ") + path;
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
using Snapshot = std::map<std::string, std::tuple<uint32_t, uint16_t, uint32_t, Bytes>>;
static void snapshot_user_files(Os64Fs* fs, const std::string& path, Snapshot& snapshot,
                                unsigned depth = 0) {
  if (path == "/bin") return;
  if (depth > 16) throw std::string("Directory depth exceeds format limit");
  Os64FsInode inode;
  if (!os64fs_lookup_path(fs, path.c_str(), &inode)) throw std::string("Snapshot lookup failed");
  Bytes content;
  if (inode.type == kOs64FsTypeFile) {
    FileHandle file;
    if (!file_open(fs, path.c_str(), &file)) throw std::string("Snapshot open failed");
    content.resize(inode.size_bytes);
    if (!content.empty() && file_read(&file, content.data(), content.size()) != content.size())
      throw std::string("Snapshot read failed");
    (void)file_close(&file);
  } else if (inode.type == kOs64FsTypeDirectory) {
    DirectoryHandle directory;
    if (!directory_open(fs, path.c_str(), &directory)) throw std::string("Snapshot directory failed");
    const uint32_t count = directory_entry_count(&directory);
    for (uint32_t index = 0; index < count; ++index) {
      DirectoryEntry entry;
      if (!directory_read(&directory, &entry)) throw std::string("Snapshot entry failed");
      snapshot_user_files(fs, (path == "/" ? path : path + "/") + entry.name,
                          snapshot, depth + 1);
    }
    (void)directory_close(&directory);
  }
  snapshot[path] = {inode.inode_number, inode.type, inode.mode, content};
}
int main(int argc, char** argv) {
  if (argc < 4) return 2; // input image, output image, executable paths...
  try {
    Bytes disk = read_bytes(argv[1]);
    if (disk.empty() || disk.size() % 512 != 0) throw std::string("Invalid image size");
    BlockDevice device{&disk, 0, static_cast<uint32_t>(disk.size() / 512),
                       512, true, read_sector, write_sector, nullptr};
    Os64Fs fs;
    if (!initialize_os64fs(&fs, &device)) throw std::string("Cannot mount image; original preserved");
    Snapshot original_user_files;
    snapshot_user_files(&fs, "/", original_user_files);
    Os64FsInode bin;
    if (!os64fs_lookup_path(&fs, "/bin", &bin)) {
      if (!os64fs_create_directory(&fs, "/bin")) throw std::string("Cannot create /bin");
    } else if (bin.type != kOs64FsTypeDirectory) throw std::string("/bin is not a directory");
    for (int i = 3; i < argc; ++i) {
      std::string source = argv[i];
      const auto slash = source.find_last_of('/');
      std::string name = source.substr(slash == std::string::npos ? 0 : slash + 1);
      if (name.size() < 5 || name.substr(name.size() - 4) != ".elf")
        throw std::string("Expected .elf tool");
      name.resize(name.size() - 4);
      if (name.empty() || name.size() > 56) throw std::string("Invalid tool name");
      Bytes bytes = read_bytes(source);
      const std::string target = "/bin/" + name;
      if (!os64fs_write_file(&fs, target.c_str(), bytes.data(), bytes.size()))
        throw std::string("Cannot update ") + target + "; original image preserved";
    }
    if (!initialize_os64fs(&fs, &device)) throw std::string("Updated image failed mount validation");
    Snapshot updated_user_files;
    snapshot_user_files(&fs, "/", updated_user_files);
    if (updated_user_files != original_user_files)
      throw std::string("User file preservation check failed; original image preserved");
    for (int i = 3; i < argc; ++i) {
      std::string source = argv[i];
      std::string name = source.substr(source.find_last_of('/') + 1);
      name.resize(name.size() - 4);
      Bytes expected = read_bytes(source);
      FileHandle file;
      if (!file_open(&fs, ("/bin/" + name).c_str(), &file)) throw std::string("Tool verification open failed");
      Bytes actual(file.inode.size_bytes);
      if (file_read(&file, actual.data(), actual.size()) != actual.size() || actual != expected)
        throw std::string("Tool verification content failed");
      (void)file_close(&file);
    }
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(disk.data()), disk.size());
    output.close();
    if (!output) throw std::string("Cannot write staged image");
    std::cout << "Verified " << argc - 3 << " tools in updated /bin\n";
    return 0;
  } catch (const std::string& message) {
    std::cerr << message << '\n'; return 1;
  }
}
