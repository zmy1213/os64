#ifndef OS64_SEGMENTS_HPP
#define OS64_SEGMENTS_HPP

#include <stdint.h>

// 这几个选择子和 stage2 里进入 long mode 时使用的那套 GDT 保持兼容：
// - 0x18 = 64 位代码段
// - 0x20 = 64 位数据段
//
// 这样内核后面即使自己重新装一份 GDT，
// 也不用把前面已经写死在 IDT/入口路径里的段选择子全部改掉。
constexpr uint16_t kKernelCodeSelector = 0x18;
constexpr uint16_t kKernelDataSelector = 0x20;

// 64 位 TSS 描述符会占 16 字节，也就是两个普通 GDT 槽位。
// 这里约定把它接在现有 5 个描述符后面，所以选择子就是 0x28。
constexpr uint16_t kKernelTssSelector = 0x28;

// 第一版用户态会复用同一份 GDT，只是在后面再补两项 ring 3 可用段描述符：
// - 0x38 = user data
// - 0x40 = user code
//
// 真正拿去做 `iretq` / `mov ds, ax` 时，还会再把最低两位 RPL 设成 3。
constexpr uint16_t kUserDataSelector = 0x38;
constexpr uint16_t kUserCodeSelector = 0x40;
constexpr uint16_t kUserDataSelectorRpl3 = kUserDataSelector | 0x3;
constexpr uint16_t kUserCodeSelectorRpl3 = kUserCodeSelector | 0x3;

// `IST = 0` 表示“不使用中断栈表，继续沿用当前栈”；
// `IST = 1` 才表示切到 TSS 里的第 1 根应急栈。
constexpr uint8_t kKernelDoubleFaultIstIndex = 1;

#endif
