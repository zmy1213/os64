#include "syscall/syscall.hpp"

#include "interrupts/interrupts.hpp"
#include "interrupts/keyboard.hpp"
#include "interrupts/pit.hpp"
#include "runtime/runtime.hpp"
#include "task/scheduler.hpp"
#include "memory/paging.hpp"

namespace {

constexpr size_t kMaxSyscallPositiveResult = 2147483647U;
SyscallContext* g_active_syscall_context = nullptr;  // 早期 boot/smoke 还会显式安装一份“当前内核上下文”；真正在线程里跑时，会优先改成取当前进程自己的 syscall_context。
SchedulerState* g_process_scheduler = nullptr;
PageAllocator* g_process_allocator = nullptr;
const Os64Fs* g_process_filesystem = nullptr;
VfsMount* g_process_vfs = nullptr;
constexpr size_t kMaxUserArguments = 8;
constexpr size_t kMaxUserTransfer = 69632;

extern "C" bool kernel_user_mode_exit_is_armed();
extern "C" [[noreturn]] void kernel_handle_user_mode_exit(uint64_t return_value);

// 下面这一组小函数先不碰文件系统或中断，
// 只负责最基础的“路径字符串清洗和拼接”。
// 这样后面的 `cd` / `open` / `stat` / `ls` 都能复用同一套路径规则。
bool is_space_char(char ch) {
  return ch == ' ' || ch == '\t';
}

bool is_path_separator(char ch) {
  return ch == '/';
}

const char* skip_spaces(const char* text) {
  if (text == nullptr) {
    return nullptr;
  }

  while (is_space_char(*text)) {
    ++text;
  }

  return text;
}

size_t string_length(const char* text) {
  if (text == nullptr) {
    return 0;
  }

  size_t length = 0;
  while (text[length] != '\0') {
    ++length;
  }

  return length;
}

const char* trim_trailing_spaces(const char* begin, const char* end) {
  while (end > begin && is_space_char(end[-1])) {
    --end;
  }

  return end;
}

bool copy_string(char* destination, size_t capacity, const char* source) {
  if (destination == nullptr || source == nullptr || capacity == 0) {
    return false;
  }

  const size_t length = string_length(source);
  if (length >= capacity) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    destination[i] = source[i];
  }
  destination[length] = '\0';
  return true;
}

bool set_root_path(char* path, size_t capacity) {
  if (path == nullptr || capacity < 2) {
    return false;
  }

  path[0] = '/';
  path[1] = '\0';
  return true;
}

const char* skip_path_separators(const char* cursor, const char* end) {
  while (cursor < end && is_path_separator(*cursor)) {
    ++cursor;
  }

  return cursor;
}

size_t path_component_length(const char* begin, const char* end) {
  size_t length = 0;
  while ((begin + length) < end &&
         !is_path_separator(begin[length])) {
    ++length;
  }

  return length;
}

bool path_component_is_dot(const char* component, size_t length) {
  return component != nullptr && length == 1 && component[0] == '.';
}

bool path_component_is_dot_dot(const char* component, size_t length) {
  return component != nullptr &&
         length == 2 &&
         component[0] == '.' &&
         component[1] == '.';
}

bool append_path_component(char* path,
                           size_t capacity,
                           const char* component,
                           size_t component_length) {
  if (path == nullptr || component == nullptr || capacity == 0 ||
      component_length == 0) {
    return false;
  }

  size_t current_length = string_length(path);
  const bool path_is_root =
      current_length == 1 && path[0] == '/';
  const size_t slash_bytes = path_is_root ? 0 : 1;
  if (current_length + slash_bytes + component_length >= capacity) {
    return false;
  }

  // 根路径 `/` 后面直接拼组件名即可；
  // 其它路径则要先补一个 `/` 再拼组件。
  if (!path_is_root) {
    path[current_length++] = '/';
  }

  for (size_t i = 0; i < component_length; ++i) {
    path[current_length + i] = component[i];
  }

  path[current_length + component_length] = '\0';
  return true;
}

void pop_path_component(char* path) {
  if (path == nullptr) {
    return;
  }

  const size_t length = string_length(path);
  if (length <= 1) {
    (void)set_root_path(path, kSyscallPathCapacity);
    return;
  }

  size_t index = length;
  while (index > 1 && path[index - 1] != '/') {
    --index;
  }

  if (index <= 1) {
    (void)set_root_path(path, kSyscallPathCapacity);
    return;
  }

  path[index - 1] = '\0';
}

// 下面这一组小函数处理“公开 syscall fd”和“内核内部 fd 表槽位”的转换。
// 用户态以后看到的是：
// - 0 stdin
// - 1 stdout
// - 2 stderr
// - 3+ 普通文件
// 但内核自己的 fd 表还是从 0 开始算，所以这里要做一层平移。
bool syscall_fd_is_open(SyscallContext* context, int32_t fd) {
  return syscall_context_is_ready(context) &&
         fd_is_open(context->fd_table, fd);
}

bool syscall_fd_is_reserved(int32_t fd) {
  return fd >= kSyscallStandardInputFd &&
         fd < kSyscallFirstFileFd;
}

bool syscall_fd_is_output(int32_t fd) {
  return fd == kSyscallStandardOutputFd ||
         fd == kSyscallStandardErrorFd;
}

bool syscall_fd_is_file(int32_t fd) {
  return fd >= kSyscallFirstFileFd;
}

int32_t table_fd_to_syscall_fd(int32_t table_fd) {
  if (table_fd < 0) {
    return table_fd;
  }

  return table_fd + kSyscallFirstFileFd;
}

