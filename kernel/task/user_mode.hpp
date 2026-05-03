#ifndef OS64_USER_MODE_HPP
#define OS64_USER_MODE_HPP

#include <stddef.h>
#include <stdint.h>

// 这是第一版“进入用户态时需要交给汇编入口”的最小机器现场。
// 之所以刻意做成一排固定 64 位字段，是因为：
// 1. 汇编按固定偏移取值最直接
// 2. 这一轮重点就是把 iretq 需要的关键机器状态看清楚
// 3. 后面真做更正式的 trap frame / 用户线程上下文时，方便一项项对照
struct UserModeLaunchContext {
  uint64_t kernel_resume_stack_pointer;  // `user_mode_enter()` 离开内核前，会把“以后该回到哪条内核栈”记在这里。
  uint64_t kernel_root_physical;         // 退出用户态时，要把 CR3 切回哪份内核页表根。
  uint64_t user_root_physical;           // 第一次进入 ring 3 前，要切进哪份用户地址空间。
  uint64_t user_instruction_pointer;     // iretq 最后落到哪条用户指令。
  uint64_t user_stack_pointer;           // 用户态开始执行时，RSP 应该是多少。
  uint64_t user_rflags;                  // 一起带进用户态的标志位；当前 smoke 先保守关着 IF。
  uint64_t user_code_selector;           // iretq 要弹进 CS 的用户代码段。
  uint64_t user_stack_selector;          // iretq 要弹进 SS 的用户数据段。
  uint64_t return_value;                 // 用户态最后用 `exit` syscall 带回来的值；当前先拿它回传用户态看到的 CS。
};

static_assert(offsetof(UserModeLaunchContext, kernel_resume_stack_pointer) == 0,
              "UserModeLaunchContext layout must match user_mode_enter");
static_assert(offsetof(UserModeLaunchContext, kernel_root_physical) == 8,
              "UserModeLaunchContext layout must match user_mode_enter");
static_assert(offsetof(UserModeLaunchContext, user_root_physical) == 16,
              "UserModeLaunchContext layout must match user_mode_enter");
static_assert(offsetof(UserModeLaunchContext, user_instruction_pointer) == 24,
              "UserModeLaunchContext layout must match user_mode_enter");
static_assert(offsetof(UserModeLaunchContext, user_stack_pointer) == 32,
              "UserModeLaunchContext layout must match user_mode_enter");
static_assert(offsetof(UserModeLaunchContext, user_rflags) == 40,
              "UserModeLaunchContext layout must match user_mode_enter");
static_assert(offsetof(UserModeLaunchContext, user_code_selector) == 48,
              "UserModeLaunchContext layout must match user_mode_enter");
static_assert(offsetof(UserModeLaunchContext, user_stack_selector) == 56,
              "UserModeLaunchContext layout must match user_mode_enter");
static_assert(offsetof(UserModeLaunchContext, return_value) == 64,
              "UserModeLaunchContext layout must match user_mode_enter");

extern "C" uint64_t user_mode_enter(UserModeLaunchContext* context);
extern "C" [[noreturn]] void user_mode_resume_kernel(
    uint64_t kernel_resume_stack_pointer,
    uint64_t kernel_root_physical,
    uint64_t return_value);
extern "C" uint8_t user_mode_smoke_program_start;
extern "C" uint8_t user_mode_smoke_program_end;

#endif
