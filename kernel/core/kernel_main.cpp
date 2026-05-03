#include <stddef.h>
#include <stdint.h>

#include "boot/boot_info.hpp"
#include "console/console.hpp"
#include "fs/directory.hpp"
#include "fs/fd.hpp"
#include "fs/file.hpp"
#include "fs/os64fs.hpp"
#include "fs/vfs.hpp"
#include "interrupts/interrupts.hpp"
#include "interrupts/keyboard.hpp"
#include "interrupts/pic.hpp"
#include "interrupts/pit.hpp"
#include "memory/heap.hpp"
#include "memory/kmemory.hpp"
#include "memory/page_allocator.hpp"
#include "memory/paging.hpp"
#include "runtime/runtime.hpp"
#include "shell/shell.hpp"
#include "storage/block_device.hpp"
#include "storage/boot_volume.hpp"
#include "syscall/syscall.hpp"
#include "task/scheduler.hpp"

namespace {

constexpr uint16_t kVgaColumns = 80;          // VGA 文本模式一行 80 列。
constexpr uintptr_t kVgaBase = 0xB8000;       // VGA 文本缓冲区从这个物理地址开始。
constexpr uint16_t kCom1Base = 0x3F8;         // COM1 串口的标准 I/O 端口基址。
constexpr uint8_t kStatusTextColor = 0x07;    // 浅灰字黑底，比纯白更轻，不会显得那么“顶眼”。
constexpr uint8_t kShellTextColor = 0x07;     // shell 正文也统一用浅灰色，减轻整屏发白的感觉。
constexpr uint8_t kShellPromptColor = 0x03;   // 提示符改成柔一点的青色，让交互入口更清楚但不刺眼。
constexpr uint8_t kExceptionTextColor = 0x0C; // 异常标题保留醒目的浅红色，方便一眼看出是错误路径。
constexpr uint16_t kMaxDecimalDigits = 20;    // 打印 64 位十进制时最多不会超过 20 位。
constexpr uint64_t kPagingTestVirtualAddress = 0x0000000000200000ULL;  // 2 MiB，正好落在当前临时映射之外。
constexpr uint64_t kPagingTestPattern = 0x1122334455667788ULL;         // 用一个好认的模式值验证读写。
constexpr uint64_t kHeapTestSmallPattern = 0xA1B2C3D4E5F60718ULL;      // 小块堆分配测试用的模式值。
constexpr uint64_t kHeapTestLargePattern0 = 0x0123456789ABCDEFULL;     // 大块堆分配第一页起始位置的模式值。
constexpr uint64_t kHeapTestLargePattern1 = 0xFEDCBA9876543210ULL;     // 大块堆分配跨页位置的模式值。
constexpr size_t kHeapTestSmallSize = 64;                              // 第一块堆内存，故意做得很小。
constexpr size_t kHeapTestLargeSize = 6000;                            // 第二块堆内存，故意让它跨过一个 4 KiB 页边界。
constexpr size_t kHeapReuseSize = 32;                                  // 释放小块后，再申请一个更小块，测试是否复用。
constexpr size_t kHeapMergeBlockASize = 512;                           // 合并测试里的第一个相邻块。
constexpr size_t kHeapMergeBlockBSize = 768;                           // 合并测试里的第二个相邻块。
constexpr size_t kHeapMergedRequestSize = 1024;                        // 如果 A/B 真合并了，这个请求应该能从 A 位置拿到。
constexpr size_t kKmallocTestSize = 48;                                // 先拿一块普通小内存，证明 kmalloc 这层已经能独立工作。
constexpr size_t kKcallocWordCount = 4;                                // 再拿 4 个 64 位字，验证 kcalloc 返回的区域确实全是 0。
constexpr uint64_t kKmallocTestPattern = 0x13579BDF2468ACE0ULL;        // 用一个好认的模式值验证 kmalloc 返回的块真的可写可读。
constexpr uint64_t kKernelObjectOperand = 0x1111111111111111ULL;       // 两个操作数先故意取一样，方便你一眼认出加法结果。
constexpr uint64_t kKernelObjectExpectedSum = 0x2222222222222222ULL;   // 这就是上面两个值相加后的预期结果。
constexpr uint64_t kKernelObjectCookie = 0x4B4F424A45435431ULL;        // ASCII 看起来像 "KOBJECT1"，用于校验对象真的构造过。
constexpr uint64_t kAlignedObjectMarker = 0xA55AA55AA55AA55AULL;       // 再给高对齐对象一个单独的标记值。
constexpr uint64_t kAlignedObjectAlignment = 64;                       // 故意把对象对齐拉到 64，证明 knew<T>() 不只会处理 16 字节对齐。
constexpr char kOs64FsExpectedSignature[] = "OS64FSV1";                // superblock 的前 8 字节固定就是这个签名。
constexpr char kOs64FsExpectedName[] = "os64-root";                    // 这就是脚本里写进 superblock 的卷名。
constexpr char kOs64FsExpectedReadme[] =
    "os64fs readme: the 64-bit kernel now mounts a real read-only filesystem.";
constexpr char kOs64FsExpectedGuide[] =
    "os64fs guide: stage2 only preloads raw sectors. the block device layer turns that memory into sector reads, and the filesystem layer resolves paths like docs/guide.txt inside the 64-bit kernel.";
constexpr uint32_t kPitFrequencyHz = 100;                              // 先把时钟中断频率设成 100Hz，够平滑也够好测。
constexpr uint64_t kTimerFirstLogTick = 10;                            // 先在第 10 个 tick 打一次点。
constexpr uint64_t kTimerSecondLogTick = 20;                           // 第 20 个 tick 再打一次点，证明中断在持续发生。
constexpr uint64_t kTimerWaitTestTicks = 3;                            // 第一段先直接按 tick 等 3 下。
constexpr uint64_t kTimerSleepTestMs = 50;                             // 第二段再按毫秒等 50ms，在 100Hz 下约等于 5 个 tick。
constexpr uint64_t kTimerSleepMinTicks = 5;                            // 50ms 在 100Hz 下至少应该跨过 5 个 tick。
constexpr uint32_t kSchedulerTimeSliceTicks = 1;                       // 时间片仍然故意压成 1 tick，这样线程切换痕迹最容易看出来。
constexpr uint64_t kSchedulerThreadWaitTicks = 1;                      // 需要观察 round-robin 的线程，每轮都先等 1 个 tick。
constexpr size_t kSchedulerPriorityNormalIterations = 2;               // 两个 normal 线程各跑 2 轮，方便证明同优先级里会轮转。
constexpr size_t kSchedulerSleepIterations = 2;                        // sleep 烟测里两个线程也各跑 2 轮，目标轨迹还是 ABAB。
constexpr uint64_t kSchedulerWakeThreadWaitTicks = 1;                  // waker 先等 1 个 tick，再去唤醒 blocked 线程。
constexpr char kSchedulerPriorityExpectedTrace[] = "HABABC";           // high 先跑，两个 normal 轮转，最后 background 才跑。
constexpr char kSchedulerSleepExpectedTrace[] = "ABAB";                // 两个 normal 线程各 sleep 1 tick 后，应被 idle + timer 唤醒成交替执行。
constexpr char kSchedulerBlockExpectedTrace[] = "BWb";                 // waiter 先 block，waker 唤醒后 waiter 再继续补完自己的后半段。
constexpr uint8_t kKeyboardIrqLine = 1;                                // 传统 PC 键盘走主 PIC 的 IRQ1。
constexpr uint64_t kKeyboardTestTimeoutTicks = 20;                     // 键盘测试每轮最多等 20 个 tick，避免异常时无限卡住。
constexpr uint16_t kConsoleTestStartRow = 20;                          // 新增 scheduler 状态行后，把交互测试区再往下挪一行。
constexpr size_t kConsoleLineBufferCapacity = 32;                      // 这一轮测试只读短行，32 字节足够验证流程。
constexpr uint16_t kShellTestStartRow = 20;                            // shell 也跟着下移，避免覆盖新的 `scheduler ok` 状态行。
constexpr size_t kShellLineBufferCapacity = 40;                        // 现在要测 `stat docs/guide.txt`，把命令行缓冲区稍微放大一点。
constexpr uint8_t kKeyboardTestScancodes[] = {
    0x1E,  // A 按下 -> 'a'
    0x9E,  // A 松开 -> 这一轮应忽略，不进入字符缓冲区
    0x30,  // B 按下 -> 'b'
    0x02,  // 1 按下 -> '1'
    0x39,  // Space 按下 -> ' '
    0x1C,  // Enter 按下 -> '\n'
    0x0E,  // Backspace 按下 -> '\b'
};
constexpr char kKeyboardExpectedChars[] = {
    'a',
    'b',
    '1',
    ' ',
    '\n',
    '\b',
};
constexpr uint8_t kConsoleInputTestScancodes[] = {
    0x18,  // O -> 'o'
    0x1F,  // S -> 's'
    0x39,  // Space -> ' '
    0x07,  // 6 -> '6'
    0x06,  // 5 -> '5'
    0x0E,  // Backspace -> 删除刚才的 '5'
    0x05,  // 4 -> '4'
    0x1C,  // Enter -> 结束这一行
};
constexpr char kConsoleExpectedLine[] = "os 64";                       // 这就是退格修正后的最终结果。
constexpr uint8_t kSyscallStdinScancodes[] = {
    0xE0, 0x48,  // ArrowUp -> 对第一版 stdin 字节流来说应被忽略。
    0x18,        // O -> 'o'
    0x25,        // K -> 'k'
    0x1C,        // Enter -> '\n'
};
constexpr char kSyscallStdinExpected[] = "ok\n";
constexpr uint8_t kBlockedStdinScancode = 0x1E;                      // 'a'，专门拿来测“read(0) 先 block，再被键盘 IRQ 唤醒”。
constexpr char kBlockedStdinExpected[] = "a";
constexpr uint8_t kInt80StdinScancodes[] = {
    0xE0, 0x4B,  // ArrowLeft -> 同样不应卡住 stdin 字节流读取。
    0x17,        // I -> 'i'
    0x13,        // R -> 'r'
    0x1C,        // Enter -> '\n'
};
constexpr char kInt80StdinExpected[] = "ir\n";
constexpr uint8_t kShellHelpScancodes[] = {
    0x23, 0x12, 0x26, 0x19, 0x1C,  // help + Enter
};
constexpr uint8_t kShellMemScancodes[] = {
    0x32, 0x12, 0x32, 0x1C,        // mem + Enter
};
constexpr uint8_t kShellTicksScancodes[] = {
    0x14, 0x17, 0x2E, 0x25, 0x1F, 0x1C,  // ticks + Enter
};
constexpr uint8_t kShellHeapScancodes[] = {
    0x23, 0x12, 0x1E, 0x19, 0x1C,  // heap + Enter
};
constexpr uint8_t kShellDiskScancodes[] = {
    0x20, 0x17, 0x1F, 0x25, 0x1C,  // disk + Enter
};
constexpr uint8_t kShellPwdScancodes[] = {
    0x19, 0x11, 0x20, 0x1C,        // pwd + Enter
};
constexpr uint8_t kShellCdDocsScancodes[] = {
    0x2E, 0x20, 0x39,              // cd + Space
    0x20, 0x18, 0x2E, 0x1F,        // docs
    0x1C,                          // Enter
};
constexpr uint8_t kShellLsScancodes[] = {
    0x26, 0x1F, 0x1C,              // ls + Enter
};
constexpr uint8_t kShellLsDocsScancodes[] = {
    0x26, 0x1F, 0x39,              // ls + Space
    0x20, 0x18, 0x2E, 0x1F,        // docs
    0x1C,                          // Enter
};
constexpr uint8_t kShellCatReadmeScancodes[] = {
    0x2E, 0x1E, 0x14, 0x39,        // cat + Space
    0x13, 0x12, 0x1E, 0x20, 0x32, 0x12,  // readme
    0x34,                          // .
    0x14, 0x2D, 0x14,              // txt
    0x1C,                          // Enter
};
constexpr uint8_t kShellCatAbsoluteGuideScancodes[] = {
    0x2E, 0x1E, 0x14, 0x39,        // cat + Space
    0x35,                          // /
    0x20, 0x18, 0x2E, 0x1F,        // docs
    0x35,                          // /
    0x22, 0x16, 0x17, 0x20, 0x12,  // guide
    0x34,                          // .
    0x14, 0x2D, 0x14,              // txt
    0x1C,                          // Enter
};
constexpr uint8_t kShellCatGuideScancodes[] = {
    0x2E, 0x1E, 0x14, 0x39,        // cat + Space
    0x22, 0x16, 0x17, 0x20, 0x12,  // guide
    0x34,                          // .
    0x14, 0x2D, 0x14,              // txt
    0x1C,                          // Enter
};
constexpr uint8_t kShellStatGuideScancodes[] = {
    0x1F, 0x14, 0x1E, 0x14, 0x39,  // stat + Space
    0x20, 0x18, 0x2E, 0x1F,        // docs
    0x35,                          // /
    0x22, 0x16, 0x17, 0x20, 0x12,  // guide
    0x34,                          // .
    0x14, 0x2D, 0x14,              // txt
    0x1C,                          // Enter
};
constexpr uint8_t kShellStatParentNotesScancodes[] = {
    0x1F, 0x14, 0x1E, 0x14, 0x39,  // stat + Space
    0x20, 0x18, 0x2E, 0x1F,        // docs
    0x35,                          // /
    0x34, 0x34,                    // ..
    0x35,                          // /
    0x31, 0x18, 0x14, 0x12, 0x1F,  // notes
    0x34,                          // .
    0x14, 0x2D, 0x14,              // txt
    0x1C,                          // Enter
};
constexpr uint8_t kShellIrqScancodes[] = {
    0x17, 0x13, 0x10, 0x1C,        // irq + Enter
};
constexpr uint8_t kShellBootInfoScancodes[] = {
    0x30, 0x18, 0x18, 0x14, 0x17, 0x31, 0x21, 0x18, 0x1C,  // bootinfo + Enter
};
constexpr uint8_t kShellE820Scancodes[] = {
    0x12, 0x09, 0x03, 0x0B, 0x1C,  // e820 + Enter
};
constexpr uint8_t kShellCpuScancodes[] = {
    0x2E, 0x19, 0x16, 0x1C,        // cpu + Enter
};
constexpr uint8_t kShellEditedEchoScancodes[] = {
    0x2D, 0x12, 0x2E, 0x23, 0x18, 0x39, 0x23, 0x17, 0x05, 0x03, 0x2C,
    0xE0, 0x47,                    // Home
    0xE0, 0x4D,                    // Right
    0xE0, 0x4B,                    // Left
    0xE0, 0x53,                    // Delete
    0xE0, 0x4F,                    // End
    0xE0, 0x4B,                    // Left
    0xE0, 0x4D,                    // Right
    0x0E,                          // Backspace
    0x1C,                          // Enter
};
constexpr uint8_t kShellUptimeScancodes[] = {
    0x16, 0x19, 0x14, 0x17, 0x32, 0x12, 0x1C,  // uptime + Enter
};
constexpr uint8_t kShellHistoryScancodes[] = {
    0x23, 0x17, 0x1F, 0x14, 0x18, 0x13, 0x15, 0x1C,  // history + Enter
};
constexpr uint8_t kShellHistoryBrowseScancodes[] = {
    0xE0, 0x48,                    // Up
    0xE0, 0x48,                    // Up
    0xE0, 0x50,                    // Down
    0x1C,                          // Enter
};
constexpr uint8_t kShellClearScancodes[] = {
    0x2E, 0x26, 0x12, 0x1E, 0x13, 0x1C,  // clear + Enter
};
constexpr uint8_t kShellBadScancodes[] = {
    0x30, 0x1E, 0x20, 0x1C,        // bad + Enter
};
#if defined(OS64_ENABLE_INVALID_OPCODE_SMOKE)
constexpr uint64_t kInvalidOpcodeMarker = 0x55443231494E5641ULL;       // 只是为了日志里有个好认的常量。
#endif
#if defined(OS64_ENABLE_PAGE_FAULT_SMOKE)
constexpr uint64_t kPageFaultSmokeAddress = 0x0000000000900000ULL;     // 9 MiB，这一轮没有映射它，专门拿来触发页错误。
constexpr uint64_t kPageFaultSmokePattern = 0x0BADF00DCAFED00DULL;     // 页错误测试里尝试写入的值。
#endif

PageAllocator g_page_allocator;               // 第一版物理页分配器状态先放在一个全局对象里。
KernelHeap g_kernel_heap;                     // 第一版内核堆状态也先放成全局对象，方便后面各模块共享。
SchedulerState g_scheduler;                   // 第一版调度器状态；现在先只管理内核线程，不碰用户态。
ShellState g_shell;                           // 最小 shell 的状态也先放在全局对象里，后面交互循环会一直复用。
BootVolume g_boot_volume;                     // 这一轮新增的启动卷状态，表示 stage2 预读进来的那段扇区数据。
BlockDevice g_boot_block_device;             // 再往上一层，把 boot volume 包成通用块设备接口。
Os64Fs g_os64fs;                             // 第一版只读文件系统状态也先做成全局对象。
VfsMount g_vfs;                              // VFS 根挂载点，shell 以后从这里而不是直接从 OS64FS 进入。
FileDescriptorTable g_fd_table;              // 第一版全局 fd 表；以后有进程后会变成每个进程一张表。
SyscallContext g_syscall_context;            // 第一版系统调用上下文；现在先包住全局 fd 表。
uint64_t g_kernel_object_ctor_count = 0;      // 对象层测试里一共成功调用过多少次构造函数。
uint64_t g_kernel_object_dtor_count = 0;      // 对象层测试里一共成功调用过多少次析构函数。

struct KernelObjectProbe {
  uint64_t left;       // 构造函数收到的第一个参数。
  uint64_t right;      // 构造函数收到的第二个参数。
  uint64_t sum;        // 构造时顺手把两者加起来，后面拿它验证对象内部状态。
  uint64_t cookie;     // 如果构造函数确实运行过，这里会写入固定魔数。