int32_t syscall_fd_to_table_fd(int32_t syscall_fd) {
  if (!syscall_fd_is_file(syscall_fd)) {
    return kInvalidFileDescriptor;
  }

  return syscall_fd - kSyscallFirstFileFd;
}

bool syscall_write_handler_is_ready(const SyscallContext* context) {
  return context != nullptr && context->write_handler != nullptr;
}

uint64_t encode_syscall_result(int64_t value) {
  // 当前返回值协议很简单：
  // 正数/0 表示成功结果，
  // 负数直接当作错误码塞回 RAX。
  return static_cast<uint64_t>(value);
}

int64_t syscall_status_result(SyscallStatus status) {
  return static_cast<int64_t>(status);
}

int32_t read_stdin_stream(void* buffer, size_t bytes_to_read) {
  if (!keyboard_is_ready()) {
    return kSyscallUnsupported;
  }

  char* out_buffer = static_cast<char*>(buffer);
  size_t total_read = 0;

  // stdin 这层想模拟的是“终端输入流”而不是“文件 EOF”：
  // 如果现在没字符，不代表结束，而是应该继续等用户敲键盘。
  for (;;) {
    char character = '\0';
    while (total_read < bytes_to_read &&
           keyboard_try_read_stream_char(&character)) {
      out_buffer[total_read++] = character;
    }

    if (total_read > 0) {
      return static_cast<int32_t>(total_read);
    }

    // 这里 deliberately 不把“当前没有字符”当成 EOF。
    // 第一版 stdin 更像终端输入：如果中断开着，就等下一次键盘 IRQ 把字符送进来。
    if (!interrupts_are_enabled()) {
      return 0;
    }

    // 如果当前正跑在线程上下文里，
    // 这里优先把线程真正挂进 keyboard wait queue，
    // 由下一个字符 IRQ 来唤醒它。
    if (keyboard_wait_for_stream_char()) {
      continue;
    }

    // 退化路径里虽然没有真正 block 住线程，
    // 但依然尽量把 CPU 交还出去，避免在内核里纯忙等。
    // 如果当前还没有线程上下文，或者暂时没法真正 block，
    // 那就退回到旧的“hlt 等中断 + 安全点 yield”路径。
    wait_for_interrupt();
    (void)scheduler_yield_if_requested();
  }
}

// 现在 syscall 分发优先看“当前线程所属进程”的上下文；
// 只有在还没进入正式线程/进程运行期时，才退回到早期全局上下文。
SyscallContext* current_dispatch_context() {
  ThreadControlBlock* const current_thread = scheduler_active_thread();
  if (current_thread != nullptr &&
      current_thread->owner != nullptr &&
      syscall_context_is_ready(&current_thread->owner->syscall_context)) {
    // 正常运行期优先走“当前线程所属进程”的 syscall 视图。
    return &current_thread->owner->syscall_context;
  }

  // 只有最早期 boot/smoke 还没真正跑在线程里时，才回退到全局上下文。
  return g_active_syscall_context;
}

bool frame_came_from_user_mode(const SyscallInterruptFrame* frame) {
  return frame != nullptr && (frame->cs & 0x3) == 3;
}

bool user_range_valid(uint64_t address, size_t bytes, bool writable) {
  if (bytes == 0) {
    return true;
  }
  AddressSpace current;
  return initialize_kernel_address_space_view(&current) &&
         address_space_user_range_valid(&current, address, bytes, writable);
}

bool user_path_valid(uint64_t address) {
  for (size_t i = 0; i < kSyscallPathCapacity; ++i) {
    if (address + i < address || !user_range_valid(address + i, 1, false)) {
      return false;
    }
    if (*reinterpret_cast<const char*>(address + i) == '\0') {
      return true;
    }
  }
  return false;
}

bool user_syscall_arguments_valid(const SyscallInterruptFrame* frame) {
  switch (frame->rax) {
    case kSyscallNumberGetCwd:
      return frame->rsi <= kMaxUserTransfer &&
             user_range_valid(frame->rdi, frame->rsi, true);
    case kSyscallNumberChdir:
    case kSyscallNumberOpen:
    case kSyscallNumberOpenFlags:
    case kSyscallNumberMkdir:
    case kSyscallNumberUnlink:
      return user_path_valid(frame->rdi);
    case kSyscallNumberRead:
    case kSyscallNumberWrite:
      return frame->rdx <= kMaxUserTransfer &&
             user_range_valid(frame->rsi, frame->rdx,
                              frame->rax == kSyscallNumberRead);
    case kSyscallNumberStat:
      return user_range_valid(frame->rsi, sizeof(VfsStat), true);
    case kSyscallNumberStatPath:
      return user_path_valid(frame->rdi) &&
             user_range_valid(frame->rsi, sizeof(VfsStat), true);
    case kSyscallNumberListDir:
      return user_path_valid(frame->rdi) && frame->rdx <= 128 &&
             user_range_valid(frame->rsi,
                              frame->rdx * sizeof(VfsDirectoryEntry), true);
    case kSyscallNumberSpawn: {
      if (!user_path_valid(frame->rdi) || frame->rdx > kMaxUserArguments ||
          !user_range_valid(frame->rsi, frame->rdx * sizeof(uint64_t), false)) {
        return false;
      }
      const auto* arguments = reinterpret_cast<const uint64_t*>(frame->rsi);
      for (size_t i = 0; i < frame->rdx; ++i) {
        if (!user_path_valid(arguments[i])) {
          return false;
        }
      }
      return true;
    }
    case kSyscallNumberWaitPid:
      return frame->rsi == 0 || user_range_valid(frame->rsi, sizeof(int32_t), true);
    case kSyscallNumberReplaceFile:
      return user_path_valid(frame->rdi) && frame->rdx <= kMaxUserTransfer &&
             user_range_valid(frame->rsi, frame->rdx, false);
    default:
      return true;
  }
}

void capture_current_user_trap_frame(const SyscallInterruptFrame* frame) {
  ThreadControlBlock* const current_thread = scheduler_active_thread();
  if (current_thread == nullptr ||
      current_thread->execution_mode != kThreadExecutionModeUser ||
      !frame_came_from_user_mode(frame)) {
    return;
  }

  UserTrapFrame& trap_frame = current_thread->user_trap_frame;
  // 这里做的事情可以简单理解成：
  // “把这次从 ring 3 打进内核时，CPU 暂停下来的用户态现场完整抄一份出来”。
  // 这样线程以后 yield / block / 被抢占之后，才有机会恢复回原来的用户态位置继续跑。
  trap_frame.r15 = frame->r15;
  trap_frame.r14 = frame->r14;
  trap_frame.r13 = frame->r13;
  trap_frame.r12 = frame->r12;
  trap_frame.r11 = frame->r11;
  trap_frame.r10 = frame->r10;
  trap_frame.r9 = frame->r9;
  trap_frame.r8 = frame->r8;
  trap_frame.rdi = frame->rdi;
  trap_frame.rsi = frame->rsi;
  trap_frame.rbp = frame->rbp;
  trap_frame.rbx = frame->rbx;
  trap_frame.rdx = frame->rdx;
  trap_frame.rcx = frame->rcx;
  trap_frame.rax = frame->rax;
  trap_frame.vector = frame->vector;
  trap_frame.error_code = frame->error_code;
  trap_frame.rip = frame->rip;
  trap_frame.cs = frame->cs;
  trap_frame.rflags = frame->rflags;

  const uint64_t* const tail =
      reinterpret_cast<const uint64_t*>(
          reinterpret_cast<const uint8_t*>(frame) +
          sizeof(SyscallInterruptFrame));
  trap_frame.rsp = tail[0];
  trap_frame.ss = tail[1];
  current_thread->has_user_trap_frame = true;
}

int64_t dispatch_syscall_registers(uint64_t syscall_number,
                                   uint64_t argument0,
                                   uint64_t argument1,
                                   uint64_t argument2,
                                   uint64_t argument3) {
  SyscallContext* context = current_dispatch_context();
  if (!syscall_context_is_ready(context)) {
    return syscall_status_result(kSyscallInvalidArgument);
  }

  // 这一层就是“寄存器 ABI -> C++ 接口”的总翻译器。
  // 它不直接做文件系统或调度逻辑，只负责：
  // 1. 按 syscall 编号选目标
  // 2. 把整数寄存器参数转回指针/size/fd
  // 3. 把结果重新编码回寄存器返回值
  switch (syscall_number) {
    case kSyscallNumberGetCwd:
      return static_cast<int64_t>(
          sys_getcwd(context,
                     reinterpret_cast<char*>(argument0),
                     static_cast<size_t>(argument1)));
    case kSyscallNumberChdir:
      return syscall_status_result(
          sys_chdir(context, reinterpret_cast<const char*>(argument0)));
    case kSyscallNumberOpen:
      return static_cast<int64_t>(
          sys_open(context, reinterpret_cast<const char*>(argument0)));
    case kSyscallNumberOpenFlags:
      if (argument1 > UINT32_MAX) {
        return kSyscallInvalidArgument;
      }
      return static_cast<int64_t>(
          sys_open(context, reinterpret_cast<const char*>(argument0),
                    static_cast<uint32_t>(argument1)));
    case kSyscallNumberRead:
      return static_cast<int64_t>(
          sys_read(context,
                   static_cast<int32_t>(argument0),
                   reinterpret_cast<void*>(argument1),
                   static_cast<size_t>(argument2)));
    case kSyscallNumberWrite:
      return static_cast<int64_t>(
          sys_write(context,
                    static_cast<int32_t>(argument0),
                    reinterpret_cast<const void*>(argument1),
                    static_cast<size_t>(argument2)));
    case kSyscallNumberExit:
      // `exit` 比普通 syscall 特殊：
      // 它不会像 read/open 那样“返回到用户态继续下一条指令”，
      // 而是直接把这条用户线程的生命周期结束掉。
      if (scheduler_active_thread() != nullptr &&
          scheduler_active_thread()->execution_mode == kThreadExecutionModeUser) {
        scheduler_exit_current_user_process(static_cast<int64_t>(argument0));
      }
      if (!kernel_user_mode_exit_is_armed()) {
        return syscall_status_result(kSyscallUnsupported);
      }
      kernel_handle_user_mode_exit(argument0);
    case kSyscallNumberYield: {
      ThreadControlBlock* const current_thread = scheduler_active_thread();
      if (current_thread == nullptr ||
          current_thread->execution_mode != kThreadExecutionModeUser) {
        return syscall_status_result(kSyscallUnsupported);
      }

      ++current_thread->user_yield_count;

      // `yield` 的重点不是返回某个数据，
      // 而是让这条用户线程主动把 CPU 交出去，之后再有机会恢复回来继续跑。
      (void)scheduler_yield_current_thread();
      return syscall_status_result(kSyscallOk);
    }
    case kSyscallNumberClose:
      return syscall_status_result(
          sys_close(context, static_cast<int32_t>(argument0)));
    case kSyscallNumberSeek:
      return syscall_status_result(
          sys_seek(context,
                   static_cast<int32_t>(argument0),
                   static_cast<uint32_t>(argument1)));
    case kSyscallNumberStat:
      return syscall_status_result(
          sys_stat(context,
                   static_cast<int32_t>(argument0),
                   reinterpret_cast<VfsStat*>(argument1)));
    case kSyscallNumberStatPath:
      return syscall_status_result(
          sys_stat_path(context,
                        reinterpret_cast<const char*>(argument0),
                        reinterpret_cast<VfsStat*>(argument1)));
    case kSyscallNumberListDir:
      return static_cast<int64_t>(
          sys_listdir(context,
                      reinterpret_cast<const char*>(argument0),
                      reinterpret_cast<VfsDirectoryEntry*>(argument1),
                      static_cast<size_t>(argument2)));
    case kSyscallNumberMkdir:
      return sys_mkdir(context, reinterpret_cast<const char*>(argument0));
    case kSyscallNumberUnlink:
      return sys_unlink(context, reinterpret_cast<const char*>(argument0));
    case kSyscallNumberSync:
      return sys_sync(context);
    case kSyscallNumberGetPid: {
      auto* thread = scheduler_active_thread();
      return thread != nullptr && thread->owner != nullptr ? thread->owner->pid : 0;
    }
    case kSyscallNumberSleep:
      if (argument0 > 86400000ULL || !timer_is_ready()) {
        return kSyscallInvalidArgument;
      }
      return timer_sleep_ms(argument0) ? kSyscallOk : kSyscallUnsupported;
    case kSyscallNumberSpawn:
      return sys_spawn(context, reinterpret_cast<const char*>(argument0),
                        reinterpret_cast<const char* const*>(argument1),
                        static_cast<size_t>(argument2));
    case kSyscallNumberWaitPid:
      return sys_waitpid(static_cast<int32_t>(argument0),
                          reinterpret_cast<int32_t*>(argument1));
    case kSyscallNumberBrk:
      return static_cast<int64_t>(sys_brk(argument0));
    case kSyscallNumberReplaceFile:
      return sys_replace_file(context, reinterpret_cast<const char*>(argument0),
                               reinterpret_cast<const void*>(argument1), argument2);
    default:
      (void)argument3;
      return syscall_status_result(kSyscallInvalidArgument);
  }
}