  KernelObjectProbe(uint64_t left_value, uint64_t right_value)
      : left(left_value),
        right(right_value),
        sum(left_value + right_value),
        cookie(kKernelObjectCookie) {
    ++g_kernel_object_ctor_count;
  }

  ~KernelObjectProbe() {
    cookie = 0;
    ++g_kernel_object_dtor_count;
  }

  bool is_valid() const {
    return left == kKernelObjectOperand &&
           right == kKernelObjectOperand &&
           sum == kKernelObjectExpectedSum &&
           cookie == kKernelObjectCookie;
  }
};

struct alignas(64) KernelAlignedObjectProbe {
  uint64_t marker;     // 这类对象主要用来验证“高对齐 + 构造函数”这条路径。
  uint64_t cookie;     // 继续沿用同一个魔数，证明这块不是只有地址对齐，构造也真的跑了。

  explicit KernelAlignedObjectProbe(uint64_t marker_value)
      : marker(marker_value),
        cookie(kKernelObjectCookie) {
    ++g_kernel_object_ctor_count;
  }

  ~KernelAlignedObjectProbe() {
    marker = 0;
    cookie = 0;
    ++g_kernel_object_dtor_count;
  }

  bool is_valid() const {
    return marker == kAlignedObjectMarker && cookie == kKernelObjectCookie;
  }
};

struct SchedulerSmokeState {
  char priority_trace[8];        // priority 烟测专用轨迹。
  size_t priority_trace_length;
  char sleep_trace[8];           // sleep + idle 烟测专用轨迹。
  size_t sleep_trace_length;
  char block_trace[8];           // block + wake 烟测专用轨迹。
  size_t block_trace_length;
};

struct SchedulerPriorityThreadContext {
  SchedulerSmokeState* shared;   // 多个 priority 线程共享同一份记录区。
  char trace_mark;               // 这个线程执行时往 priority trace 里写哪个字符。
  uint64_t observed_tid;         // 线程第一次真正跑起来后，会把自己看到的 tid 记到这里。
  size_t iterations;             // 这个线程要执行多少轮。
  uint64_t wait_ticks;           // 每轮后要不要等 1 个 tick，从而给调度器制造切换机会。
};

struct SchedulerSleepThreadContext {
  SchedulerSmokeState* shared;   // sleep 烟测的共享状态。
  char trace_mark;               // 每次醒来/运行时写入的字符。
  uint64_t observed_tid;         // 证明线程上下文里看到的 tid 确实是它自己。
  size_t iterations;             // 一共要跑几轮。
  uint64_t sleep_ticks;          // 每跑一轮后睡多久。
};

struct SchedulerBlockedThreadContext {
  SchedulerSmokeState* shared;   // block/wake 烟测的共享状态。
  uint64_t observed_tid;         // waiter 线程第一次真正跑起来时看到的 tid。
};

struct SchedulerWakeThreadContext {
  SchedulerSmokeState* shared;   // block/wake 烟测的共享状态。
  ThreadControlBlock* wake_target;  // 这个线程负责唤醒谁。
  uint64_t observed_tid;            // waker 第一次真正跑起来时看到的 tid。
  uint64_t wait_ticks;              // 先等几个 tick，再去 wake。
};

struct StdinBlockingReaderContext {
  SyscallContext* syscall_context;  // 读 stdin 还是走现有 sys_read 接口，只是现在在调度线程里调用它。
  char character;                   // 被唤醒后真正读到的那个字符。
  int32_t read_result;              // `sys_read()` 最终返回多少字节。
  uint64_t observed_tid;            // 证明 reader 线程真的跑在线程上下文里。
};

struct StdinBlockingInjectorContext {
  uint8_t scancode;                 // 要注入哪个测试扫描码。
  uint64_t observed_tid;            // 证明 injector 也是独立线程。
};

// 往 I/O 端口写一个字节。内核里没有现成库函数，所以这里自己直接发机器指令。
inline void out8(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// 从 I/O 端口读一个字节。串口状态轮询要靠它。
inline uint8_t in8(uint16_t port) {
  uint8_t value = 0;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

// 串口发送前先等发送保持寄存器空出来。
void serial_write_char(char ch) {
  while ((in8(kCom1Base + 5) & 0x20) == 0) {
  }

  out8(kCom1Base, static_cast<uint8_t>(ch));
}

// 把一个 0 结尾字符串送到串口，方便自动测试直接抓日志。
void serial_write_string(const char* text) {
  for (size_t i = 0; text[i] != '\0'; ++i) {
    serial_write_char(text[i]);
  }
}

// 换行在串口里要写 CRLF，这样终端和日志都更稳定。
void serial_write_crlf() {
  // 这里故意拆成两个单字符发送，而不是再走一遍字符串遍历。
  // 这样这一层的行为更直接，也更容易排查最早期串口日志问题。
  serial_write_char('\r');
  serial_write_char('\n');
}

// shell 的输出需要同时进 VGA 控制台和串口日志。
// 这样你手工运行时能看见提示符，自动测试时也能抓到命令结果。
void shell_output_char(char ch) {
  console_write_char(ch);

  if (ch == '\n') {
    serial_write_crlf();
    return;
  }

  serial_write_char(ch);
}

void shell_clear_output() {
  console_clear();
}

void shell_set_output_color(uint8_t color) {
  console_set_color(color);
}

size_t syscall_output_write(int32_t fd,
                            const void* buffer,
                            size_t bytes_to_write,
                            void* context) {
  (void)fd;
  (void)context;

  if (buffer == nullptr) {
    return 0;
  }

  const char* text = static_cast<const char*>(buffer);
  for (size_t i = 0; i < bytes_to_write; ++i) {
    if (console_is_initialized()) {
      console_write_char(text[i]);
    }

    if (text[i] == '\n') {
      serial_write_crlf();
      continue;
    }

    serial_write_char(text[i]);
  }

  return bytes_to_write;
}

size_t shell_history_provider_count(const void* context) {
  return shell_history_entry_count(static_cast<const ShellState*>(context));
}

const char* shell_history_provider_text(const void* context, size_t index) {
  return shell_history_entry_text(static_cast<const ShellState*>(context),
                                  index);
}

const ShellOutput kShellOutput = {
    shell_output_char,
    shell_clear_output,
    shell_set_output_color,
    kShellTextColor,
    kShellPromptColor,
};

// 打印 1 个十六进制字符，比如 10 -> 'A'。
void serial_write_hex_nibble(uint8_t value) {
  if (value < 10) {
    serial_write_char(static_cast<char>('0' + value));
    return;
  }

  serial_write_char(static_cast<char>('A' + (value - 10)));
}

// 固定打印 16 个十六进制字符，适合拿来看物理地址。
void serial_write_hex64(uint64_t value) {
  for (int shift = 60; shift >= 0; shift -= 4) {
    const uint8_t nibble = static_cast<uint8_t>((value >> shift) & 0xF);
    serial_write_hex_nibble(nibble);
  }
}

void serial_write_bounded_string(const char* text, size_t limit) {
  if (text == nullptr) {
    return;
  }

  for (size_t i = 0; i < limit && text[i] != '\0'; ++i) {
    serial_write_char(text[i]);
  }
}

// 十进制打印主要用来显示“第几条 E820 记录”和“还有多少页”。
void serial_write_u64(uint64_t value) {
  char digits[kMaxDecimalDigits];
  size_t count = 0;

  if (value == 0) {
    serial_write_char('0');
    return;
  }

  while (value != 0 && count < kMaxDecimalDigits) {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  }

  while (count > 0) {
    serial_write_char(digits[--count]);
  }
}

void serial_write_i64(int64_t value) {
  if (value < 0) {
    serial_write_char('-');
    serial_write_u64(static_cast<uint64_t>(-(value + 1)) + 1);
    return;
  }

  serial_write_u64(static_cast<uint64_t>(value));
}

bool is_aligned(uint64_t value, uint64_t alignment) {
  if (alignment == 0) {
    return false;
  }

  return (value & (alignment - 1)) == 0;
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

bool strings_equal(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return left == right;
  }

  for (size_t i = 0;; ++i) {
    if (left[i] != right[i]) {
      return false;
    }

    if (left[i] == '\0') {
      return true;
    }
  }
}

bool bounded_text_equals(const char* actual,
                         const char* expected,
                         size_t limit) {
  if (actual == nullptr || expected == nullptr) {
    return actual == expected;
  }

  for (size_t i = 0; i < limit; ++i) {
    if (expected[i] == '\0') {
      return actual[i] == '\0';
    }

    if (actual[i] != expected[i]) {
      return false;
    }
  }

  return expected[limit] == '\0';
}

// 直接往 VGA 文本缓冲区写一整行，让你在 QEMU 图形窗口里也能看到结果。
void vga_write_line(uint16_t row, const char* text, uint8_t color) {
  volatile uint16_t* const vga =
      reinterpret_cast<volatile uint16_t*>(kVgaBase);

  for (size_t col = 0; col < kVgaColumns; ++col) {
    const char ch = text[col];
    if (ch == '\0') {
      break;
    }

    vga[row * kVgaColumns + col] =
        static_cast<uint16_t>(color) << 8 | static_cast<uint8_t>(ch);
  }
}

// 既写屏幕也写串口，这样手工看和自动测都能兼顾。
void write_status_line(uint16_t row, const char* text) {
  vga_write_line(row, text, kStatusTextColor);
  serial_write_string(text);
  serial_write_crlf();
}

// 先只做最重要的判断：type=1 就当 usable，其它先统一当 reserved。
const char* memory_kind_name(uint32_t type) {
  if (type == kE820TypeUsable) {
    return "usable";
  }

  return "reserved";
}

// 判断 BootInfo 是否至少长得像我们约定的结构，避免一上来就乱解指针。
bool is_boot_info_valid(const BootInfo* boot_info) {
  return boot_info != nullptr && boot_info->magic == kBootInfoMagic &&
         boot_info->memory_map_ptr != 0 &&
         boot_info->memory_map_entry_size == sizeof(E820Entry);
}

// 一条一条把 E820 记录打印出来，让你真正看到 BIOS 给了内核什么内存地图。
void log_e820_entries(const BootInfo* boot_info) {
  const auto* entries =
      reinterpret_cast<const E820Entry*>(static_cast<uintptr_t>(boot_info->memory_map_ptr));

  for (uint16_t i = 0; i < boot_info->memory_map_count; ++i) {
    const E820Entry& entry = entries[i];

    serial_write_string("e820[");
    serial_write_u64(i);
    serial_write_string("] base=0x");
    serial_write_hex64(entry.base);
    serial_write_string(" length=0x");
    serial_write_hex64(entry.length);
    serial_write_string(" raw_type=0x");
    serial_write_hex64(entry.type);
    serial_write_string(" kind=");
    serial_write_string(memory_kind_name(entry.type));
    serial_write_crlf();
  }
}

// 把真正收进页分配器的 usable 区间再单独打印一遍。
// 这样你能看见：不是所有 E820 usable 条目都会原样进入分配器，低地址会被故意裁掉。
void log_allocator_ranges(const PageAllocator& allocator) {
  for (uint16_t i = 0; i < allocator.range_count; ++i) {
    const PageAllocatorRange& range = allocator.ranges[i];
    serial_write_string("allocator_range[");
    serial_write_u64(i);
    serial_write_string("] start=0x");
    serial_write_hex64(range.next_free);
    serial_write_string(" end=0x");
    serial_write_hex64(range.limit);
    serial_write_crlf();
  }
}

// 先试着连续拿 3 个页，让结果一眼可见。
void log_allocated_pages(PageAllocator* allocator) {
  for (uint16_t i = 0; i < 3; ++i) {
    const uint64_t page = alloc_page(allocator);
    serial_write_string("alloc_page_");
    serial_write_u64(i);
    serial_write_string("=0x");
    serial_write_hex64(page);
    serial_write_crlf();
  }
}

// 这一步才是真正把“物理页分配器”和“页表管理器”接起来：
// 先拿到一个物理页，再把它映射到一个新的虚拟地址，然后实际写进去读出来。
bool run_paging_smoke_test(PageAllocator* allocator) {
  if (allocator == nullptr) {
    return false;
  }

  const uint64_t mapped_physical_page = alloc_page(allocator);
  if (mapped_physical_page == 0) {
    return false;
  }

  serial_write_string("mapped_test_page=0x");
  serial_write_hex64(mapped_physical_page);
  serial_write_crlf();

  if (!map_page(allocator, kPagingTestVirtualAddress, mapped_physical_page,
                kPageWritable)) {
    return false;
  }

  serial_write_string("mapped_virtual=0x");
  serial_write_hex64(kPagingTestVirtualAddress);
  serial_write_crlf();

  auto* const mapped_value =
      reinterpret_cast<volatile uint64_t*>(static_cast<uintptr_t>(kPagingTestVirtualAddress));
  *mapped_value = kPagingTestPattern;
  const uint64_t read_back = *mapped_value;

  serial_write_string("read_back_value=0x");
  serial_write_hex64(read_back);
  serial_write_crlf();

  return read_back == kPagingTestPattern;
}

// 这里用两次堆分配证明两件事：
// 1. 小块对象已经能落在新的堆虚拟地址上
// 2. 大块对象已经能跨页工作，而不是只会在单页里凑巧成功
bool run_heap_smoke_test(KernelHeap* heap) {
  if (heap == nullptr) {
    return false;
  }

  auto* const small_block =
      static_cast<uint64_t*>(heap_alloc(heap, kHeapTestSmallSize, 16));
  if (small_block == nullptr) {
    return false;
  }

  serial_write_string("heap_small=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(small_block));
  serial_write_crlf();

  auto* const large_block =
      static_cast<uint8_t*>(heap_alloc(heap, kHeapTestLargeSize,
                                       kPagingPageSize));
  if (large_block == nullptr) {
    return false;
  }

  serial_write_string("heap_large=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(large_block));
  serial_write_crlf();

  serial_write_string("heap_large_page_aligned=");
  serial_write_u64(is_aligned(reinterpret_cast<uint64_t>(large_block),
                              kPagingPageSize)
                       ? 1
                       : 0);
  serial_write_crlf();

  auto* const large_first_word =
      reinterpret_cast<uint64_t*>(large_block);
  auto* const large_cross_page_word =
      reinterpret_cast<uint64_t*>(large_block + kPagingPageSize);

  *small_block = kHeapTestSmallPattern;
  *large_first_word = kHeapTestLargePattern0;
  *large_cross_page_word = kHeapTestLargePattern1;

  const uint64_t small_read_back = *small_block;
  const uint64_t cross_page_read_back = *large_cross_page_word;

  serial_write_string("heap_cross_page_value=0x");
  serial_write_hex64(cross_page_read_back);
  serial_write_crlf();

  if (!heap_free(heap, small_block)) {
    return false;
  }

  auto* const reused_small =
      static_cast<uint64_t*>(heap_alloc(heap, kHeapReuseSize, 16));
  if (reused_small == nullptr) {
    return false;
  }

  serial_write_string("heap_reuse=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(reused_small));
  serial_write_crlf();

  auto* const merge_block_a =
      static_cast<uint8_t*>(heap_alloc(heap, kHeapMergeBlockASize, 16));
  auto* const merge_block_b =
      static_cast<uint8_t*>(heap_alloc(heap, kHeapMergeBlockBSize, 16));
  if (merge_block_a == nullptr || merge_block_b == nullptr) {
    return false;
  }

  if (!heap_free(heap, merge_block_a) || !heap_free(heap, merge_block_b)) {
    return false;
  }

  auto* const merged_block =
      static_cast<uint8_t*>(heap_alloc(heap, kHeapMergedRequestSize, 16));
  if (merged_block == nullptr) {
    return false;
  }

  serial_write_string("heap_coalesced=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(merged_block));
  serial_write_crlf();

  serial_write_string("heap_used_bytes=");
  serial_write_u64(heap_used_bytes(heap));
  serial_write_crlf();

  serial_write_string("heap_mapped_bytes=");
  serial_write_u64(heap_mapped_bytes(heap));
  serial_write_crlf();

  serial_write_string("heap_free_bytes=");
  serial_write_u64(heap_free_bytes(heap));
  serial_write_crlf();

  return small_read_back == kHeapTestSmallPattern &&
         cross_page_read_back == kHeapTestLargePattern1 &&
         is_aligned(reinterpret_cast<uint64_t>(large_block), kPagingPageSize) &&
         reused_small == small_block && merged_block == merge_block_a;
}

// 这一轮把“原始堆分配”再向上抬一层：
// 现在不只要验证 heap_alloc()/heap_free()，
// 还要验证 kmalloc/kcalloc，以及 C++ 对象真的能通过 knew/kdelete 完整走完构造和析构。
bool run_kernel_memory_smoke_test(PageAllocator* allocator,
                                  KernelHeap* heap) {
  if (allocator == nullptr || heap == nullptr) {
    return false;
  }

  if (!initialize_kernel_memory_system(allocator, heap) ||
      !kernel_memory_system_ready()) {
    return false;
  }

  serial_write_string("kernel_memory_page_allocator=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(kernel_memory_page_allocator()));
  serial_write_crlf();

  serial_write_string("kernel_memory_heap=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(kernel_memory_heap()));
  serial_write_crlf();

  const uint64_t active_before = heap_active_allocations(heap);
  const uint64_t total_before = heap_total_allocations(heap);
  g_kernel_object_ctor_count = 0;
  g_kernel_object_dtor_count = 0;

  auto* const kmalloc_block =
      static_cast<uint64_t*>(kmalloc(kKmallocTestSize));
  if (kmalloc_block == nullptr) {
    return false;
  }

  serial_write_string("kmalloc_block=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(kmalloc_block));
  serial_write_crlf();

  *kmalloc_block = kKmallocTestPattern;
  const uint64_t kmalloc_read_back = *kmalloc_block;

  auto* const kcalloc_block = static_cast<uint64_t*>(
      kcalloc(kKcallocWordCount, sizeof(uint64_t)));
  if (kcalloc_block == nullptr) {
    return false;
  }

  serial_write_string("kcalloc_block=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(kcalloc_block));
  serial_write_crlf();

  uint64_t zero_word_count = 0;
  for (size_t i = 0; i < kKcallocWordCount; ++i) {
    if (kcalloc_block[i] == 0) {
      ++zero_word_count;
    }
  }

  serial_write_string("kcalloc_zero_words=");
  serial_write_u64(zero_word_count);
  serial_write_crlf();

  auto* const object =
      knew<KernelObjectProbe>(kKernelObjectOperand, kKernelObjectOperand);
  if (object == nullptr) {
    return false;
  }

  serial_write_string("kobject_ptr=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(object));
  serial_write_crlf();

  serial_write_string("kobject_sum=0x");
  serial_write_hex64(object->sum);
  serial_write_crlf();

  auto* const aligned_object =
      knew<KernelAlignedObjectProbe>(kAlignedObjectMarker);
  if (aligned_object == nullptr) {
    return false;
  }

  serial_write_string("kobject_aligned_ptr=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(aligned_object));
  serial_write_crlf();

  const bool object_valid = object->is_valid();
  const bool aligned_object_valid = aligned_object->is_valid();
  const bool aligned_ok = is_aligned(
      reinterpret_cast<uint64_t>(aligned_object), kAlignedObjectAlignment);

  serial_write_string("kobject_alignment=");
  serial_write_u64(aligned_ok ? kAlignedObjectAlignment : 0);
  serial_write_crlf();

  const bool freed_aligned_object = kdelete(aligned_object);
  const bool freed_object = kdelete(object);
  const bool freed_kcalloc_block = kfree(kcalloc_block);
  const bool freed_kmalloc_block = kfree(kmalloc_block);

  const uint64_t active_after = heap_active_allocations(heap);
  const uint64_t total_after = heap_total_allocations(heap);

  serial_write_string("kobject_ctor_count=");
  serial_write_u64(g_kernel_object_ctor_count);
  serial_write_crlf();

  serial_write_string("kobject_dtor_count=");
  serial_write_u64(g_kernel_object_dtor_count);
  serial_write_crlf();

  serial_write_string("kernel_memory_active_before=");
  serial_write_u64(active_before);
  serial_write_crlf();

  serial_write_string("kernel_memory_active_after=");
  serial_write_u64(active_after);
  serial_write_crlf();

  return kernel_memory_page_allocator() == allocator &&
         kernel_memory_heap() == heap &&
         kmalloc_read_back == kKmallocTestPattern &&
         zero_word_count == kKcallocWordCount &&
         object_valid &&
         aligned_object_valid &&
         aligned_ok &&
         freed_aligned_object &&
         freed_object &&
         freed_kcalloc_block &&
         freed_kmalloc_block &&
         g_kernel_object_ctor_count == 2 &&
         g_kernel_object_dtor_count == 2 &&
         active_after == active_before &&
         total_after == total_before + 4 &&
         heap_failed_allocations(heap) == 0;
}

bool read_inode_text(const Os64Fs* filesystem,
                     const Os64FsInode* inode,
                     char* buffer,
                     size_t capacity) {
  if (filesystem == nullptr || inode == nullptr || buffer == nullptr ||
      capacity == 0 || inode->size_bytes >= capacity) {
    return false;
  }

  if (!os64fs_read_inode_data(filesystem, inode, 0, buffer,
                              inode->size_bytes)) {
    return false;
  }

  buffer[inode->size_bytes] = '\0';
  return true;
}

// 先把 stage2 预读进来的连续扇区区间接进内核。
// 这一层还不谈目录和文件，它只负责证明：
// “64 位内核现在已经有一条稳定的按扇区读取路径”。
bool run_boot_volume_smoke_test(const BootInfo* boot_info,
                                BootVolume* volume,
                                BlockDevice* device) {
  if (boot_info == nullptr || volume == nullptr || device == nullptr) {
    return false;
  }

  if (!initialize_boot_volume(volume, boot_info) ||
      !boot_volume_is_ready(volume) ||
      !initialize_block_device_from_boot_volume(device, volume) ||
      !block_device_is_ready(device)) {
    return false;
  }

  uint8_t sector0[kBootVolumeSectorSize];
  if (!block_device_read_sector(device, 0, sector0, sizeof(sector0))) {
    return false;
  }
  uint64_t sector0_prefix = 0;
  memory_copy(&sector0_prefix, sector0, sizeof(sector0_prefix));

  serial_write_string("boot_volume_ptr=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(volume->base));
  serial_write_crlf();

  serial_write_string("boot_volume_start_lba=");
  serial_write_u64(volume->start_lba);
  serial_write_crlf();

  serial_write_string("boot_volume_sector_count=");
  serial_write_u64(device->sector_count);
  serial_write_crlf();

  serial_write_string("boot_volume_sector_size=");
  serial_write_u64(device->sector_size);
  serial_write_crlf();

  serial_write_string("block_device_total_bytes=");
  serial_write_u64(block_device_total_bytes(device));
  serial_write_crlf();

  serial_write_string("block_device_sector0_prefix=0x");
  serial_write_hex64(sector0_prefix);
  serial_write_crlf();

  return volume->start_lba == device->start_lba &&
         volume->sector_count == device->sector_count &&
         volume->sector_size == device->sector_size &&
         block_device_total_bytes(device) ==
             boot_volume_total_bytes(volume);
}

// 这一层才真正开始把“扇区”解释成“文件系统”。
// 也就是：
// sector 0 -> superblock
// sector 1 -> inode table
// sector 2+ -> 数据区
bool run_filesystem_smoke_test(const BlockDevice* device,
                               Os64Fs* filesystem) {
  if (device == nullptr || filesystem == nullptr) {
    return false;
  }

  if (!initialize_os64fs(filesystem, device) ||
      !os64fs_is_mounted(filesystem)) {
    return false;
  }

  const Os64FsSuperblock* const superblock = os64fs_superblock(filesystem);
  if (superblock == nullptr) {
    return false;
  }

  Os64FsInode root_inode;
  Os64FsInode readme_inode;
  Os64FsInode notes_inode;
  Os64FsInode docs_inode;
  Os64FsInode guide_inode;
  if (!os64fs_lookup_path(filesystem, "/", &root_inode) ||
      !os64fs_lookup_path(filesystem, "readme.txt", &readme_inode) ||
      !os64fs_lookup_path(filesystem, "./readme.txt", &readme_inode) ||
      !os64fs_lookup_path(filesystem, "docs", &docs_inode) ||
      !os64fs_lookup_path(filesystem, "docs/guide.txt", &guide_inode) ||
      !os64fs_lookup_path(filesystem, "/docs/guide.txt", &guide_inode) ||
      !os64fs_lookup_path(filesystem, "docs/../notes.txt", &notes_inode)) {
    return false;
  }

  char readme_text[256];
  char guide_text[256];
  if (!read_inode_text(filesystem, &readme_inode, readme_text,
                       sizeof(readme_text)) ||
      !read_inode_text(filesystem, &guide_inode, guide_text,
                       sizeof(guide_text))) {
    return false;
  }

  serial_write_string("os64fs_signature=");
  serial_write_bounded_string(superblock->signature,
                              sizeof(superblock->signature));
  serial_write_crlf();

  serial_write_string("os64fs_volume_name=");
  serial_write_bounded_string(superblock->volume_name,
                              sizeof(superblock->volume_name));
  serial_write_crlf();

  serial_write_string("os64fs_inode_count=");
  serial_write_u64(superblock->inode_count);
  serial_write_crlf();

  serial_write_string("os64fs_data_block_size=");
  serial_write_u64(superblock->data_block_size);
  serial_write_crlf();

  serial_write_string("os64fs_root_entries=");
  serial_write_u64(os64fs_directory_entry_count(filesystem, &root_inode));
  serial_write_crlf();

  serial_write_string("os64fs_docs_entries=");
  serial_write_u64(os64fs_directory_entry_count(filesystem, &docs_inode));
  serial_write_crlf();

  serial_write_string("os64fs_parent_lookup_inode=");
  serial_write_u64(notes_inode.inode_number);
  serial_write_crlf();

  serial_write_string("os64fs_readme=");
  serial_write_string(readme_text);
  serial_write_crlf();

  serial_write_string("os64fs_guide=");
  serial_write_string(guide_text);
  serial_write_crlf();

  return bounded_text_equals(superblock->signature,
                             kOs64FsExpectedSignature,
                             sizeof(superblock->signature)) &&
         strings_equal(superblock->volume_name, kOs64FsExpectedName) &&
         superblock->version == kOs64FsVersion &&
         superblock->total_sectors == device->sector_count &&
         superblock->inode_size == sizeof(Os64FsInode) &&
         superblock->root_inode == 1 &&
         superblock->data_block_size == 128 &&
         root_inode.type == kOs64FsTypeDirectory &&
         os64fs_directory_entry_count(filesystem, &root_inode) == 3 &&
         docs_inode.type == kOs64FsTypeDirectory &&
         os64fs_directory_entry_count(filesystem, &docs_inode) == 1 &&
         readme_inode.type == kOs64FsTypeFile &&
         notes_inode.type == kOs64FsTypeFile &&
         notes_inode.inode_number == 3 &&
         guide_inode.type == kOs64FsTypeFile &&
         guide_inode.direct_blocks[0] != kOs64FsInvalidBlockIndex &&
         guide_inode.direct_blocks[1] != kOs64FsInvalidBlockIndex &&
         strings_equal(readme_text, kOs64FsExpectedReadme) &&
         strings_equal(guide_text, kOs64FsExpectedGuide);
}

// OS64FS 是“底层文件系统格式层”，它关心 inode、direct block、目录项。
// FileHandle 是“上层打开文件层”，它关心 open/read/close/stat。
// 这两个层次分开后，shell 以后就不需要直接碰磁盘 inode 格式。
bool run_file_handle_smoke_test(const Os64Fs* filesystem) {
  if (!os64fs_is_mounted(filesystem)) {
    return false;
  }

  FileHandle readme_handle;
  if (!file_open(filesystem, "readme.txt", &readme_handle)) {
    return false;
  }

  serial_write_string("file_open ok");
  serial_write_crlf();

  FileStat readme_stat;
  if (!file_handle_stat(&readme_handle, &readme_stat) ||
      readme_stat.type != kOs64FsTypeFile) {
    (void)file_close(&readme_handle);
    return false;
  }

  uint8_t first_chunk[8];
  const size_t first_read =
      file_read(&readme_handle, first_chunk, sizeof(first_chunk));
  if (first_read != sizeof(first_chunk) ||
      !file_seek(&readme_handle, 0) ||
      file_tell(&readme_handle) != 0) {
    (void)file_close(&readme_handle);
    return false;
  }

  const size_t expected_readme_length = string_length(kOs64FsExpectedReadme);
  char readme_text[256];
  if (expected_readme_length >= sizeof(readme_text)) {
    (void)file_close(&readme_handle);
    return false;
  }

  size_t total_read = 0;
  while (total_read < expected_readme_length) {
    const size_t bytes_read =
        file_read(&readme_handle, readme_text + total_read, 13);
    if (bytes_read == 0) {
      (void)file_close(&readme_handle);
      return false;
    }

    total_read += bytes_read;
  }

  readme_text[total_read] = '\0';

  uint8_t eof_byte = 0;
  const size_t eof_read = file_read(&readme_handle, &eof_byte, 1);

  FileStat guide_stat;
  FileStat docs_stat;
  const bool stat_ok =
      file_stat(filesystem, "/docs/guide.txt", &guide_stat) &&
      file_stat(filesystem, "docs", &docs_stat);

  const bool close_ok = file_close(&readme_handle);

  serial_write_string("file_read_total=");
  serial_write_u64(total_read);
  serial_write_crlf();

  serial_write_string("file_eof_read=");
  serial_write_u64(eof_read);
  serial_write_crlf();

  serial_write_string("file_stat_inode=");
  serial_write_u64(stat_ok ? guide_stat.inode_number : 0);
  serial_write_crlf();

  const bool ok =
      close_ok &&
      stat_ok &&
      total_read == expected_readme_length &&
      eof_read == 0 &&
      readme_stat.size_bytes == expected_readme_length &&
      strings_equal(readme_text, kOs64FsExpectedReadme) &&
      guide_stat.inode_number == 5 &&
      guide_stat.type == kOs64FsTypeFile &&
      guide_stat.direct_blocks[0] == 4 &&
      guide_stat.direct_blocks[1] == 5 &&
      docs_stat.type == kOs64FsTypeDirectory;

  if (ok) {
    serial_write_string("file_layer ok");
    serial_write_crlf();
  }

  return ok;
}

// DirectoryHandle 是目录版的 FileHandle。
// 这一层把“读目录项”的动作从 shell 里拿出来，
// 以后 `ls` 就不需要直接知道 Os64FsDirEntry 的底层布局。
bool run_directory_handle_smoke_test(const Os64Fs* filesystem) {
  if (!os64fs_is_mounted(filesystem)) {
    return false;
  }

  DirectoryHandle root_handle;
  if (!directory_open(filesystem, "/", &root_handle)) {
    return false;
  }

  serial_write_string("directory_open ok");
  serial_write_crlf();

  const uint32_t root_entry_count =
      directory_entry_count(&root_handle);

  serial_write_string("directory_root_entries=");
  serial_write_u64(root_entry_count);
  serial_write_crlf();

  DirectoryEntry readme_entry;
  DirectoryEntry notes_entry;
  DirectoryEntry docs_entry;
  const bool root_read_ok =
      directory_read(&root_handle, &readme_entry) &&
      directory_read(&root_handle, &notes_entry) &&
      directory_read(&root_handle, &docs_entry);

  const uint32_t root_read_count = directory_tell(&root_handle);
  const bool root_eof_ok =
      root_read_ok &&
      !directory_read(&root_handle, &docs_entry) &&
      directory_tell(&root_handle) == root_read_count;

  const bool rewind_ok =
      directory_rewind(&root_handle) &&
      directory_tell(&root_handle) == 0;

  serial_write_string("directory_read_count=");
  serial_write_u64(root_read_count);
  serial_write_crlf();

  serial_write_string("directory_rewind_index=");
  serial_write_u64(directory_tell(&root_handle));
  serial_write_crlf();

  const bool close_root_ok = directory_close(&root_handle);

  DirectoryHandle docs_handle;
  DirectoryEntry guide_entry;
  const bool docs_open_ok =
      directory_open(filesystem, "docs", &docs_handle);
  const bool docs_read_ok =
      docs_open_ok &&
      directory_entry_count(&docs_handle) == 1 &&
      directory_read(&docs_handle, &guide_entry);
  const bool close_docs_ok =
      docs_open_ok ? directory_close(&docs_handle) : false;

  serial_write_string("directory_docs_first_inode=");
  serial_write_u64(docs_read_ok ? guide_entry.inode_number : 0);
  serial_write_crlf();

  const bool ok =
      root_entry_count == 3 &&
      root_read_ok &&
      root_eof_ok &&
      rewind_ok &&
      close_root_ok &&
      docs_read_ok &&
      close_docs_ok &&
      readme_entry.inode_number == 2 &&
      readme_entry.type == kOs64FsTypeFile &&
      strings_equal(readme_entry.name, "readme.txt") &&
      notes_entry.inode_number == 3 &&
      notes_entry.type == kOs64FsTypeFile &&
      strings_equal(notes_entry.name, "notes.txt") &&
      docs_entry.inode_number == 4 &&
      docs_entry.type == kOs64FsTypeDirectory &&
      strings_equal(docs_entry.name, "docs") &&
      guide_entry.inode_number == 5 &&
      guide_entry.type == kOs64FsTypeFile &&
      strings_equal(guide_entry.name, "guide.txt");

  if (ok) {
    serial_write_string("directory_layer ok");
    serial_write_crlf();
  }

  return ok;
}

// VFS 是“具体文件系统”之上的统一入口。
// 这一版 VFS 只挂载一个 OS64FS，但 shell 以后已经可以先依赖 vfs_* 接口。
bool run_vfs_smoke_test(VfsMount* mount, const Os64Fs* filesystem) {
  if (mount == nullptr || !initialize_vfs(mount, filesystem) ||
      !vfs_is_mounted(mount)) {
    return false;
  }

  serial_write_string("vfs_mount ok");
  serial_write_crlf();

  VfsStat readme_stat;
  VfsStat guide_stat;
  VfsStat docs_stat;
  const bool stat_ok =
      vfs_stat(mount, "readme.txt", &readme_stat) &&
      vfs_stat(mount, "/docs/guide.txt", &guide_stat) &&
      vfs_stat(mount, "docs", &docs_stat);

  serial_write_string("vfs_stat_inode=");
  serial_write_u64(stat_ok ? guide_stat.inode_number : 0);
  serial_write_crlf();

  VfsFile readme_file;
  if (!stat_ok ||
      readme_stat.type != kVfsNodeTypeFile ||
      guide_stat.type != kVfsNodeTypeFile ||
      docs_stat.type != kVfsNodeTypeDirectory ||
      !vfs_open_file(mount, "readme.txt", &readme_file)) {
    return false;
  }

  const size_t expected_readme_length = string_length(kOs64FsExpectedReadme);
  char readme_text[256];
  if (expected_readme_length >= sizeof(readme_text)) {
    (void)vfs_close_file(&readme_file);
    return false;
  }

  size_t total_read = 0;
  while (total_read < expected_readme_length) {
    const size_t bytes_read =
        vfs_read_file(&readme_file, readme_text + total_read, 17);
    if (bytes_read == 0) {
      (void)vfs_close_file(&readme_file);
      return false;
    }

    total_read += bytes_read;
  }
  readme_text[total_read] = '\0';

  uint8_t eof_byte = 0;
  const size_t eof_read = vfs_read_file(&readme_file, &eof_byte, 1);
  const bool close_file_ok = vfs_close_file(&readme_file);

  serial_write_string("vfs_file_read_total=");
  serial_write_u64(total_read);
  serial_write_crlf();

  VfsDirectory root_directory;
  if (!vfs_open_directory(mount, "/", &root_directory)) {
    return false;
  }

  const uint32_t root_entry_count =
      vfs_directory_entry_count(&root_directory);
  VfsDirectoryEntry first_entry;
  const bool first_entry_ok =
      vfs_read_directory(&root_directory, &first_entry);
  const bool close_directory_ok = vfs_close_directory(&root_directory);

  serial_write_string("vfs_directory_entries=");
  serial_write_u64(root_entry_count);
  serial_write_crlf();

  serial_write_string("vfs_directory_first_inode=");
  serial_write_u64(first_entry_ok ? first_entry.inode_number : 0);
  serial_write_crlf();

  const bool ok =
      close_file_ok &&
      close_directory_ok &&
      total_read == expected_readme_length &&
      eof_read == 0 &&
      strings_equal(readme_text, kOs64FsExpectedReadme) &&
      guide_stat.inode_number == 5 &&
      guide_stat.direct_blocks[0] == 4 &&
      guide_stat.direct_blocks[1] == 5 &&
      docs_stat.type == kVfsNodeTypeDirectory &&
      root_entry_count == 3 &&
      first_entry_ok &&
      first_entry.inode_number == 2 &&
      first_entry.type == kVfsNodeTypeFile &&
      strings_equal(first_entry.name, "readme.txt");

  if (ok) {
    serial_write_string("vfs_layer ok");
    serial_write_crlf();
  }

  return ok;
}

// fd 是“文件描述符”的缩写。
// 上层以后不需要直接保存 VfsFile，而是保存一个小整数：
// fd_open("readme.txt") -> 0
// fd_read(0, ...)       -> 从 0 号槽位背后的文件读取
// 这就是后面系统调用 read/write/open/close 的基础形状。
bool run_file_descriptor_smoke_test(FileDescriptorTable* table,
                                    const VfsMount* vfs) {
  if (table == nullptr ||
      !initialize_file_descriptor_table(table, vfs) ||
      !file_descriptor_table_is_ready(table)) {
    return false;
  }

  serial_write_string("fd_table ok");
  serial_write_crlf();

  const int32_t readme_fd = fd_open(table, "readme.txt");
  if (readme_fd == kInvalidFileDescriptor) {
    return false;
  }

  serial_write_string("fd_open=");
  serial_write_u64(static_cast<uint64_t>(readme_fd));
  serial_write_crlf();

  VfsStat readme_stat;
  if (!fd_stat(table, readme_fd, &readme_stat) ||
      readme_stat.type != kVfsNodeTypeFile) {
    (void)fd_close(table, readme_fd);
    return false;
  }

  uint8_t first_chunk[8];
  const size_t first_read =
      fd_read(table, readme_fd, first_chunk, sizeof(first_chunk));
  if (first_read != sizeof(first_chunk) ||
      !fd_seek(table, readme_fd, 0) ||
      fd_tell(table, readme_fd) != 0) {
    (void)fd_close(table, readme_fd);
    return false;
  }

  const size_t expected_readme_length = string_length(kOs64FsExpectedReadme);
  char readme_text[256];
  if (expected_readme_length >= sizeof(readme_text)) {
    (void)fd_close(table, readme_fd);
    return false;
  }

  size_t total_read = 0;
  while (total_read < expected_readme_length) {
    const size_t bytes_read =
        fd_read(table, readme_fd, readme_text + total_read, 11);
    if (bytes_read == 0) {
      (void)fd_close(table, readme_fd);
      return false;
    }

    total_read += bytes_read;
  }
  readme_text[total_read] = '\0';

  uint8_t eof_byte = 0;
  const size_t eof_read = fd_read(table, readme_fd, &eof_byte, 1);
  const bool close_ok = fd_close(table, readme_fd);
  const uint32_t open_count_after_close = fd_open_count(table);

  serial_write_string("fd_read_total=");
  serial_write_u64(total_read);
  serial_write_crlf();

  serial_write_string("fd_eof_read=");
  serial_write_u64(eof_read);
  serial_write_crlf();

  serial_write_string("fd_open_count=");
  serial_write_u64(open_count_after_close);
  serial_write_crlf();

  const bool ok =
      readme_fd == 0 &&
      close_ok &&
      open_count_after_close == 0 &&
      total_read == expected_readme_length &&
      eof_read == 0 &&
      readme_stat.size_bytes == expected_readme_length &&
      strings_equal(readme_text, kOs64FsExpectedReadme);

  if (ok) {
    serial_write_string("fd_layer ok");
    serial_write_crlf();
  }

  return ok;
}

int64_t invoke_int80_syscall(uint64_t syscall_number,
                             uint64_t argument0 = 0,
                             uint64_t argument1 = 0,
                             uint64_t argument2 = 0,
                             uint64_t argument3 = 0) {
  int64_t result;
  asm volatile(
      "int $0x80"
      : "=a"(result)
      : "a"(syscall_number),
        "D"(argument0),
        "S"(argument1),
        "d"(argument2),
        "c"(argument3)
      : "cc", "memory");
  return result;
}

// syscall 这一层先不碰 CPU 的 `syscall` 指令。
// 它的目标是把 open/read/stat/seek/close 收口成一组“像系统调用”的内核入口：
// 上层只拿 fd 和错误码，底层仍然由 fd 表、VFS、OS64FS 一层层执行真实工作。
bool run_syscall_smoke_test(SyscallContext* context,
                            FileDescriptorTable* fd_table) {
  if (context == nullptr ||
      !initialize_syscall_context(context, fd_table) ||
      !install_syscall_write_handler(context, syscall_output_write, nullptr) ||
      !syscall_context_is_ready(context)) {
    return false;
  }

  serial_write_string("syscall_context ok");
  serial_write_crlf();

  char cwd[kSyscallPathCapacity];
  if (sys_getcwd(context, cwd, sizeof(cwd)) < 0) {
    return false;
  }

  serial_write_string("sys_cwd=");
  serial_write_string(cwd);
  serial_write_crlf();

  const int32_t root_entry_count =
      sys_listdir(context, ".", nullptr, 0);
  if (root_entry_count < 0) {
    return false;
  }

  serial_write_string("sys_root_entries=");
  serial_write_u64(static_cast<uint64_t>(root_entry_count));
  serial_write_crlf();

  if (sys_chdir(context, "docs") != kSyscallOk) {
    return false;
  }

  char cwd_after_cd[kSyscallPathCapacity];
  if (sys_getcwd(context, cwd_after_cd, sizeof(cwd_after_cd)) < 0) {
    return false;
  }

  serial_write_string("sys_cwd_after_cd=");
  serial_write_string(cwd_after_cd);
  serial_write_crlf();

  VfsDirectoryEntry directory_entries[4];
  const int32_t directory_entry_count =
      sys_listdir(context, ".", directory_entries,
                  sizeof(directory_entries) / sizeof(directory_entries[0]));
  if (directory_entry_count < 0) {
    return false;
  }

  serial_write_string("sys_listdir_count=");
  serial_write_u64(static_cast<uint64_t>(directory_entry_count));
  serial_write_crlf();

  VfsStat guide_path_stat;
  if (sys_stat_path(context, "guide.txt", &guide_path_stat) != kSyscallOk) {
    return false;
  }

  serial_write_string("sys_path_stat_inode=");
  serial_write_u64(guide_path_stat.inode_number);
  serial_write_crlf();

  const char* const kSysWriteStdoutMessage = "hello sys_write\n";
  const size_t sys_write_stdout_length =
      string_length(kSysWriteStdoutMessage);
  serial_write_string("sys_write_stdout_payload=");
  const int32_t stdout_write =
      sys_write(context, kSyscallStandardOutputFd,
                kSysWriteStdoutMessage, sys_write_stdout_length);
  if (stdout_write < 0) {
    return false;
  }

  const char* const kSysWriteStderrMessage = "error sys_write\n";
  const size_t sys_write_stderr_length =
      string_length(kSysWriteStderrMessage);
  serial_write_string("sys_write_stderr_payload=");
  const int32_t stderr_write =
      sys_write(context, kSyscallStandardErrorFd,
                kSysWriteStderrMessage, sys_write_stderr_length);
  if (stderr_write < 0) {
    return false;
  }

  const int32_t guide_fd = sys_open(context, "guide.txt");
  if (guide_fd < 0) {
    return false;
  }

  serial_write_string("sys_open=");
  serial_write_u64(static_cast<uint64_t>(guide_fd));
  serial_write_crlf();

  const int32_t file_write_status =
      sys_write(context, guide_fd,
                kSysWriteStdoutMessage, sys_write_stdout_length);

  VfsStat guide_stat;
  if (sys_stat(context, guide_fd, &guide_stat) != kSyscallOk ||
      guide_stat.type != kVfsNodeTypeFile) {
    (void)sys_close(context, guide_fd);
    return false;
  }

  serial_write_string("sys_stat_inode=");
  serial_write_u64(guide_stat.inode_number);
  serial_write_crlf();

  const size_t expected_guide_length = string_length(kOs64FsExpectedGuide);
  char guide_text[256];
  if (expected_guide_length >= sizeof(guide_text)) {
    (void)sys_close(context, guide_fd);
    return false;
  }

  size_t total_read = 0;
  while (total_read < expected_guide_length) {
    const int32_t bytes_read =
        sys_read(context, guide_fd, guide_text + total_read, 19);
    if (bytes_read <= 0) {
      (void)sys_close(context, guide_fd);
      return false;
    }

    total_read += static_cast<size_t>(bytes_read);
  }
  guide_text[total_read] = '\0';

  uint8_t eof_byte = 0;
  const int32_t eof_read = sys_read(context, guide_fd, &eof_byte, 1);

  const bool seek_ok = sys_seek(context, guide_fd, 0) == kSyscallOk;
  char prefix[8];
  const int32_t prefix_read =
      seek_ok ? sys_read(context, guide_fd, prefix, sizeof(prefix)) : -1;

  const SyscallStatus close_status = sys_close(context, guide_fd);
  const uint32_t open_count_after_close = fd_open_count(fd_table);
  const int32_t bad_fd_write_status =
      sys_write(context, 99,
                kSysWriteStdoutMessage, sys_write_stdout_length);

  serial_write_string("sys_write_stdout_bytes=");
  serial_write_i64(stdout_write);
  serial_write_crlf();

  serial_write_string("sys_write_stderr_bytes=");
  serial_write_i64(stderr_write);
  serial_write_crlf();

  serial_write_string("sys_write_file_status=");
  serial_write_i64(file_write_status);
  serial_write_crlf();

  serial_write_string("sys_write_bad_fd=");
  serial_write_i64(bad_fd_write_status);
  serial_write_crlf();

  serial_write_string("sys_read_total=");
  serial_write_u64(total_read);
  serial_write_crlf();

  serial_write_string("sys_eof_read=");
  serial_write_u64(static_cast<uint64_t>(eof_read));
  serial_write_crlf();

  serial_write_string("sys_open_count=");
  serial_write_u64(open_count_after_close);
  serial_write_crlf();

  const bool ok =
      stdout_write == static_cast<int32_t>(sys_write_stdout_length) &&
      stderr_write == static_cast<int32_t>(sys_write_stderr_length) &&
      guide_fd == kSyscallFirstFileFd &&
      file_write_status == kSyscallUnsupported &&
      bad_fd_write_status == kSyscallBadFileDescriptor &&
      strings_equal(cwd, "/") &&
      root_entry_count == 3 &&
      strings_equal(cwd_after_cd, "/docs") &&
      directory_entry_count == 1 &&
      strings_equal(directory_entries[0].name, "guide.txt") &&
      guide_path_stat.inode_number == 5 &&
      guide_stat.inode_number == 5 &&
      guide_stat.size_bytes == expected_guide_length &&
      total_read == expected_guide_length &&
      eof_read == 0 &&
      seek_ok &&
      prefix_read == static_cast<int32_t>(sizeof(prefix)) &&
      bounded_text_equals(prefix, "os64fs g", sizeof(prefix)) &&
      close_status == kSyscallOk &&
      open_count_after_close == 0 &&
      strings_equal(guide_text, kOs64FsExpectedGuide);

  if (ok) {
    serial_write_string("syscall_layer ok");
    serial_write_crlf();
  }

  return ok;
}

// 这一步开始让 CPU 真正打一趟 `int 0x80`，而不是只在 C++ 里直接调用 `sys_*`。
// 这样我们能验证：
// 1. IDT 里 0x80 号门已经连上
// 2. 汇编 stub 确实把寄存器保存并交给了 C++
// 3. 返回值也真的能从内核写回 RAX 再带出来
bool run_int80_syscall_smoke_test(SyscallContext* context,
                                  FileDescriptorTable* fd_table) {
  if (context == nullptr ||
      fd_table == nullptr ||
      !initialize_syscall_context(context, fd_table) ||
      !install_syscall_write_handler(context, syscall_output_write, nullptr) ||
      !install_syscall_dispatch_context(context) ||
      !syscall_dispatch_is_ready()) {
    return false;
  }

  char cwd[kSyscallPathCapacity];
  const int64_t cwd_length =
      invoke_int80_syscall(kSyscallNumberGetCwd,
                           reinterpret_cast<uint64_t>(cwd),
                           sizeof(cwd));
  if (cwd_length < 0) {
    return false;
  }

  serial_write_string("int80_cwd=");
  serial_write_string(cwd);
  serial_write_crlf();

  const int64_t chdir_status =
      invoke_int80_syscall(kSyscallNumberChdir,
                           reinterpret_cast<uint64_t>("docs"));
  if (chdir_status != kSyscallOk) {
    return false;
  }

  char cwd_after_cd[kSyscallPathCapacity];
  const int64_t cwd_after_cd_length =
      invoke_int80_syscall(kSyscallNumberGetCwd,
                           reinterpret_cast<uint64_t>(cwd_after_cd),
                           sizeof(cwd_after_cd));
  if (cwd_after_cd_length < 0) {
    return false;
  }

  serial_write_string("int80_cwd_after_cd=");
  serial_write_string(cwd_after_cd);
  serial_write_crlf();

  VfsDirectoryEntry directory_entries[4];
  const int64_t directory_entry_count =
      invoke_int80_syscall(kSyscallNumberListDir,
                           reinterpret_cast<uint64_t>("."),
                           reinterpret_cast<uint64_t>(directory_entries),
                           sizeof(directory_entries) /
                               sizeof(directory_entries[0]));
  if (directory_entry_count < 0) {
    return false;
  }

  serial_write_string("int80_listdir_count=");
  serial_write_u64(static_cast<uint64_t>(directory_entry_count));
  serial_write_crlf();

  VfsStat guide_path_stat;
  const int64_t path_stat_status =
      invoke_int80_syscall(kSyscallNumberStatPath,
                           reinterpret_cast<uint64_t>("guide.txt"),
                           reinterpret_cast<uint64_t>(&guide_path_stat));
  if (path_stat_status != kSyscallOk) {
    return false;
  }

  serial_write_string("int80_path_stat_inode=");
  serial_write_u64(guide_path_stat.inode_number);
  serial_write_crlf();

  const char* const kInt80WriteStdoutMessage = "hello int80_write\n";
  const size_t int80_write_stdout_length =
      string_length(kInt80WriteStdoutMessage);
  serial_write_string("int80_write_stdout_payload=");
  const int64_t stdout_write =
      invoke_int80_syscall(kSyscallNumberWrite,
                           static_cast<uint64_t>(kSyscallStandardOutputFd),
                           reinterpret_cast<uint64_t>(kInt80WriteStdoutMessage),
                           int80_write_stdout_length);
  if (stdout_write < 0) {
    return false;
  }

  const char* const kInt80WriteStderrMessage = "error int80_write\n";
  const size_t int80_write_stderr_length =
      string_length(kInt80WriteStderrMessage);
  serial_write_string("int80_write_stderr_payload=");
  const int64_t stderr_write =
      invoke_int80_syscall(kSyscallNumberWrite,
                           static_cast<uint64_t>(kSyscallStandardErrorFd),
                           reinterpret_cast<uint64_t>(kInt80WriteStderrMessage),
                           int80_write_stderr_length);
  if (stderr_write < 0) {
    return false;
  }

  const int64_t guide_fd =
      invoke_int80_syscall(kSyscallNumberOpen,
                           reinterpret_cast<uint64_t>("guide.txt"));
  if (guide_fd < 0) {
    return false;
  }

  serial_write_string("int80_open=");
  serial_write_u64(static_cast<uint64_t>(guide_fd));
  serial_write_crlf();

  const int64_t file_write_status =
      invoke_int80_syscall(kSyscallNumberWrite,
                           static_cast<uint64_t>(guide_fd),
                           reinterpret_cast<uint64_t>(kInt80WriteStdoutMessage),
                           int80_write_stdout_length);

  VfsStat guide_fd_stat;
  const int64_t fd_stat_status =
      invoke_int80_syscall(kSyscallNumberStat,
                           static_cast<uint64_t>(guide_fd),
                           reinterpret_cast<uint64_t>(&guide_fd_stat));
  if (fd_stat_status != kSyscallOk) {
    (void)invoke_int80_syscall(kSyscallNumberClose,
                               static_cast<uint64_t>(guide_fd));
    return false;
  }

  serial_write_string("int80_fd_stat_inode=");
  serial_write_u64(guide_fd_stat.inode_number);
  serial_write_crlf();

  const size_t expected_guide_length = string_length(kOs64FsExpectedGuide);
  char guide_text[256];
  if (expected_guide_length >= sizeof(guide_text)) {
    (void)invoke_int80_syscall(kSyscallNumberClose,
                               static_cast<uint64_t>(guide_fd));
    return false;
  }

  size_t total_read = 0;
  while (total_read < expected_guide_length) {
    const int64_t bytes_read =
        invoke_int80_syscall(kSyscallNumberRead,
                             static_cast<uint64_t>(guide_fd),
                             reinterpret_cast<uint64_t>(guide_text + total_read),
                             17);
    if (bytes_read <= 0) {
      (void)invoke_int80_syscall(kSyscallNumberClose,
                                 static_cast<uint64_t>(guide_fd));
      return false;
    }

    total_read += static_cast<size_t>(bytes_read);
  }
  guide_text[total_read] = '\0';

  uint8_t eof_byte = 0;
  const int64_t eof_read =
      invoke_int80_syscall(kSyscallNumberRead,
                           static_cast<uint64_t>(guide_fd),
                           reinterpret_cast<uint64_t>(&eof_byte),
                           1);

  const int64_t seek_status =
      invoke_int80_syscall(kSyscallNumberSeek,
                           static_cast<uint64_t>(guide_fd),
                           0);

  char prefix[8];
  const int64_t prefix_read =
      seek_status == kSyscallOk
          ? invoke_int80_syscall(kSyscallNumberRead,
                                 static_cast<uint64_t>(guide_fd),
                                 reinterpret_cast<uint64_t>(prefix),
                                 sizeof(prefix))
          : -1;

  const int64_t close_status =
      invoke_int80_syscall(kSyscallNumberClose,
                           static_cast<uint64_t>(guide_fd));
  const uint32_t open_count_after_close = fd_open_count(fd_table);
  const int64_t bad_fd_write_status =
      invoke_int80_syscall(kSyscallNumberWrite,
                           99,
                           reinterpret_cast<uint64_t>(kInt80WriteStdoutMessage),
                           int80_write_stdout_length);

  serial_write_string("int80_write_stdout_bytes=");
  serial_write_i64(stdout_write);
  serial_write_crlf();

  serial_write_string("int80_write_stderr_bytes=");
  serial_write_i64(stderr_write);
  serial_write_crlf();

  serial_write_string("int80_write_file_status=");
  serial_write_i64(file_write_status);
  serial_write_crlf();

  serial_write_string("int80_write_bad_fd=");
  serial_write_i64(bad_fd_write_status);
  serial_write_crlf();

  serial_write_string("int80_read_total=");
  serial_write_u64(total_read);
  serial_write_crlf();

  serial_write_string("int80_eof_read=");
  serial_write_u64(static_cast<uint64_t>(eof_read));
  serial_write_crlf();

  serial_write_string("int80_open_count=");
  serial_write_u64(open_count_after_close);
  serial_write_crlf();

  const int64_t bad_syscall_result = invoke_int80_syscall(99);

  serial_write_string("int80_bad_result=0x");
  serial_write_hex64(static_cast<uint64_t>(bad_syscall_result));
  serial_write_crlf();

  const bool ok =
      cwd_length == 1 &&
      strings_equal(cwd, "/") &&
      chdir_status == kSyscallOk &&
      cwd_after_cd_length == 5 &&
      strings_equal(cwd_after_cd, "/docs") &&
      directory_entry_count == 1 &&
      strings_equal(directory_entries[0].name, "guide.txt") &&
      path_stat_status == kSyscallOk &&
      stdout_write == static_cast<int64_t>(int80_write_stdout_length) &&
      stderr_write == static_cast<int64_t>(int80_write_stderr_length) &&
      guide_path_stat.inode_number == 5 &&
      guide_fd == kSyscallFirstFileFd &&
      file_write_status == kSyscallUnsupported &&
      fd_stat_status == kSyscallOk &&
      guide_fd_stat.inode_number == 5 &&
      guide_fd_stat.size_bytes == expected_guide_length &&
      total_read == expected_guide_length &&
      eof_read == 0 &&
      seek_status == kSyscallOk &&
      prefix_read == static_cast<int64_t>(sizeof(prefix)) &&
      bounded_text_equals(prefix, "os64fs g", sizeof(prefix)) &&
      close_status == kSyscallOk &&
      open_count_after_close == 0 &&
      bad_fd_write_status == kSyscallBadFileDescriptor &&
      bad_syscall_result == kSyscallInvalidArgument &&
      strings_equal(guide_text, kOs64FsExpectedGuide);

  if (ok) {
    serial_write_string("int80_syscall ok");
    serial_write_crlf();
  }

  return ok;
}

bool run_timer_smoke_test() {
  // 先初始化 PIC，让外部硬件中断有地方可去，并避开 CPU 异常向量区。
  if (!initialize_pic()) {
    return false;
  }

  serial_write_string("pic ok");
  serial_write_crlf();

  // 再初始化 PIT，让 IRQ0 真的开始按固定频率发生。
  if (!initialize_pit(kPitFrequencyHz)) {
    return false;
  }

  serial_write_string("pit ok");
  serial_write_crlf();

  serial_write_string("pit_frequency_hz=");
  serial_write_u64(timer_frequency_hz());
  serial_write_crlf();

  enable_interrupts();   // 到这里才真正允许外部硬件中断进来。

  bool logged_first_tick = false;
  bool logged_second_tick = false;

  while (!logged_second_tick) {
    // 不断看“全局 tick 计数器”现在走到了几。
    const uint64_t ticks = timer_tick_count();

    if (!logged_first_tick && ticks >= kTimerFirstLogTick) {
      serial_write_string("timer_tick=");
      serial_write_u64(ticks);
      serial_write_crlf();
      logged_first_tick = true;
    }

    if (ticks >= kTimerSecondLogTick) {
      serial_write_string("timer_tick=");
      serial_write_u64(ticks);
      serial_write_crlf();
      logged_second_tick = true;
      break;                        // 看到第 20 个 tick 就够证明这条 IRQ 链已经持续工作。
    }

    wait_for_interrupt();  // 没有到目标 tick 之前，就先让 CPU 睡到下一次中断再醒。
  }

  // 第一段：直接按 tick 等待，证明“系统已经能靠中断睡到未来某个 tick 再醒”。
  const uint64_t wait_start_tick = timer_tick_count();
  timer_wait_ticks(kTimerWaitTestTicks);
  const uint64_t wait_end_tick = timer_tick_count();
  const uint64_t wait_elapsed_ticks = wait_end_tick - wait_start_tick;

  serial_write_string("timer_wait_elapsed_ticks=");
  serial_write_u64(wait_elapsed_ticks);
  serial_write_crlf();

  // 第二段：再给一个更像用户视角的接口，按毫秒等待。
  const uint64_t sleep_start_tick = timer_tick_count();
  if (!timer_sleep_ms(kTimerSleepTestMs)) {
    disable_interrupts();
    return false;
  }
  const uint64_t sleep_end_tick = timer_tick_count();
  const uint64_t sleep_elapsed_ticks = sleep_end_tick - sleep_start_tick;

  serial_write_string("timer_sleep_ms=");
  serial_write_u64(kTimerSleepTestMs);
  serial_write_crlf();

  serial_write_string("timer_sleep_elapsed_ticks=");
  serial_write_u64(sleep_elapsed_ticks);
  serial_write_crlf();

  disable_interrupts();             // 测试结束先关掉中断，避免后面异常测试被时钟持续打断。
  return timer_is_ready() &&
         timer_frequency_hz() == kPitFrequencyHz &&
         wait_elapsed_ticks >= kTimerWaitTestTicks &&
         sleep_elapsed_ticks >= kTimerSleepMinTicks;
}

void append_scheduler_trace(char* trace,
                            size_t capacity,
                            size_t* trace_length,
                            char mark) {
  if (trace == nullptr || trace_length == nullptr ||
      (*trace_length + 1) >= capacity) {
    return;
  }

  trace[*trace_length] = mark;
  ++(*trace_length);
  trace[*trace_length] = '\0';
}

void observe_scheduler_thread_tid(uint64_t* observed_tid) {
  if (observed_tid == nullptr || *observed_tid != 0) {
    return;
  }

  const ThreadControlBlock* const current_thread =
      scheduler_current_thread(&g_scheduler);
  if (current_thread != nullptr) {
    *observed_tid = current_thread->tid;
  }
}

bool run_scheduler_phase_until_idle() {
  enable_interrupts();
  const bool run_ok = scheduler_run_until_idle(&g_scheduler);
  disable_interrupts();
  return run_ok;
}

void scheduler_priority_thread_entry(void* raw_context) {
  auto* const context =
      static_cast<SchedulerPriorityThreadContext*>(raw_context);
  if (context == nullptr || context->shared == nullptr) {
    return;
  }

  observe_scheduler_thread_tid(&context->observed_tid);

  for (size_t iteration = 0; iteration < context->iterations; ++iteration) {
    append_scheduler_trace(context->shared->priority_trace,
                           sizeof(context->shared->priority_trace),
                           &context->shared->priority_trace_length,
                           context->trace_mark);

    if (context->wait_ticks != 0) {
      timer_wait_ticks(context->wait_ticks);
    }
  }
}

void scheduler_sleep_thread_entry(void* raw_context) {
  auto* const context =
      static_cast<SchedulerSleepThreadContext*>(raw_context);
  if (context == nullptr || context->shared == nullptr) {
    return;
  }

  observe_scheduler_thread_tid(&context->observed_tid);

  for (size_t iteration = 0; iteration < context->iterations; ++iteration) {
    append_scheduler_trace(context->shared->sleep_trace,
                           sizeof(context->shared->sleep_trace),
                           &context->shared->sleep_trace_length,
                           context->trace_mark);
    (void)scheduler_sleep_current_thread(context->sleep_ticks);
  }
}

void scheduler_blocked_thread_entry(void* raw_context) {
  auto* const context =
      static_cast<SchedulerBlockedThreadContext*>(raw_context);
  if (context == nullptr || context->shared == nullptr) {
    return;
  }

  observe_scheduler_thread_tid(&context->observed_tid);
  append_scheduler_trace(context->shared->block_trace,
                         sizeof(context->shared->block_trace),
                         &context->shared->block_trace_length,
                         'B');

  if (!scheduler_block_current_thread()) {
    return;
  }

  append_scheduler_trace(context->shared->block_trace,
                         sizeof(context->shared->block_trace),
                         &context->shared->block_trace_length,
                         'b');
}

void scheduler_wake_thread_entry(void* raw_context) {
  auto* const context =
      static_cast<SchedulerWakeThreadContext*>(raw_context);
  if (context == nullptr || context->shared == nullptr) {
    return;
  }

  observe_scheduler_thread_tid(&context->observed_tid);
  timer_wait_ticks(context->wait_ticks);
  append_scheduler_trace(context->shared->block_trace,
                         sizeof(context->shared->block_trace),
                         &context->shared->block_trace_length,
                         'W');

  if (context->wake_target != nullptr) {
    (void)scheduler_wake_thread(context->wake_target);
  }

  (void)scheduler_yield_current_thread();
}

bool run_scheduler_smoke_test() {
  if (!initialize_scheduler(&g_scheduler, kSchedulerTimeSliceTicks)) {
    return false;
  }

  SchedulerSmokeState smoke_state;
  memory_set(&smoke_state, 0, sizeof(smoke_state));

  SchedulerPriorityThreadContext high_context;
  memory_set(&high_context, 0, sizeof(high_context));
  high_context.shared = &smoke_state;
  high_context.trace_mark = 'H';
  high_context.iterations = 1;

  SchedulerPriorityThreadContext normal_a_context;
  memory_set(&normal_a_context, 0, sizeof(normal_a_context));
  normal_a_context.shared = &smoke_state;
  normal_a_context.trace_mark = 'A';
  normal_a_context.iterations = kSchedulerPriorityNormalIterations;
  normal_a_context.wait_ticks = kSchedulerThreadWaitTicks;

  SchedulerPriorityThreadContext normal_b_context;
  memory_set(&normal_b_context, 0, sizeof(normal_b_context));
  normal_b_context.shared = &smoke_state;
  normal_b_context.trace_mark = 'B';
  normal_b_context.iterations = kSchedulerPriorityNormalIterations;
  normal_b_context.wait_ticks = kSchedulerThreadWaitTicks;

  SchedulerPriorityThreadContext background_context;
  memory_set(&background_context, 0, sizeof(background_context));
  background_context.shared = &smoke_state;
  background_context.trace_mark = 'C';
  background_context.iterations = 1;

  SchedulerSleepThreadContext sleep_a_context;
  memory_set(&sleep_a_context, 0, sizeof(sleep_a_context));
  sleep_a_context.shared = &smoke_state;
  sleep_a_context.trace_mark = 'A';
  sleep_a_context.iterations = kSchedulerSleepIterations;
  sleep_a_context.sleep_ticks = 1;

  SchedulerSleepThreadContext sleep_b_context;
  memory_set(&sleep_b_context, 0, sizeof(sleep_b_context));
  sleep_b_context.shared = &smoke_state;
  sleep_b_context.trace_mark = 'B';
  sleep_b_context.iterations = kSchedulerSleepIterations;
  sleep_b_context.sleep_ticks = 1;

  SchedulerBlockedThreadContext blocked_context;
  memory_set(&blocked_context, 0, sizeof(blocked_context));
  blocked_context.shared = &smoke_state;

  SchedulerWakeThreadContext wake_context;
  memory_set(&wake_context, 0, sizeof(wake_context));
  wake_context.shared = &smoke_state;
  wake_context.wait_ticks = kSchedulerWakeThreadWaitTicks;

  ProcessControlBlock* const priority_process =
      scheduler_create_kernel_process(&g_scheduler, "sched-priority");
  if (priority_process == nullptr) {
    return false;
  }

  ThreadControlBlock* const high_thread =
      scheduler_create_kernel_thread(&g_scheduler, priority_process,
                                     "sched-high",
                                     scheduler_priority_thread_entry,
                                     &high_context, 0, kThreadPriorityHigh);
  ThreadControlBlock* const normal_a_thread =
      scheduler_create_kernel_thread(&g_scheduler, priority_process,
                                     "sched-normal-a",
                                     scheduler_priority_thread_entry,
                                     &normal_a_context, 0,
                                     kThreadPriorityNormal);
  ThreadControlBlock* const normal_b_thread =
      scheduler_create_kernel_thread(&g_scheduler, priority_process,
                                     "sched-normal-b",
                                     scheduler_priority_thread_entry,
                                     &normal_b_context, 0,
                                     kThreadPriorityNormal);
  ThreadControlBlock* const background_thread =
      scheduler_create_kernel_thread(&g_scheduler, priority_process,
                                     "sched-background",
                                     scheduler_priority_thread_entry,
                                     &background_context, 0,
                                     kThreadPriorityBackground);
  if (high_thread == nullptr || normal_a_thread == nullptr ||
      normal_b_thread == nullptr || background_thread == nullptr) {
    return false;
  }

  serial_write_string("sched_priority_pid=");
  serial_write_u64(priority_process->pid);
  serial_write_crlf();

  serial_write_string("sched_priority_high_tid=");
  serial_write_u64(high_thread->tid);
  serial_write_crlf();

  serial_write_string("sched_priority_normal_a_tid=");
  serial_write_u64(normal_a_thread->tid);
  serial_write_crlf();

  serial_write_string("sched_priority_normal_b_tid=");
  serial_write_u64(normal_b_thread->tid);
  serial_write_crlf();

  serial_write_string("sched_priority_background_tid=");
  serial_write_u64(background_thread->tid);
  serial_write_crlf();

  const bool run_priority_ok = run_scheduler_phase_until_idle();

  ProcessControlBlock* const sleep_process =
      scheduler_create_kernel_process(&g_scheduler, "sched-sleep");
  if (sleep_process == nullptr) {
    return false;
  }

  ThreadControlBlock* const sleep_a_thread =
      scheduler_create_kernel_thread(&g_scheduler, sleep_process,
                                     "sched-sleep-a",
                                     scheduler_sleep_thread_entry,
                                     &sleep_a_context, 0,
                                     kThreadPriorityNormal);
  ThreadControlBlock* const sleep_b_thread =
      scheduler_create_kernel_thread(&g_scheduler, sleep_process,
                                     "sched-sleep-b",
                                     scheduler_sleep_thread_entry,
                                     &sleep_b_context, 0,
                                     kThreadPriorityNormal);
  if (sleep_a_thread == nullptr || sleep_b_thread == nullptr) {
    return false;
  }

  serial_write_string("sched_sleep_pid=");
  serial_write_u64(sleep_process->pid);
  serial_write_crlf();

  serial_write_string("sched_sleep_thread_a_tid=");
  serial_write_u64(sleep_a_thread->tid);
  serial_write_crlf();

  serial_write_string("sched_sleep_thread_b_tid=");
  serial_write_u64(sleep_b_thread->tid);
  serial_write_crlf();

  const bool run_sleep_ok = run_scheduler_phase_until_idle();

  ProcessControlBlock* const block_process =
      scheduler_create_kernel_process(&g_scheduler, "sched-block");
  if (block_process == nullptr) {
    return false;
  }

  ThreadControlBlock* const blocked_thread =
      scheduler_create_kernel_thread(&g_scheduler, block_process,
                                     "sched-blocked",
                                     scheduler_blocked_thread_entry,
                                     &blocked_context, 0,
                                     kThreadPriorityNormal);
  if (blocked_thread == nullptr) {
    return false;
  }

  wake_context.wake_target = blocked_thread;
  ThreadControlBlock* const wake_thread =
      scheduler_create_kernel_thread(&g_scheduler, block_process,
                                     "sched-waker",
                                     scheduler_wake_thread_entry,
                                     &wake_context, 0,
                                     kThreadPriorityNormal);
  if (wake_thread == nullptr) {
    return false;
  }

  serial_write_string("sched_block_pid=");
  serial_write_u64(block_process->pid);
  serial_write_crlf();

  serial_write_string("sched_block_waiter_tid=");
  serial_write_u64(blocked_thread->tid);
  serial_write_crlf();

  serial_write_string("sched_block_waker_tid=");
  serial_write_u64(wake_thread->tid);
  serial_write_crlf();

  const bool run_block_ok = run_scheduler_phase_until_idle();

  serial_write_string("sched_priority_trace=");
  serial_write_string(smoke_state.priority_trace);
  serial_write_crlf();

  serial_write_string("sched_sleep_trace=");
  serial_write_string(smoke_state.sleep_trace);
  serial_write_crlf();

  serial_write_string("sched_block_trace=");
  serial_write_string(smoke_state.block_trace);
  serial_write_crlf();

  serial_write_string("sched_priority_process_state=");
  serial_write_string(scheduler_process_state_name(priority_process->state));
  serial_write_crlf();

  serial_write_string("sched_sleep_process_state=");
  serial_write_string(scheduler_process_state_name(sleep_process->state));
  serial_write_crlf();

  serial_write_string("sched_block_process_state=");
  serial_write_string(scheduler_process_state_name(block_process->state));
  serial_write_crlf();

  serial_write_string("sched_idle_priority=");
  serial_write_string(scheduler_thread_priority_name(
      g_scheduler.idle_thread->priority));
  serial_write_crlf();

  serial_write_string("sched_idle_dispatches=");
  serial_write_u64(g_scheduler.idle_thread->dispatch_count);
  serial_write_crlf();

  serial_write_string("sched_total_ticks=");
  serial_write_u64(g_scheduler.total_ticks);
  serial_write_crlf();

  serial_write_string("sched_total_switches=");
  serial_write_u64(g_scheduler.total_switches);
  serial_write_crlf();

  serial_write_string("sched_total_yields=");
  serial_write_u64(g_scheduler.total_yields);
  serial_write_crlf();

  serial_write_string("sched_preempt_requests=");
  serial_write_u64(g_scheduler.preempt_request_count);
  serial_write_crlf();

  serial_write_string("sched_sleeping_after=");
  serial_write_u64(scheduler_sleeping_thread_count(&g_scheduler));
  serial_write_crlf();

  serial_write_string("sched_blocked_after=");
  serial_write_u64(scheduler_blocked_thread_count(&g_scheduler));
  serial_write_crlf();

  serial_write_string("sched_ready_after=");
  serial_write_u64(scheduler_ready_thread_count(&g_scheduler));
  serial_write_crlf();

  serial_write_string("sched_live_after=");
  serial_write_u64(scheduler_live_thread_count(&g_scheduler));
  serial_write_crlf();

  const bool ok =
      run_priority_ok &&
      run_sleep_ok &&
      run_block_ok &&
      priority_process->pid == 1 &&
      sleep_process->pid == 2 &&
      block_process->pid == 3 &&
      high_thread->tid == 1 &&
      normal_a_thread->tid == 2 &&
      normal_b_thread->tid == 3 &&
      background_thread->tid == 4 &&
      sleep_a_thread->tid == 5 &&
      sleep_b_thread->tid == 6 &&
      blocked_thread->tid == 7 &&
      wake_thread->tid == 8 &&
      high_context.observed_tid == high_thread->tid &&
      normal_a_context.observed_tid == normal_a_thread->tid &&
      normal_b_context.observed_tid == normal_b_thread->tid &&
      background_context.observed_tid == background_thread->tid &&
      sleep_a_context.observed_tid == sleep_a_thread->tid &&
      sleep_b_context.observed_tid == sleep_b_thread->tid &&
      blocked_context.observed_tid == blocked_thread->tid &&
      wake_context.observed_tid == wake_thread->tid &&
      strings_equal(smoke_state.priority_trace,
                    kSchedulerPriorityExpectedTrace) &&
      strings_equal(smoke_state.sleep_trace,
                    kSchedulerSleepExpectedTrace) &&
      strings_equal(smoke_state.block_trace,
                    kSchedulerBlockExpectedTrace) &&
      priority_process->state == kProcessStateExited &&
      sleep_process->state == kProcessStateExited &&
      block_process->state == kProcessStateExited &&
      high_thread->state == kThreadStateFinished &&
      normal_a_thread->state == kThreadStateFinished &&
      normal_b_thread->state == kThreadStateFinished &&
      background_thread->state == kThreadStateFinished &&
      sleep_a_thread->state == kThreadStateFinished &&
      sleep_b_thread->state == kThreadStateFinished &&
      blocked_thread->state == kThreadStateFinished &&
      wake_thread->state == kThreadStateFinished &&
      normal_a_thread->consumed_ticks >=
          kSchedulerPriorityNormalIterations &&
      normal_b_thread->consumed_ticks >=
          kSchedulerPriorityNormalIterations &&
      wake_thread->consumed_ticks >= kSchedulerWakeThreadWaitTicks &&
      g_scheduler.idle_thread != nullptr &&
      g_scheduler.idle_thread->priority == kThreadPriorityIdle &&
      g_scheduler.idle_thread->dispatch_count >= 2 &&
      scheduler_sleeping_thread_count(&g_scheduler) == 0 &&
      scheduler_blocked_thread_count(&g_scheduler) == 0 &&
      scheduler_ready_thread_count(&g_scheduler) == 0 &&
      scheduler_live_thread_count(&g_scheduler) == 0 &&
      scheduler_current_thread(&g_scheduler) == nullptr;

  if (ok) {
    serial_write_string("scheduler ok");
    serial_write_crlf();
  }

  return ok;
}

void stdin_blocking_reader_thread_entry(void* raw_context) {
  auto* const context =
      static_cast<StdinBlockingReaderContext*>(raw_context);
  if (context == nullptr || context->syscall_context == nullptr) {
    return;
  }

  observe_scheduler_thread_tid(&context->observed_tid);
  context->character = '\0';
  context->read_result =
      sys_read(context->syscall_context, kSyscallStandardInputFd,
               &context->character, 1);
}

void stdin_blocking_injector_thread_entry(void* raw_context) {
  auto* const context =
      static_cast<StdinBlockingInjectorContext*>(raw_context);
  if (context == nullptr) {
    return;
  }

  observe_scheduler_thread_tid(&context->observed_tid);
  timer_wait_ticks(1);
  (void)keyboard_inject_test_scancode(context->scancode);
}

bool run_stdin_blocking_scheduler_smoke_test(SyscallContext* context) {
  if (context == nullptr ||
      !keyboard_is_ready() ||
      keyboard_buffered_char_count() != 0 ||
      !scheduler_is_ready(&g_scheduler) ||
      scheduler_current_thread(&g_scheduler) != nullptr) {
    return false;
  }

  StdinBlockingReaderContext reader_context;
  memory_set(&reader_context, 0, sizeof(reader_context));
  reader_context.syscall_context = context;
  reader_context.read_result = kSyscallInvalidArgument;

  StdinBlockingInjectorContext injector_context;
  memory_set(&injector_context, 0, sizeof(injector_context));
  injector_context.scancode = kBlockedStdinScancode;

  ProcessControlBlock* const process =
      scheduler_create_kernel_process(&g_scheduler, "stdin-block");
  if (process == nullptr) {
    return false;
  }

  ThreadControlBlock* const reader_thread =
      scheduler_create_kernel_thread(&g_scheduler, process,
                                     "stdin-reader",
                                     stdin_blocking_reader_thread_entry,
                                     &reader_context, 0,
                                     kThreadPriorityNormal);
  ThreadControlBlock* const injector_thread =
      scheduler_create_kernel_thread(&g_scheduler, process,
                                     "stdin-injector",
                                     stdin_blocking_injector_thread_entry,
                                     &injector_context, 0,
                                     kThreadPriorityNormal);
  if (reader_thread == nullptr || injector_thread == nullptr) {
    return false;
  }

  serial_write_string("stdin_block_pid=");
  serial_write_u64(process->pid);
  serial_write_crlf();

  serial_write_string("stdin_block_reader_tid=");
  serial_write_u64(reader_thread->tid);
  serial_write_crlf();

  serial_write_string("stdin_block_injector_tid=");
  serial_write_u64(injector_thread->tid);
  serial_write_crlf();

  const bool run_ok = run_scheduler_phase_until_idle();
  const uint16_t remaining_chars = keyboard_buffered_char_count();

  serial_write_string("stdin_block_read=");
  serial_write_i64(reader_context.read_result);
  serial_write_crlf();

  serial_write_string("stdin_block_char=0x");
  serial_write_hex64(static_cast<uint8_t>(reader_context.character));
  serial_write_crlf();

  serial_write_string("stdin_block_buffer_remaining=");
  serial_write_u64(remaining_chars);
  serial_write_crlf();

  return run_ok &&
         process->pid == 4 &&
         reader_thread->tid == 9 &&
         injector_thread->tid == 10 &&
         reader_context.observed_tid == reader_thread->tid &&
         injector_context.observed_tid == injector_thread->tid &&
         reader_context.read_result == 1 &&
         reader_context.character == kBlockedStdinExpected[0] &&
         process->state == kProcessStateExited &&
         reader_thread->state == kThreadStateFinished &&
         injector_thread->state == kThreadStateFinished &&
         scheduler_blocked_thread_count(&g_scheduler) == 0 &&
         remaining_chars == 0;
}

bool wait_for_keyboard_irq_count(uint64_t target_count,
                                 uint64_t timeout_ticks) {
  const uint64_t start_tick = timer_tick_count();

  while (keyboard_irq_count() < target_count) {
    if ((timer_tick_count() - start_tick) >= timeout_ticks) {
      return false;
    }

    wait_for_interrupt();
  }

  return true;
}

bool run_keyboard_smoke_test() {
  if (!initialize_keyboard()) {
    return false;
  }

  serial_write_string("keyboard init ok");
  serial_write_crlf();

  // 这一轮只单独放开 IRQ1，让键盘中断第一次真的能进入内核。
  if (!enable_pic_irq(kKeyboardIrqLine)) {
    return false;
  }

  serial_write_string("keyboard irq1 enabled");
  serial_write_crlf();

  const uint64_t before_irq_count = keyboard_irq_count();
  const uint64_t expected_irq_count =
      before_irq_count +
      (sizeof(kKeyboardTestScancodes) / sizeof(kKeyboardTestScancodes[0]));

  enable_interrupts();

  // 这一轮不再只测“来过一次 IRQ”，
  // 而是连续注入一串扫描码，验证：
  // 1. IRQ 顺序没有乱
  // 2. break code 会被忽略
  // 3. make code 会被翻译成字符并进入 FIFO 缓冲区
  for (uint64_t i = 0; i < (sizeof(kKeyboardTestScancodes) /
                            sizeof(kKeyboardTestScancodes[0]));
       ++i) {
    serial_write_string("keyboard_inject[");
    serial_write_u64(i);
    serial_write_string("]=0x");
    serial_write_hex64(kKeyboardTestScancodes[i]);
    serial_write_crlf();

    if (!keyboard_inject_test_scancode(kKeyboardTestScancodes[i])) {
      disable_interrupts();
      return false;
    }

    if (!wait_for_keyboard_irq_count(before_irq_count + i + 1,
                                     kKeyboardTestTimeoutTicks)) {
      disable_interrupts();
      return false;
    }
  }

  disable_interrupts();

  const uint64_t after_irq_count = keyboard_irq_count();
  const uint8_t last_scancode = keyboard_last_scancode();
  const uint16_t buffered_char_count = keyboard_buffered_char_count();
  const uint64_t dropped_char_count = keyboard_dropped_char_count();

  serial_write_string("keyboard_irq_count=");
  serial_write_u64(after_irq_count);
  serial_write_crlf();

  serial_write_string("keyboard_last_scancode=0x");
  serial_write_hex64(last_scancode);
  serial_write_crlf();

  serial_write_string("keyboard_char_count=");
  serial_write_u64(buffered_char_count);
  serial_write_crlf();

  bool chars_match = true;
  for (uint64_t i = 0; i < (sizeof(kKeyboardExpectedChars) /
                            sizeof(kKeyboardExpectedChars[0]));
       ++i) {
    char character = '\0';
    if (!keyboard_try_read_char(&character)) {
      chars_match = false;
      break;
    }

    serial_write_string("keyboard_char_");
    serial_write_u64(i);
    serial_write_string("=0x");
    serial_write_hex64(static_cast<uint8_t>(character));
    serial_write_crlf();

    if (character != kKeyboardExpectedChars[i]) {
      chars_match = false;
    }
  }

  char extra_character = '\0';
  const bool has_extra_char = keyboard_try_read_char(&extra_character);
  const uint16_t remaining_chars = keyboard_buffered_char_count();

  serial_write_string("keyboard_buffer_remaining=");
  serial_write_u64(remaining_chars);
  serial_write_crlf();

  serial_write_string("keyboard_dropped_chars=");
  serial_write_u64(dropped_char_count);
  serial_write_crlf();

  return after_irq_count == expected_irq_count &&
         last_scancode ==
             kKeyboardTestScancodes[(sizeof(kKeyboardTestScancodes) /
                                     sizeof(kKeyboardTestScancodes[0])) -
                                    1] &&
         buffered_char_count ==
             (sizeof(kKeyboardExpectedChars) / sizeof(kKeyboardExpectedChars[0])) &&
         chars_match && !has_extra_char && remaining_chars == 0 &&
         dropped_char_count == 0;
}

bool run_stdin_syscall_smoke_test(SyscallContext* context,
                                  FileDescriptorTable* fd_table) {
  if (context == nullptr ||
      fd_table == nullptr ||
      !keyboard_is_ready() ||
      keyboard_buffered_char_count() != 0 ||
      !initialize_syscall_context(context, fd_table) ||
      !install_syscall_dispatch_context(context) ||
      !syscall_dispatch_is_ready()) {
    return false;
  }

  if (!run_stdin_blocking_scheduler_smoke_test(context)) {
    return false;
  }

  uint64_t expected_irq_count = keyboard_irq_count();
  enable_interrupts();

  for (uint64_t i = 0; i < (sizeof(kSyscallStdinScancodes) /
                            sizeof(kSyscallStdinScancodes[0]));
       ++i) {
    serial_write_string("stdin_inject[");
    serial_write_u64(i);
    serial_write_string("]=0x");
    serial_write_hex64(kSyscallStdinScancodes[i]);
    serial_write_crlf();

    if (!keyboard_inject_test_scancode(kSyscallStdinScancodes[i])) {
      disable_interrupts();
      return false;
    }

    ++expected_irq_count;
    if (!wait_for_keyboard_irq_count(expected_irq_count,
                                     kKeyboardTestTimeoutTicks)) {
      disable_interrupts();
      return false;
    }
  }

  disable_interrupts();

  char stdin_buffer[8];
  const int32_t stdin_read =
      sys_read(context, kSyscallStandardInputFd,
               stdin_buffer, sizeof(stdin_buffer) - 1);
  if (stdin_read < 0) {
    return false;
  }
  stdin_buffer[static_cast<size_t>(stdin_read)] = '\0';

  char stdin_empty = '\0';
  const int32_t stdin_empty_read =
      sys_read(context, kSyscallStandardInputFd,
               &stdin_empty, 1);

  serial_write_string("stdin_sys_read=");
  serial_write_i64(stdin_read);
  serial_write_crlf();

  serial_write_string("stdin_sys_empty=");
  serial_write_i64(stdin_empty_read);
  serial_write_crlf();

  enable_interrupts();

  for (uint64_t i = 0; i < (sizeof(kInt80StdinScancodes) /
                            sizeof(kInt80StdinScancodes[0]));
       ++i) {
    serial_write_string("stdin_int80_inject[");
    serial_write_u64(i);
    serial_write_string("]=0x");
    serial_write_hex64(kInt80StdinScancodes[i]);
    serial_write_crlf();

    if (!keyboard_inject_test_scancode(kInt80StdinScancodes[i])) {
      disable_interrupts();
      return false;
    }

    ++expected_irq_count;
    if (!wait_for_keyboard_irq_count(expected_irq_count,
                                     kKeyboardTestTimeoutTicks)) {
      disable_interrupts();
      return false;
    }
  }

  disable_interrupts();

  char int80_stdin_buffer[8];
  const int64_t int80_stdin_read =
      invoke_int80_syscall(kSyscallNumberRead,
                           static_cast<uint64_t>(kSyscallStandardInputFd),
                           reinterpret_cast<uint64_t>(int80_stdin_buffer),
                           sizeof(int80_stdin_buffer) - 1);
  if (int80_stdin_read < 0) {
    return false;
  }
  int80_stdin_buffer[static_cast<size_t>(int80_stdin_read)] = '\0';

  char int80_stdin_empty = '\0';
  const int64_t int80_stdin_empty_read =
      invoke_int80_syscall(kSyscallNumberRead,
                           static_cast<uint64_t>(kSyscallStandardInputFd),
                           reinterpret_cast<uint64_t>(&int80_stdin_empty),
                           1);

  const uint16_t remaining_chars = keyboard_buffered_char_count();
  const uint64_t dropped_chars = keyboard_dropped_char_count();

  serial_write_string("stdin_int80_read=");
  serial_write_i64(int80_stdin_read);
  serial_write_crlf();

  serial_write_string("stdin_int80_empty=");
  serial_write_i64(int80_stdin_empty_read);
  serial_write_crlf();

  serial_write_string("stdin_buffer_remaining=");
  serial_write_u64(remaining_chars);
  serial_write_crlf();

  serial_write_string("stdin_dropped_chars=");
  serial_write_u64(dropped_chars);
  serial_write_crlf();

  const bool ok =
      keyboard_irq_count() == expected_irq_count &&
      stdin_read == static_cast<int32_t>(string_length(kSyscallStdinExpected)) &&
      stdin_empty_read == 0 &&
      strings_equal(stdin_buffer, kSyscallStdinExpected) &&
      int80_stdin_read ==
          static_cast<int64_t>(string_length(kInt80StdinExpected)) &&
      int80_stdin_empty_read == 0 &&
      strings_equal(int80_stdin_buffer, kInt80StdinExpected) &&
      remaining_chars == 0 &&
      dropped_chars == 0;

  if (ok) {
    serial_write_string("stdin_syscall ok");
    serial_write_crlf();
  }

  return ok;
}

bool run_console_input_smoke_test() {
  if (keyboard_buffered_char_count() != 0) {
    return false;  // 先确认上一轮字符测试已经把缓冲区消费干净。
  }

  initialize_console(kConsoleTestStartRow, kShellTextColor);
  console_write_string("input> ");  // 这一轮先给最小控制台写一个提示符，模拟将来的命令行入口。

  const uint64_t before_irq_count = keyboard_irq_count();
  const uint64_t expected_irq_count =
      before_irq_count +
      (sizeof(kConsoleInputTestScancodes) /
       sizeof(kConsoleInputTestScancodes[0]));

  enable_interrupts();

  for (uint64_t i = 0; i < (sizeof(kConsoleInputTestScancodes) /
                            sizeof(kConsoleInputTestScancodes[0]));
       ++i) {
    serial_write_string("console_inject[");
    serial_write_u64(i);
    serial_write_string("]=0x");
    serial_write_hex64(kConsoleInputTestScancodes[i]);
    serial_write_crlf();

    if (!keyboard_inject_test_scancode(kConsoleInputTestScancodes[i])) {
      disable_interrupts();
      return false;
    }

    if (!wait_for_keyboard_irq_count(before_irq_count + i + 1,
                                     kKeyboardTestTimeoutTicks)) {
      disable_interrupts();
      return false;
    }
  }

  char line_buffer[kConsoleLineBufferCapacity];
  const size_t line_length =
      console_read_line(line_buffer, sizeof(line_buffer));

  disable_interrupts();

  serial_write_string("console_line_length=");
  serial_write_u64(line_length);
  serial_write_crlf();

  serial_write_string("console_line=");
  serial_write_string(line_buffer);
  serial_write_crlf();

  serial_write_string("console_buffer_remaining=");
  serial_write_u64(keyboard_buffered_char_count());
  serial_write_crlf();

  return keyboard_irq_count() == expected_irq_count &&
         line_length == string_length(kConsoleExpectedLine) &&
         strings_equal(line_buffer, kConsoleExpectedLine) &&
         keyboard_buffered_char_count() == 0;
}

struct ShellSmokeCommand {
  const char* expected_line;              // 这一条命令读取出来后，应该得到哪一行文本。
  const uint8_t* scancodes;               // 用哪些扫描码把这条命令注入进键盘路径。
  size_t scancode_count;                  // 这条命令一共注入多少个扫描码。
  ShellCommandResult expected_result;     // 这条命令执行后，应该得到什么分类结果。
};

constexpr ShellSmokeCommand kShellSmokeCommands[] = {
    {"help", kShellHelpScancodes,
     sizeof(kShellHelpScancodes) / sizeof(kShellHelpScancodes[0]),
     kShellCommandExecuted},
    {"mem", kShellMemScancodes,
     sizeof(kShellMemScancodes) / sizeof(kShellMemScancodes[0]),
     kShellCommandExecuted},
    {"ticks", kShellTicksScancodes,
     sizeof(kShellTicksScancodes) / sizeof(kShellTicksScancodes[0]),
     kShellCommandExecuted},
    {"heap", kShellHeapScancodes,
     sizeof(kShellHeapScancodes) / sizeof(kShellHeapScancodes[0]),
     kShellCommandExecuted},
    {"disk", kShellDiskScancodes,
     sizeof(kShellDiskScancodes) / sizeof(kShellDiskScancodes[0]),
     kShellCommandExecuted},
    {"ls", kShellLsScancodes,
     sizeof(kShellLsScancodes) / sizeof(kShellLsScancodes[0]),
     kShellCommandExecuted},
    {"ls docs", kShellLsDocsScancodes,
     sizeof(kShellLsDocsScancodes) / sizeof(kShellLsDocsScancodes[0]),
     kShellCommandExecuted},
    {"cat readme.txt", kShellCatReadmeScancodes,
     sizeof(kShellCatReadmeScancodes) /
         sizeof(kShellCatReadmeScancodes[0]),
     kShellCommandExecuted},
    {"cat /docs/guide.txt", kShellCatAbsoluteGuideScancodes,
     sizeof(kShellCatAbsoluteGuideScancodes) /
         sizeof(kShellCatAbsoluteGuideScancodes[0]),
     kShellCommandExecuted},
    {"stat docs/guide.txt", kShellStatGuideScancodes,
     sizeof(kShellStatGuideScancodes) /
         sizeof(kShellStatGuideScancodes[0]),
     kShellCommandExecuted},
    {"stat docs/../notes.txt", kShellStatParentNotesScancodes,
     sizeof(kShellStatParentNotesScancodes) /
         sizeof(kShellStatParentNotesScancodes[0]),
     kShellCommandExecuted},
    {"pwd", kShellPwdScancodes,
     sizeof(kShellPwdScancodes) / sizeof(kShellPwdScancodes[0]),
     kShellCommandExecuted},
    {"cd docs", kShellCdDocsScancodes,
     sizeof(kShellCdDocsScancodes) / sizeof(kShellCdDocsScancodes[0]),
     kShellCommandExecuted},
    {"pwd", kShellPwdScancodes,
     sizeof(kShellPwdScancodes) / sizeof(kShellPwdScancodes[0]),
     kShellCommandExecuted},
    {"ls", kShellLsScancodes,
     sizeof(kShellLsScancodes) / sizeof(kShellLsScancodes[0]),
     kShellCommandExecuted},
    {"cat guide.txt", kShellCatGuideScancodes,
     sizeof(kShellCatGuideScancodes) / sizeof(kShellCatGuideScancodes[0]),
     kShellCommandExecuted},
    {"irq", kShellIrqScancodes,
     sizeof(kShellIrqScancodes) / sizeof(kShellIrqScancodes[0]),
     kShellCommandExecuted},
    {"bootinfo", kShellBootInfoScancodes,
     sizeof(kShellBootInfoScancodes) / sizeof(kShellBootInfoScancodes[0]),
     kShellCommandExecuted},
    {"e820", kShellE820Scancodes,
     sizeof(kShellE820Scancodes) / sizeof(kShellE820Scancodes[0]),
     kShellCommandExecuted},
    {"cpu", kShellCpuScancodes,
     sizeof(kShellCpuScancodes) / sizeof(kShellCpuScancodes[0]),
     kShellCommandExecuted},
    {"echo hi42", kShellEditedEchoScancodes,
     sizeof(kShellEditedEchoScancodes) / sizeof(kShellEditedEchoScancodes[0]),
     kShellCommandExecuted},
    {"uptime", kShellUptimeScancodes,
     sizeof(kShellUptimeScancodes) / sizeof(kShellUptimeScancodes[0]),
     kShellCommandExecuted},
    {"history", kShellHistoryScancodes,
     sizeof(kShellHistoryScancodes) / sizeof(kShellHistoryScancodes[0]),
     kShellCommandExecuted},
    {"history", kShellHistoryBrowseScancodes,
     sizeof(kShellHistoryBrowseScancodes) / sizeof(kShellHistoryBrowseScancodes[0]),
     kShellCommandExecuted},
    {"clear", kShellClearScancodes,
     sizeof(kShellClearScancodes) / sizeof(kShellClearScancodes[0]),
     kShellCommandExecuted},
    {"bad", kShellBadScancodes,
     sizeof(kShellBadScancodes) / sizeof(kShellBadScancodes[0]),
     kShellCommandUnknown},
};

bool inject_scancode_sequence(const char* log_prefix,
                              const uint8_t* scancodes,
                              size_t scancode_count,
                              uint64_t* next_expected_irq_count) {
  if (log_prefix == nullptr || scancodes == nullptr ||
      next_expected_irq_count == nullptr) {
    return false;
  }

  for (size_t i = 0; i < scancode_count; ++i) {
    serial_write_string(log_prefix);
    serial_write_char('[');
    serial_write_u64(i);
    serial_write_string("]=0x");
    serial_write_hex64(scancodes[i]);
    serial_write_crlf();

    if (!keyboard_inject_test_scancode(scancodes[i])) {
      return false;
    }

    ++(*next_expected_irq_count);
    if (!wait_for_keyboard_irq_count(*next_expected_irq_count,
                                     kKeyboardTestTimeoutTicks)) {
      return false;
    }
  }

  return true;
}

bool run_shell_smoke_test(const BootInfo* boot_info,
                          const BootVolume* boot_volume,
                          const BlockDevice* block_device,
                          SyscallContext* syscall_context) {
  initialize_console(kShellTestStartRow, kShellTextColor);

  if (!initialize_shell(&g_shell, boot_info, &g_page_allocator, &g_kernel_heap,
                        boot_volume, block_device, syscall_context,
                        &kShellOutput)) {
    return false;
  }

  ConsoleHistoryProvider history_provider;
  history_provider.entry_count = shell_history_provider_count;
  history_provider.entry_text = shell_history_provider_text;
  history_provider.context = &g_shell;

  uint64_t expected_irq_count = keyboard_irq_count();
  enable_interrupts();

  for (size_t command_index = 0;
       command_index < (sizeof(kShellSmokeCommands) / sizeof(kShellSmokeCommands[0]));
       ++command_index) {
    const ShellSmokeCommand& command = kShellSmokeCommands[command_index];

    shell_print_prompt(&g_shell);
    if (!inject_scancode_sequence("shell_inject",
                                  command.scancodes,
                                  command.scancode_count,
                                  &expected_irq_count)) {
      disable_interrupts();
      return false;
    }

    char line_buffer[kShellLineBufferCapacity];
    const size_t line_length =
        console_read_line_with_history(line_buffer, sizeof(line_buffer),
                                       &history_provider);

    serial_write_string("shell_line=");
    serial_write_string(line_buffer);
    serial_write_crlf();

    serial_write_string("shell_line_length=");
    serial_write_u64(line_length);
    serial_write_crlf();

    const ShellCommandResult result =
        shell_execute_line(&g_shell, line_buffer);

    serial_write_string("shell_result=");
    serial_write_string(shell_command_result_name(result));
    serial_write_crlf();

    if (!strings_equal(line_buffer, command.expected_line) ||
        line_length != string_length(command.expected_line) ||
        result != command.expected_result) {
      disable_interrupts();
      return false;
    }
  }

  disable_interrupts();
  return keyboard_irq_count() == expected_irq_count &&
         keyboard_buffered_char_count() == 0;
}

#if defined(OS64_ENABLE_INVALID_OPCODE_SMOKE)
void run_invalid_opcode_smoke_test() {
  serial_write_string("invalid_opcode_marker=0x");
  serial_write_hex64(kInvalidOpcodeMarker);
  serial_write_crlf();

  asm volatile("ud2");   // `ud2` 是 x86 专门留给“故意触发非法指令异常”的指令。
}
#endif

#if defined(OS64_ENABLE_PAGE_FAULT_SMOKE)
void run_page_fault_smoke_test() {
  serial_write_string("page_fault_smoke_address=0x");
  serial_write_hex64(kPageFaultSmokeAddress);
  serial_write_crlf();

  auto* const fault_ptr = reinterpret_cast<volatile uint64_t*>(
      static_cast<uintptr_t>(kPageFaultSmokeAddress));
  *fault_ptr = kPageFaultSmokePattern;  // 这里应该触发 page fault，正常不会再往下走。
}
#endif

}  // namespace

extern "C" void kernel_main(const BootInfo* boot_info) {
  write_status_line(4, "hello from os64 kernel");

  if (!initialize_idt()) {
    write_status_line(5, "idt bad");
    return;
  }

  write_status_line(5, "idt ok");

  if (!is_boot_info_valid(boot_info)) {
    write_status_line(6, "boot info bad");
    return;
  }

  write_status_line(6, "boot info ok");

  log_e820_entries(boot_info);                 // 第一步：真正把 BIOS 给的内存地图读出来。
  write_status_line(7, "e820 parse ok");

  if (!initialize_page_allocator(&g_page_allocator, boot_info)) {
    write_status_line(8, "page allocator bad");
    return;
  }

  log_allocator_ranges(g_page_allocator);      // 第二步：把 usable 区域裁成真正可分配的页区间。
  log_allocated_pages(&g_page_allocator);      // 第三步：实际分几个页，证明 alloc_page() 已经能用了。

  serial_write_string("free_pages_remaining=");
  serial_write_u64(count_free_pages(&g_page_allocator));
  serial_write_crlf();

  write_status_line(8, "page allocator ok");

  if (!run_paging_smoke_test(&g_page_allocator)) {
    write_status_line(9, "map_page bad");
    return;
  }

  write_status_line(9, "map_page ok");

  if (!initialize_kernel_heap(&g_kernel_heap, &g_page_allocator)) {
    write_status_line(10, "heap init bad");
    return;
  }

  write_status_line(10, "heap init ok");

  if (!run_heap_smoke_test(&g_kernel_heap)) {
    write_status_line(11, "heap alloc bad");
    return;
  }

  write_status_line(11, "heap alloc ok");

  if (!run_kernel_memory_smoke_test(&g_page_allocator, &g_kernel_heap)) {
    write_status_line(12, "kernel memory bad");
    return;
  }

  write_status_line(12, "kernel memory ok");

  if (!run_boot_volume_smoke_test(boot_info, &g_boot_volume,
                                  &g_boot_block_device)) {
    write_status_line(13, "boot volume bad");
    return;
  }

  write_status_line(13, "boot volume ok");

  if (!run_filesystem_smoke_test(&g_boot_block_device, &g_os64fs)) {
    write_status_line(14, "filesystem bad");
    return;
  }

  if (!run_file_handle_smoke_test(&g_os64fs)) {
    write_status_line(14, "file layer bad");
    return;
  }

  if (!run_directory_handle_smoke_test(&g_os64fs)) {
    write_status_line(14, "directory layer bad");
    return;
  }

  if (!run_vfs_smoke_test(&g_vfs, &g_os64fs)) {
    write_status_line(14, "vfs bad");
    return;
  }

  if (!run_file_descriptor_smoke_test(&g_fd_table, &g_vfs)) {
    write_status_line(14, "fd layer bad");
    return;
  }

  if (!run_syscall_smoke_test(&g_syscall_context, &g_fd_table)) {
    write_status_line(14, "syscall layer bad");
    return;
  }

  if (!run_int80_syscall_smoke_test(&g_syscall_context, &g_fd_table)) {
    write_status_line(14, "int80 syscall bad");
    return;
  }

  write_status_line(14, "filesystem ok");

  if (!run_timer_smoke_test()) {
    write_status_line(15, "timer bad");
    return;
  }

  write_status_line(15, "timer ok");

  if (!run_scheduler_smoke_test()) {
    write_status_line(16, "scheduler bad");
    return;
  }

  write_status_line(16, "scheduler ok");

  if (!run_keyboard_smoke_test()) {
    write_status_line(17, "keyboard bad");
    return;
  }

  if (!run_stdin_syscall_smoke_test(&g_syscall_context, &g_fd_table)) {
    write_status_line(17, "stdin syscall bad");
    return;
  }

  write_status_line(17, "keyboard ok");

  if (!run_console_input_smoke_test()) {
    write_status_line(18, "console input bad");
    return;
  }

  write_status_line(18, "console input ok");

  if (!run_shell_smoke_test(boot_info, &g_boot_volume,
                            &g_boot_block_device, &g_syscall_context)) {
    write_status_line(19, "shell bad");
    return;
  }

  write_status_line(19, "shell ok");

#if defined(OS64_ENABLE_PAGE_FAULT_SMOKE)
  write_status_line(20, "page fault smoke");
  run_page_fault_smoke_test();
#endif

#if defined(OS64_ENABLE_INVALID_OPCODE_SMOKE)
  write_status_line(20, "invalid opcode smoke");
  run_invalid_opcode_smoke_test();
#endif

#if !defined(OS64_ENABLE_PAGE_FAULT_SMOKE) && !defined(OS64_ENABLE_INVALID_OPCODE_SMOKE)
  enable_interrupts();  // 真正进入交互 shell 前重新开中断，不然 `hlt` 等键盘时不会再醒。
  char shell_line_buffer[kShellLineBufferCapacity];
  shell_run_forever(&g_shell, shell_line_buffer, sizeof(shell_line_buffer));
#endif
}

extern "C" void kernel_handle_exception(const InterruptFrame* frame,
                                         uint64_t fault_address) {
  vga_write_line(12, "kernel exception", kExceptionTextColor);

  serial_write_string("exception=");
  serial_write_string(exception_name(frame->vector));
  serial_write_crlf();

  serial_write_string("vector=0x");
  serial_write_hex64(frame->vector);
  serial_write_crlf();

  serial_write_string("error_code=0x");
  serial_write_hex64(frame->error_code);
  serial_write_crlf();

  if (frame->vector == 14) {
    serial_write_string("fault_address=0x");
    serial_write_hex64(fault_address);
    serial_write_crlf();
  }

  serial_write_string("fault_rip=0x");
  serial_write_hex64(frame->rip);
  serial_write_crlf();
}