SyscallStatus stat_path_internal(SyscallContext* context, const char* path,
                                 VfsStat* out_stat, char* resolved_path,
                                 size_t resolved_capacity) {
  if (!syscall_context_is_ready(context) || out_stat == nullptr) {
    return kSyscallInvalidArgument;
  }

  char resolved[kSyscallPathCapacity];
  char* path_buffer = resolved;
  size_t path_capacity = sizeof(resolved);

  if (resolved_path != nullptr) {
    if (resolved_capacity < 2) {
      return kSyscallInvalidArgument;
    }

    path_buffer = resolved_path;
    path_capacity = resolved_capacity;
  }

  // 这层的价值是把“路径解析 + vfs_stat + 错误码翻译”收口到一个内部 helper，
  // 这样 `chdir` / `stat_path` / `listdir` 这些基于路径的 syscall 都不用重复写一遍。
  if (!syscall_resolve_path(context, path, path_buffer, path_capacity)) {
    return kSyscallInvalidArgument;
  }

  return vfs_stat(context->fd_table->vfs, path_buffer, out_stat)
             ? kSyscallOk
             : kSyscallNotFound;
}

}  // namespace

bool initialize_syscall_context(SyscallContext* context,
                                FileDescriptorTable* fd_table) {
  if (context == nullptr) {
    return false;
  }

  // 先清空，避免初始化失败时留下半有效的旧 fd 表指针。
  memory_set(context, 0, sizeof(*context));

  if (!file_descriptor_table_is_ready(fd_table)) {
    return false;
  }

  context->fd_table = fd_table;
  return set_root_path(context->current_working_directory,
                       sizeof(context->current_working_directory));
}

bool syscall_context_is_ready(const SyscallContext* context) {
  return context != nullptr &&
         context->fd_table != nullptr && context->fd_table->vfs != nullptr &&
         context->fd_table->vfs->os64fs != nullptr;
}

bool install_syscall_write_handler(SyscallContext* context,
                                   SyscallWriteHandler handler,
                                   void* write_context) {
  if (!syscall_context_is_ready(context) || handler == nullptr) {
    return false;
  }

  context->write_handler = handler;
  context->write_context = write_context;
  return true;
}

bool install_syscall_dispatch_context(SyscallContext* context) {
  if (!syscall_context_is_ready(context)) {
    return false;
  }

  // 这只是“早期默认上下文安装位”。
  // 真正进入线程/进程运行期后，分发器优先走 current thread -> owner -> syscall_context。
  g_active_syscall_context = context;
  return true;
}

bool syscall_dispatch_is_ready() {
  return syscall_context_is_ready(current_dispatch_context());
}

const char* syscall_current_working_directory(const SyscallContext* context) {
  if (!syscall_context_is_ready(context) ||
      context->current_working_directory[0] == '\0') {
    return nullptr;
  }

  return context->current_working_directory;
}

bool syscall_resolve_path(const SyscallContext* context,
                          const char* raw_path,
                          char* out_path,
                          size_t capacity) {
  if (!syscall_context_is_ready(context) || out_path == nullptr ||
      capacity < 2) {
    return false;
  }

  const char* base_path = syscall_current_working_directory(context);
  if (base_path == nullptr) {
    return false;
  }

  const char* begin = skip_spaces(raw_path);
  if (begin == nullptr || begin[0] == '\0') {
    return copy_string(out_path, capacity, base_path);
  }

  const char* end = begin + string_length(begin);
  end = trim_trailing_spaces(begin, end);
  if (end <= begin) {
    return copy_string(out_path, capacity, base_path);
  }

  const bool absolute = is_path_separator(begin[0]);
  if (absolute) {
    // 绝对路径从 `/` 开始重新算，不继承当前 cwd。
    if (!set_root_path(out_path, capacity)) {
      return false;
    }
  } else if (!copy_string(out_path, capacity, base_path)) {
    // 相对路径先拿 cwd 当起点，再往后拼组件。
    return false;
  }

  // 这里做的就是“最小版路径规范化”：
  // - 连续 `/` 压成一个分隔效果
  // - `.` 直接跳过
  // - `..` 回退到上一级
  // - 其它名字就正常追加
  const char* cursor =
      absolute ? skip_path_separators(begin, end) : begin;
  while (cursor < end) {
    const size_t component_length =
        path_component_length(cursor, end);
    if (component_length == 0) {
      cursor = skip_path_separators(cursor, end);
      continue;
    }

    if (path_component_is_dot(cursor, component_length)) {
      cursor = skip_path_separators(cursor + component_length, end);
      continue;
    }

    if (path_component_is_dot_dot(cursor, component_length)) {
      pop_path_component(out_path);
      cursor = skip_path_separators(cursor + component_length, end);
      continue;
    }

    if (!append_path_component(out_path, capacity, cursor,
                               component_length)) {
      return false;
    }

    cursor = skip_path_separators(cursor + component_length, end);
  }

  return true;
}

int32_t sys_getcwd(SyscallContext* context, char* buffer, size_t capacity) {
  if (!syscall_context_is_ready(context) || buffer == nullptr ||
      capacity == 0) {
    return kSyscallInvalidArgument;
  }

  // 第一版先直接把内核里维护的 cwd 文本复制给调用者。
  const char* cwd = syscall_current_working_directory(context);
  if (cwd == nullptr || !copy_string(buffer, capacity, cwd)) {
    return kSyscallInvalidArgument;
  }

  return static_cast<int32_t>(string_length(buffer));
}

SyscallStatus sys_chdir(SyscallContext* context, const char* path) {
  if (!syscall_context_is_ready(context) || path == nullptr) {
    return kSyscallInvalidArgument;
  }

  // `chdir` 的关键不是简单改字符串，
  // 而是先确认这个路径真的存在，而且目标对象真的是目录。
  char resolved[kSyscallPathCapacity];
  VfsStat stat;
  const SyscallStatus status =
      stat_path_internal(context, path, &stat, resolved, sizeof(resolved));
  if (status != kSyscallOk) {
    return status;
  }

  if (stat.type != kVfsNodeTypeDirectory) {
    return kSyscallNotFile;
  }

  return copy_string(context->current_working_directory,
                     sizeof(context->current_working_directory),
                     resolved)
             ? kSyscallOk
             : kSyscallInvalidArgument;
}

int32_t sys_open(SyscallContext* context, const char* path, uint32_t flags) {
  if (!syscall_context_is_ready(context) || path == nullptr) {
    return kSyscallInvalidArgument;
  }
  if (flags == 0) {
    flags = kOpenRead;
  }
  constexpr uint32_t allowed = kOpenRead | kOpenWrite | kOpenCreate |
                               kOpenTruncate | kOpenAppend;
  if ((flags & ~allowed) != 0 || (flags & (kOpenRead | kOpenWrite)) == 0 ||
      ((flags & (kOpenCreate | kOpenTruncate | kOpenAppend)) != 0 &&
       (flags & kOpenWrite) == 0)) {
    return kSyscallInvalidArgument;
  }

  // 先把相对路径按当前 cwd 展开成绝对路径，
  // 后面 fd 层和 VFS 层都只面对统一后的路径。
  char resolved[kSyscallPathCapacity];
  if (!syscall_resolve_path(context, path, resolved, sizeof(resolved))) {
    return kSyscallInvalidArgument;
  }

  // open 前先 stat 一下，这样能把“不存在”和“路径是目录”分成不同错误码。
  VfsStat stat;
  const bool exists = vfs_stat(context->fd_table->vfs, resolved, &stat);
  if (!exists && (flags & kOpenCreate) == 0) {
    return kSyscallNotFound;
  }

  if (exists && stat.type != kVfsNodeTypeFile) {
    return kSyscallNotFile;
  }

  const int32_t fd = fd_open(context->fd_table, resolved, flags);
  if (fd == kInvalidFileDescriptor) {
    return kSyscallIoError;
  }

  // 用户态最终看到的不是内核 fd 表里的 0/1/2/...，
  // 而是经过保留位平移后的公开 fd：
  // 0 stdin, 1 stdout, 2 stderr, 3+ 普通文件。
  return table_fd_to_syscall_fd(fd);
}

int32_t sys_read(SyscallContext* context, int32_t fd,
                 void* buffer, size_t bytes_to_read) {
  if (!syscall_context_is_ready(context)) {
    return kSyscallInvalidArgument;
  }

  if (bytes_to_read == 0) {
    return 0;
  }

  if (buffer == nullptr || bytes_to_read > kMaxSyscallPositiveResult) {
    return kSyscallInvalidArgument;
  }

  if (fd == kSyscallStandardInputFd) {
    // `read(0, ...)` 走的是键盘字符流，而不是文件系统。
    return read_stdin_stream(buffer, bytes_to_read);
  }

  if (syscall_fd_is_reserved(fd)) {
    return kSyscallUnsupported;
  }

  // 其它 `3+` 的 fd 再映射回内核真实 fd 表槽位。
  const int32_t table_fd = syscall_fd_to_table_fd(fd);
  if (!syscall_fd_is_open(context, table_fd)) {
    return kSyscallBadFileDescriptor;
  }

  if (!fd_can_read(context->fd_table, table_fd)) {
    return kSyscallUnsupported;
  }

  const size_t bytes_read =
      fd_read(context->fd_table, table_fd, buffer, bytes_to_read);
  if (bytes_read > kMaxSyscallPositiveResult) {
    return kSyscallIoError;
  }

  return static_cast<int32_t>(bytes_read);
}

int32_t sys_write(SyscallContext* context, int32_t fd,
                  const void* buffer, size_t bytes_to_write) {
  if (!syscall_context_is_ready(context)) {
    return kSyscallInvalidArgument;
  }

  if (bytes_to_write == 0) {
    return 0;
  }

  if (buffer == nullptr || bytes_to_write > kMaxSyscallPositiveResult) {
    return kSyscallInvalidArgument;
  }

  if (syscall_fd_is_output(fd)) {
    if (!syscall_write_handler_is_ready(context)) {
      return kSyscallUnsupported;
    }

    // 当前 stdout/stderr 还没有真正 TTY 设备，
    // 所以先把字节流交给外部注入的 write_handler。
    const size_t bytes_written =
        context->write_handler(fd, buffer, bytes_to_write,
                               context->write_context);
    if (bytes_written > kMaxSyscallPositiveResult) {
      return kSyscallIoError;
    }

    return static_cast<int32_t>(bytes_written);
  }

  if (fd == kSyscallStandardInputFd) {
    return kSyscallUnsupported;
  }

  const int32_t table_fd = syscall_fd_to_table_fd(fd);
  if (!syscall_fd_is_open(context, table_fd)) {
    return kSyscallBadFileDescriptor;
  }

  if (!fd_can_write(context->fd_table, table_fd)) {
    return kSyscallUnsupported;
  }
  const int32_t written = fd_write(context->fd_table, table_fd, buffer, bytes_to_write);
  return written < 0 ? kSyscallIoError : written;
}

SyscallStatus sys_stat_path(SyscallContext* context, const char* path,
                            VfsStat* out_stat) {
  return stat_path_internal(context, path, out_stat, nullptr, 0);
}

int32_t sys_listdir(SyscallContext* context, const char* path,
                    VfsDirectoryEntry* out_entries, size_t entry_capacity) {
  if (!syscall_context_is_ready(context)) {
    return kSyscallInvalidArgument;
  }

  // `ls` 这一类操作先验证路径确实指向目录，
  // 再一次性把目录项平铺到调用者提供的数组里。
  char resolved[kSyscallPathCapacity];
  VfsStat stat;
  const SyscallStatus status =
      stat_path_internal(context, path, &stat, resolved, sizeof(resolved));
  if (status != kSyscallOk) {
    return status;
  }

  if (stat.type != kVfsNodeTypeDirectory) {
    return kSyscallNotFile;
  }

  VfsDirectory directory;
  if (!vfs_open_directory(context->fd_table->vfs, resolved, &directory)) {
    return kSyscallIoError;
  }

  const uint32_t entry_count =
      vfs_directory_entry_count(&directory);

  if (out_entries == nullptr && entry_capacity == 0) {
    // 两阶段接口：
    // 第一步允许调用者只问“这个目录有多少项”，先不真正拷贝目录项。
    (void)vfs_close_directory(&directory);
    return static_cast<int32_t>(entry_count);
  }

  if (out_entries == nullptr ||
      entry_capacity < static_cast<size_t>(entry_count)) {
    (void)vfs_close_directory(&directory);
    return kSyscallInvalidArgument;
  }

  for (uint32_t entry_index = 0; entry_index < entry_count; ++entry_index) {
    // 这一版先做成“整批拷贝目录项数组”，
    // 还没有做像 Linux `getdents` 那种更底层的字节打包 ABI。
    if (!vfs_read_directory(&directory,
                            &out_entries[static_cast<size_t>(entry_index)])) {
      (void)vfs_close_directory(&directory);
      return kSyscallIoError;
    }
  }

  if (!vfs_close_directory(&directory)) {
    return kSyscallIoError;
  }

  return static_cast<int32_t>(entry_count);
}

SyscallStatus sys_close(SyscallContext* context, int32_t fd) {
  if (!syscall_context_is_ready(context)) {
    return kSyscallInvalidArgument;
  }

  // 标准输入输出这些保留 fd 当前不允许走普通 close 语义。
  if (syscall_fd_is_reserved(fd)) {
    return kSyscallUnsupported;
  }

  const int32_t table_fd = syscall_fd_to_table_fd(fd);
  if (table_fd < 0 || static_cast<size_t>(table_fd) >= kFileDescriptorCapacity ||
      !context->fd_table->entries[table_fd].open) {
    return kSyscallBadFileDescriptor;
  }

  return fd_close(context->fd_table, table_fd) ? kSyscallOk : kSyscallIoError;
}

SyscallStatus sys_seek(SyscallContext* context, int32_t fd, uint32_t offset) {
  if (!syscall_context_is_ready(context)) {
    return kSyscallInvalidArgument;
  }

  // seek 只对真正打开的普通文件 fd 生效。
  if (syscall_fd_is_reserved(fd)) {
    return kSyscallUnsupported;
  }

  const int32_t table_fd = syscall_fd_to_table_fd(fd);
  if (!syscall_fd_is_open(context, table_fd)) {
    return kSyscallBadFileDescriptor;
  }

  return fd_seek(context->fd_table, table_fd, offset)
             ? kSyscallOk
             : kSyscallIoError;
}

SyscallStatus sys_stat(SyscallContext* context, int32_t fd,
                       VfsStat* out_stat) {
  if (!syscall_context_is_ready(context) || out_stat == nullptr) {
    return kSyscallInvalidArgument;
  }

  if (syscall_fd_is_reserved(fd)) {
    return kSyscallUnsupported;
  }

  const int32_t table_fd = syscall_fd_to_table_fd(fd);
  if (!syscall_fd_is_open(context, table_fd)) {
    return kSyscallBadFileDescriptor;
  }

  return fd_stat(context->fd_table, table_fd, out_stat)
             ? kSyscallOk
             : kSyscallIoError;
}

bool install_syscall_process_services(SchedulerState* scheduler,
                                      PageAllocator* allocator,
                                      const Os64Fs* filesystem,
                                      VfsMount* vfs) {
  if (!scheduler_is_ready(scheduler) || allocator == nullptr ||
      !os64fs_is_mounted(filesystem) || !vfs_is_mounted(vfs)) {
    return false;
  }
  g_process_scheduler = scheduler;
  g_process_allocator = allocator;
  g_process_filesystem = filesystem;
  g_process_vfs = vfs;
  return true;
}

int32_t sys_spawn(SyscallContext* context, const char* path,
                  const char* const* argv, size_t argc) {
  if (!syscall_context_is_ready(context) || g_process_scheduler == nullptr ||
      path == nullptr || argc > kMaxUserArguments || (argc != 0 && argv == nullptr)) {
    return kSyscallInvalidArgument;
  }
  char resolved[kSyscallPathCapacity];
  if (!syscall_resolve_path(context, path, resolved, sizeof(resolved))) {
    return kSyscallInvalidArgument;
  }
  char copied_arguments[kMaxUserArguments][kSyscallPathCapacity];
  const char* arguments[kMaxUserArguments];
  if (argc == 0) {
    arguments[0] = resolved;
    argc = 1;
  } else {
    for (size_t i = 0; i < argc; ++i) {
      if (!copy_string(copied_arguments[i], sizeof(copied_arguments[i]), argv[i])) {
        return kSyscallInvalidArgument;
      }
      arguments[i] = copied_arguments[i];
    }
  }
  VfsStat stat;
  if (!vfs_stat(g_process_vfs, resolved, &stat)) {
    return kSyscallNotFound;
  }
  if (stat.type != kVfsNodeTypeFile) {
    return kSyscallNotFile;
  }
  SchedulerElfThreadLoadResult launch;
  if (!scheduler_create_user_elf_thread(g_process_scheduler, g_process_allocator,
                                        g_process_filesystem, g_process_vfs,
                                        context->write_handler, context->write_context,
                                        resolved, "main", resolved,
                                        kUserAddressSpaceDefaultStackTop, 0x202,
                                        kThreadPriorityNormal, &launch)) {
    return kSyscallIoError;
  }
  if (!copy_string(launch.process->syscall_context.current_working_directory,
                    kSyscallPathCapacity, context->current_working_directory) ||
      !scheduler_prepare_user_arguments(launch.process, launch.thread, argc, arguments)) {
    (void)scheduler_discard_process(g_process_scheduler, launch.process->pid);
    return kSyscallIoError;
  }
  return static_cast<int32_t>(launch.process->pid);
}

int32_t sys_waitpid(int32_t pid, int32_t* status) {
  if (pid <= 0 || g_process_scheduler == nullptr) {
    return kSyscallInvalidArgument;
  }
  int64_t exit_status = 0;
  if (!scheduler_wait_process(g_process_scheduler, static_cast<uint32_t>(pid),
                                &exit_status)) {
    return kSyscallNotFound;
  }
  if (!scheduler_reap_process(g_process_scheduler, static_cast<uint32_t>(pid), nullptr)) {
    return kSyscallIoError;
  }
  if (status != nullptr) {
    *status = static_cast<int32_t>(exit_status);
  }
  return pid;
}

SyscallStatus sys_mkdir(SyscallContext* context, const char* path) {
  char resolved[kSyscallPathCapacity];
  if (path == nullptr || !syscall_resolve_path(context, path, resolved, sizeof(resolved))) {
    return kSyscallInvalidArgument;
  }
  VfsMount mount = *context->fd_table->vfs;
  return vfs_create_directory(&mount, resolved) ? kSyscallOk : kSyscallIoError;
}

SyscallStatus sys_unlink(SyscallContext* context, const char* path) {
  char resolved[kSyscallPathCapacity];
  if (path == nullptr || !syscall_resolve_path(context, path, resolved, sizeof(resolved))) {
    return kSyscallInvalidArgument;
  }
  VfsStat stat;
  if (!vfs_stat(context->fd_table->vfs, resolved, &stat)) {
    return kSyscallNotFound;
  }
  // This small filesystem refuses removal of open inodes and active cwd's.
  // That keeps descriptors valid without claiming Unix orphan-inode semantics.
  const auto busy_context = [&stat, &resolved, &context](const SyscallContext* candidate) {
    if (!syscall_context_is_ready(candidate) ||
        candidate->fd_table->vfs->os64fs != context->fd_table->vfs->os64fs) {
      return false;
    }
    if (stat.type == kVfsNodeTypeDirectory) {
      const size_t length = string_length(resolved);
      const char* cwd = candidate->current_working_directory;
      size_t i = 0;
      while (i < length && cwd[i] == resolved[i]) {
        ++i;
      }
      if (i == length && (cwd[i] == '\0' || cwd[i] == '/')) {
        return true;
      }
    }
    for (size_t i = 0; i < kFileDescriptorCapacity; ++i) {
      const auto& entry = candidate->fd_table->entries[i];
      if (entry.open && entry.file.handle.inode.inode_number == stat.inode_number) {
        return true;
      }
    }
    return false;
  };
  if (busy_context(context)) {
    return kSyscallUnsupported;
  }
  if (g_process_scheduler != nullptr) {
    for (const auto& process : g_process_scheduler->processes) {
      if (process.in_use && busy_context(&process.syscall_context)) {
        return kSyscallUnsupported;
      }
    }
  }
  VfsMount mount = *context->fd_table->vfs;
  return vfs_unlink(&mount, resolved) ? kSyscallOk : kSyscallIoError;
}

SyscallStatus sys_sync(SyscallContext* context) {
  if (!syscall_context_is_ready(context)) {
    return kSyscallInvalidArgument;
  }
  VfsMount mount = *context->fd_table->vfs;
  return vfs_sync(&mount) ? kSyscallOk : kSyscallIoError;
}

// 一次文件系统事务完成替换，不采用“先清空，再写入”两次独立操作。
int32_t sys_replace_file(SyscallContext* context, const char* path,
                         const void* buffer, size_t size) {
  if (!syscall_context_is_ready(context) || path == nullptr ||
      size > kMaxUserTransfer || (size != 0 && buffer == nullptr))
    return kSyscallInvalidArgument;
  char resolved[kSyscallPathCapacity];
  if (!syscall_resolve_path(context, path, resolved, sizeof(resolved)))
    return kSyscallInvalidArgument;
  VfsMount mount = *context->fd_table->vfs;
  return vfs_write_file(&mount, resolved, buffer, size)
      ? static_cast<int32_t>(size) : kSyscallIoError;
}

// brk 只改变当前进程的堆。实际页按 4 KiB 分配，break 按字节记录。
// 这是立即分配版本：没有缺页时再分配的 demand paging。
uint64_t sys_brk(uint64_t requested_break) {
  auto* const thread = scheduler_active_thread();
  auto* const process = thread != nullptr ? thread->owner : nullptr;
  if (process == nullptr || process->is_kernel_process ||
      process->user_heap_base == 0 || process->page_allocator == nullptr) return 0;
  const uint64_t old_break = process->user_heap_break;
  if (requested_break == 0 || requested_break == old_break) return old_break;
  if (requested_break < process->user_heap_base ||
      requested_break > process->user_heap_limit) return old_break;
  const auto page_end = [](uint64_t value) {
    return (value + kPageSize - 1) & ~(kPageSize - 1);
  };
  const uint64_t old_end = page_end(old_break);
  const uint64_t new_end = page_end(requested_break);
  auto* const allocator = process->page_allocator;
  auto* const space = &process->address_space;
  if (new_end > old_end) {
    uint64_t cursor = old_end;
    for (; cursor < new_end; cursor += kPageSize) {
      const uint64_t physical = alloc_page(allocator);
      void* const bytes = paging_physical_pointer(physical);
      if (physical == 0 || bytes == nullptr) {
        if (physical != 0) (void)free_page(allocator, physical);
        break;
      }
      memory_set(bytes, 0, kPageSize); // 不把上一个进程的内容交给新申请者。
      if (!address_space_map_user_page(space, allocator, cursor, physical,
                                       kPageWritable | kPageNoExecute)) {
        (void)free_page(allocator, physical);
        break;
      }
    }
    if (cursor != new_end) {
      // 中途缺内存时撤销本次映射，break 保持原值。
      while (cursor > old_end) {
        cursor -= kPageSize;
        (void)address_space_unmap_user_page(space, allocator, cursor);
      }
      return old_break;
    }
  }
  if (requested_break < old_break) {
    // 整页归还。最后一页里不再属于堆的尾巴也清零，避免再次增长时读到旧数据。
    for (uint64_t address = new_end; address < old_end; address += kPageSize)
      (void)address_space_unmap_user_page(space, allocator, address);
    if (requested_break < new_end) {
      const uint64_t physical = address_space_resolve_mapping(space, requested_break);
      void* const tail = paging_physical_pointer(physical);
      if (tail != nullptr) memory_set(tail, 0, new_end - requested_break);
    }
  }
  process->user_heap_break = requested_break;
  return requested_break;
}

extern "C" void kernel_handle_syscall(SyscallInterruptFrame* frame) {
  if (frame == nullptr) {
    return;
  }

  // 先把“这次从用户态打进来的寄存器现场”留档，
  // 这样后面就算线程在 syscall 里 yield/block，也还有机会恢复回原来的用户态位置。
  capture_current_user_trap_frame(frame);

  if (frame_came_from_user_mode(frame) && !user_syscall_arguments_valid(frame)) {
    frame->rax = encode_syscall_result(kSyscallInvalidArgument);
    return;
  }

  // `int 0x80` 现在走的是 DPL=3 的 interrupt gate。
  // 这意味着：
  // - CPU 进内核时会先自动把 IF 清掉
  // - 如果我们什么都不做，像 `read(0)` 这种“需要等键盘 IRQ 来唤醒自己”的 syscall
  //   就会看到“当前中断没开”，从而根本没法真正 block
  //
  // 所以这里做一个很保守的补丁：
  // 只有当这次 syscall 确实来自 ring3，且用户态原来的 RFLAGS 里 IF 本来就是开的，
  // 才在真正分发 syscall 期间重新开中断。
  //
  // 这样：
  // - 普通内核态测试用的 `int 0x80` 行为不变
  // - 用户态阻塞式 syscall 终于能等到外部 IRQ
  const bool should_enable_interrupts =
      frame_came_from_user_mode(frame) && ((frame->rflags & (1ULL << 9)) != 0);
  if (should_enable_interrupts) {
    enable_interrupts();
  }

  // 真正的 syscall 分发只有这一句：
  // 拆寄存器 -> 调 C++ `sys_*` -> 再把结果塞回 `frame->rax`。
  frame->rax = encode_syscall_result(
      dispatch_syscall_registers(frame->rax,
                                 frame->rdi,
                                 frame->rsi,
                                 frame->rdx,
                                 frame->rcx));

  if (should_enable_interrupts) {
    disable_interrupts();
  }
}
