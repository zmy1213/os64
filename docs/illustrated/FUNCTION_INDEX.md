# 全函数图解索引

当前库存的 **1020 个 C++ 函数定义已全部逐函数讲解**。构造/析构和头文件中的显式定义计入；声明、隐式生成函数与汇编不混入总数。

所有函数的图块都已关联到实际文件，共 **29 张机制图**；源码行、讲义标题/表行和图块均在覆盖记录中逐项保存。

一张六栏图解释一组协作关系，每个已讲解函数链接到真实标题或表格所在小节；表行定位另保存在 [覆盖记录](FUNCTION_COVERAGE.json)。源码链接使用仓库相对路径与定义起始行。此索引列出实现函数，不将工作窃取、无锁调度、运行时迁移等未来计划标成已实现。

库存和提取方法见 [FUNCTION_INVENTORY.json](FUNCTION_INVENTORY.json)。讲义中的同名图块属于各自章节，例如调度 P 指进程回收，协作 P 指管道。

| 讲义 | 函数数 | 实际源码范围 |
| --- | ---: | --- |
| [SMP_BOOT_FUNCTIONS.md](SMP_BOOT_FUNCTIONS.md) | 68 | `kernel/cpu/smp.cpp`、`kernel/cpu/topology.cpp`、`kernel/cpu/cpu.cpp`、`kernel/cpu/xapic.hpp`、`kernel/interrupts/interrupts.cpp` |
| [SCHEDULER_FUNCTIONS.md](SCHEDULER_FUNCTIONS.md) | 88 | `kernel/task/scheduler.cpp` |
| [COOPERATION_FUNCTIONS.md](COOPERATION_FUNCTIONS.md) | 104 | `kernel/fs/fd.cpp`、`kernel/net/network.cpp`、`user/programs/coop_test.cpp`、`user/programs/smp_test.cpp`、`user/programs/udp_mixed.cpp`、`user/bench_workload.hpp` |
| [PARALLEL_REDUCTION.md](PARALLEL_REDUCTION.md) | 12 | `user/programs/parallel_reduce.cpp` |
| [DEVICES_IO_FUNCTIONS.md](DEVICES_IO_FUNCTIONS.md) | 162 | `kernel/console/console.cpp`、`kernel/device/pci.cpp`、`kernel/interrupts/keyboard.cpp`、`kernel/interrupts/pic.cpp`、`kernel/interrupts/pit.cpp`、`kernel/interrupts/serial.cpp`、`kernel/log/log.cpp`、`kernel/storage/ata_pio.cpp`、`kernel/storage/block_device.cpp`、`kernel/storage/boot_volume.cpp`、`kernel/runtime/runtime.cpp`、`kernel/net/network.hpp`、`kernel/net/network_irq.cpp`、`kernel/net/virtio_net.cpp`、`kernel/perf/perf.cpp` |
| [SYSTEM_MEMORY_FUNCTIONS.md](SYSTEM_MEMORY_FUNCTIONS.md) | 181 | `kernel/core/kernel_main.cpp`、`kernel/memory/page_allocator.cpp`、`kernel/memory/paging.cpp`、`kernel/memory/address_space.cpp`、`kernel/memory/heap.cpp`、`kernel/memory/kmemory.cpp`、`kernel/memory/kmemory.hpp` |
| [USER_ABI_FUNCTIONS.md](USER_ABI_FUNCTIONS.md) | 107 | `kernel/syscall/syscall.cpp`、`kernel/task/elf_loader.cpp`、`user/memory.cpp`、`user/os64.hpp`、`user/smp.hpp`、`user/udp.hpp` |
| [FILESYSTEM_FUNCTIONS.md](FILESYSTEM_FUNCTIONS.md) | 131 | `kernel/fs/os64fs.cpp`、`kernel/fs/directory.cpp`、`kernel/fs/file.cpp`、`kernel/fs/vfs.cpp` |
| [SHELL_FUNCTIONS.md](SHELL_FUNCTIONS.md) | 87 | `kernel/shell/parser.cpp`、`kernel/shell/shell.cpp` |
| [USER_PROGRAM_FUNCTIONS.md](USER_PROGRAM_FUNCTIONS.md) | 80 | `user/programs/badptr.cpp`、`user/programs/bench.cpp`、`user/programs/bench_ipc.cpp`、`user/programs/cat.cpp`、`user/programs/echo.cpp`、`user/programs/edit.cpp`、`user/programs/false.cpp`、`user/programs/fault.cpp`、`user/programs/fp_test.cpp`、`user/programs/fs_test.cpp`、`user/programs/hello.cpp`、`user/programs/ls.cpp`、`user/programs/mem_test.cpp`、`user/programs/mkdir.cpp`、`user/programs/nxfault.cpp`、`user/programs/perf_test.cpp`、`user/programs/pipe_test.cpp`、`user/programs/pwd.cpp`、`user/programs/rm.cpp`、`user/programs/sched_test.cpp`、`user/programs/sleep.cpp`、`user/programs/spawn_test.cpp`、`user/programs/spin.cpp`、`user/programs/stackfault.cpp`、`user/programs/sync.cpp`、`user/programs/true.cpp`、`user/programs/ud2.cpp`、`user/programs/udp_test.cpp`、`user/programs/wc.cpp`、`user/programs/writer.cpp` |

## 按源码文件查找

### kernel/console/console.cpp

23 个定义；已讲解 23 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `vga_buffer` | [L28](../../kernel/console/console.cpp#L28) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第39行） | [I(e)](images/console-input.png) |
| `vga_cell` | [L32](../../kernel/console/console.cpp#L32) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第40行） | [I(e)](images/console-input.png) |
| `clear_row` | [L37](../../kernel/console/console.cpp#L37) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第41行） | [I(e)](images/console-input.png) |
| `scroll_if_needed` | [L46](../../kernel/console/console.cpp#L46) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第42行） | [I(e)](images/console-input.png) |
| `put_visible_char` | [L66](../../kernel/console/console.cpp#L66) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第43行） | [I(e)](images/console-input.png) |
| `newline` | [L80](../../kernel/console/console.cpp#L80) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第44行） | [I(e)](images/console-input.png) |
| `backspace` | [L87](../../kernel/console/console.cpp#L87) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第45行） | [I(d)、I(e)](images/console-input.png) |
| `is_printable_ascii` | [L105](../../kernel/console/console.cpp#L105) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第46行） | [I(d)](images/console-input.png) |
| `current_cursor` | [L109](../../kernel/console/console.cpp#L109) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第47行） | [I(d)](images/console-input.png) |
| `set_cursor` | [L116](../../kernel/console/console.cpp#L116) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第48行） | [I(d)、I(e)](images/console-input.png) |
| `set_cursor_to_line_offset` | [L121](../../kernel/console/console.cpp#L121) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第49行） | [I(d)、I(e)](images/console-input.png) |
| `redraw_input_line` | [L131](../../kernel/console/console.cpp#L131) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第50行） | [I(d)、I(e)、I(f)](images/console-input.png) |
| `copy_line_text` | [L169](../../kernel/console/console.cpp#L169) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第51行） | [I(d)](images/console-input.png) |
| `history_entry_count` | [L189](../../kernel/console/console.cpp#L189) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第52行） | [I(d)](images/console-input.png) |
| `console_set_viewport` | [L199](../../kernel/console/console.cpp#L199) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第53行） | [I(e)](images/console-input.png) |
| `initialize_console` | [L224](../../kernel/console/console.cpp#L224) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第54行） | [I(e)](images/console-input.png) |
| `console_is_initialized` | [L241](../../kernel/console/console.cpp#L241) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第55行） | [I(e)](images/console-input.png) |
| `console_write_char` | [L245](../../kernel/console/console.cpp#L245) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第56行） | [I(e)](images/console-input.png) |
| `console_write_string` | [L263](../../kernel/console/console.cpp#L263) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第57行） | [I(e)](images/console-input.png) |
| `console_set_color` | [L273](../../kernel/console/console.cpp#L273) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第58行） | [I(e)](images/console-input.png) |
| `console_clear` | [L277](../../kernel/console/console.cpp#L277) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第59行） | [I(e)](images/console-input.png) |
| `console_read_line` | [L286](../../kernel/console/console.cpp#L286) | [讲解](DEVICES_IO_FUNCTIONS.md#控制台其它函数逐一对照--idieif)（讲义第60行） | [I(d)、I(f)](images/console-input.png) |
| `console_read_line_with_history` | [L290](../../kernel/console/console.cpp#L290) | [讲解](DEVICES_IO_FUNCTIONS.md#consoleconsole_read_line_with_historybuffer-capacity-history--idieif) | [I(d)、I(e)、I(f)](images/console-input.png) |

### kernel/core/kernel_main.cpp

94 个定义；已讲解 94 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `timer_wait_running_smoke_ticks` | [L39](../../kernel/core/kernel_main.cpp#L39) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第178行） | [K(d)](images/kernel-bringup.png) |
| `KernelObjectProbe::KernelObjectProbe` | [L357](../../kernel/core/kernel_main.cpp#L357) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第179行） | [H(d)](images/heap-memory.png) |
| `KernelObjectProbe::~KernelObjectProbe` | [L365](../../kernel/core/kernel_main.cpp#L365) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第180行） | [H(d)](images/heap-memory.png) |
| `KernelObjectProbe::is_valid` | [L370](../../kernel/core/kernel_main.cpp#L370) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第181行） | [H(d)](images/heap-memory.png) |
| `KernelAlignedObjectProbe::KernelAlignedObjectProbe` | [L382](../../kernel/core/kernel_main.cpp#L382) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第182行） | [H(d)](images/heap-memory.png) |
| `KernelAlignedObjectProbe::~KernelAlignedObjectProbe` | [L388](../../kernel/core/kernel_main.cpp#L388) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第183行） | [H(d)](images/heap-memory.png) |
| `KernelAlignedObjectProbe::is_valid` | [L394](../../kernel/core/kernel_main.cpp#L394) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第184行） | [H(d)](images/heap-memory.png) |
| `out8` | [L464](../../kernel/core/kernel_main.cpp#L464) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第185行） | [K(f)](images/kernel-bringup.png) |
| `in8` | [L469](../../kernel/core/kernel_main.cpp#L469) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第186行） | [K(f)](images/kernel-bringup.png) |
| `serial_write_char` | [L476](../../kernel/core/kernel_main.cpp#L476) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第187行） | [K(f)](images/kernel-bringup.png) |
| `serial_write_string` | [L484](../../kernel/core/kernel_main.cpp#L484) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第188行） | [K(f)](images/kernel-bringup.png) |
| `serial_write_crlf` | [L491](../../kernel/core/kernel_main.cpp#L491) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第189行） | [K(f)](images/kernel-bringup.png) |
| `shell_output_char` | [L500](../../kernel/core/kernel_main.cpp#L500) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第190行） | [K(f)](images/kernel-bringup.png) |
| `shell_clear_output` | [L511](../../kernel/core/kernel_main.cpp#L511) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第191行） | [K(f)](images/kernel-bringup.png) |
| `shell_set_output_color` | [L515](../../kernel/core/kernel_main.cpp#L515) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第192行） | [K(f)](images/kernel-bringup.png) |
| `syscall_output_write` | [L519](../../kernel/core/kernel_main.cpp#L519) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第193行） | [S(f)](images/syscall-boundary.png)；[K(f)](images/kernel-bringup.png) |
| `serial_write_hex_nibble` | [L556](../../kernel/core/kernel_main.cpp#L556) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第194行） | [K(f)](images/kernel-bringup.png) |
| `serial_write_hex64` | [L566](../../kernel/core/kernel_main.cpp#L566) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第195行） | [K(f)](images/kernel-bringup.png) |
| `serial_write_bounded_string` | [L573](../../kernel/core/kernel_main.cpp#L573) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第196行） | [K(f)](images/kernel-bringup.png) |
| `serial_write_u64` | [L584](../../kernel/core/kernel_main.cpp#L584) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第197行） | [K(f)](images/kernel-bringup.png) |
| `serial_write_i64` | [L603](../../kernel/core/kernel_main.cpp#L603) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第198行） | [K(f)](images/kernel-bringup.png) |
| `read_rflags` | [L613](../../kernel/core/kernel_main.cpp#L613) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第199行） | [K(a)](images/kernel-bringup.png) |
| `is_aligned` | [L619](../../kernel/core/kernel_main.cpp#L619) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第200行） | [H(b)](images/heap-memory.png) |
| `string_length` | [L627](../../kernel/core/kernel_main.cpp#L627) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第201行） | [K(f)](images/kernel-bringup.png) |
| `strings_equal` | [L640](../../kernel/core/kernel_main.cpp#L640) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第202行） | [K(f)](images/kernel-bringup.png) |
| `bounded_text_equals` | [L656](../../kernel/core/kernel_main.cpp#L656) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第203行） | [K(f)](images/kernel-bringup.png) |
| `page_is_accessible_to_kernel` | [L676](../../kernel/core/kernel_main.cpp#L676) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第204行） | [V(b)](images/paging-address-space.png) |
| `user_mode_smoke_program_bytes` | [L680](../../kernel/core/kernel_main.cpp#L680) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第205行） | [U(c)](images/elf-user-abi.png) |
| `user_mode_smoke_program_size` | [L684](../../kernel/core/kernel_main.cpp#L684) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第206行） | [U(c)](images/elf-user-abi.png) |
| `user_mode_yield_program_bytes` | [L692](../../kernel/core/kernel_main.cpp#L692) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第207行） | [U(c)](images/elf-user-abi.png) |
| `user_mode_yield_program_size` | [L696](../../kernel/core/kernel_main.cpp#L696) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第208行） | [U(c)](images/elf-user-abi.png) |
| `load_user_program_file` | [L707](../../kernel/core/kernel_main.cpp#L707) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第209行） | [U(a)、U(c)](images/elf-user-abi.png) |
| `vga_fill_line` | [L769](../../kernel/core/kernel_main.cpp#L769) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第210行） | [K(f)](images/kernel-bringup.png) |
| `vga_write_text_at` | [L779](../../kernel/core/kernel_main.cpp#L779) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第211行） | [K(f)](images/kernel-bringup.png) |
| `vga_write_centered_line` | [L795](../../kernel/core/kernel_main.cpp#L795) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第212行） | [K(f)](images/kernel-bringup.png) |
| `vga_write_line` | [L812](../../kernel/core/kernel_main.cpp#L812) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第213行） | [K(f)](images/kernel-bringup.png) |
| `vga_write_inset_line` | [L817](../../kernel/core/kernel_main.cpp#L817) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第214行） | [K(f)](images/kernel-bringup.png) |
| `draw_terminal_chrome` | [L825](../../kernel/core/kernel_main.cpp#L825) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第215行） | [K(f)](images/kernel-bringup.png) |
| `draw_terminal_overview_panel` | [L839](../../kernel/core/kernel_main.cpp#L839) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第216行） | [K(f)](images/kernel-bringup.png) |
| `write_status_line` | [L865](../../kernel/core/kernel_main.cpp#L865) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第217行） | [K(f)](images/kernel-bringup.png) |
| `memory_kind_name` | [L872](../../kernel/core/kernel_main.cpp#L872) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第218行） | [A(a)](images/physical-pages.png) |
| `is_boot_info_valid` | [L881](../../kernel/core/kernel_main.cpp#L881) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第219行） | [K(a)](images/kernel-bringup.png) |
| `log_e820_entries` | [L888](../../kernel/core/kernel_main.cpp#L888) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第220行） | [A(a)](images/physical-pages.png)；[K(f)](images/kernel-bringup.png) |
| `log_allocator_ranges` | [L911](../../kernel/core/kernel_main.cpp#L911) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第221行） | [A(f)](images/physical-pages.png) |
| `log_allocated_pages` | [L925](../../kernel/core/kernel_main.cpp#L925) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第222行） | [A(c)](images/physical-pages.png) |
| `run_tss_smoke_test` | [L942](../../kernel/core/kernel_main.cpp#L942) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第223行） | [K(a)](images/kernel-bringup.png) |
| `run_direct_map_smoke_test` | [L980](../../kernel/core/kernel_main.cpp#L980) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第224行） | [A(d)](images/physical-pages.png)；[V(b)](images/paging-address-space.png) |
| `run_paging_smoke_test` | [L1003](../../kernel/core/kernel_main.cpp#L1003) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第225行） | [V(c)](images/paging-address-space.png) |
| `run_address_space_smoke_test` | [L1043](../../kernel/core/kernel_main.cpp#L1043) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第226行） | [V(d)、V(e)](images/paging-address-space.png) |
| `run_user_mode_smoke_test` | [L1125](../../kernel/core/kernel_main.cpp#L1125) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第227行） | [K(c)](images/kernel-bringup.png)；[U(c)](images/elf-user-abi.png) |
| `run_user_file_program_smoke_test` | [L1259](../../kernel/core/kernel_main.cpp#L1259) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第228行） | [U(a)、U(c)](images/elf-user-abi.png) |
| `run_user_elf_program_smoke_test` | [L1394](../../kernel/core/kernel_main.cpp#L1394) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第229行） | [U(a)、U(b)、U(c)](images/elf-user-abi.png) |
| `run_scheduler_elf_thread_smoke_test` | [L1551](../../kernel/core/kernel_main.cpp#L1551) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第230行） | [K(d)](images/kernel-bringup.png)；[U(d)](images/elf-user-abi.png) |
| `run_heap_smoke_test` | [L1728](../../kernel/core/kernel_main.cpp#L1728) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第231行） | [H(a)、H(b)、H(c)](images/heap-memory.png) |
| `run_kernel_memory_smoke_test` | [L1834](../../kernel/core/kernel_main.cpp#L1834) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第232行） | [H(d)、H(f)](images/heap-memory.png) |
| `read_inode_text` | [L1967](../../kernel/core/kernel_main.cpp#L1967) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第233行） | [K(c)](images/kernel-bringup.png) |
| `run_boot_volume_smoke_test` | [L1993](../../kernel/core/kernel_main.cpp#L1993) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第234行） | [K(c)](images/kernel-bringup.png) |
| `run_filesystem_smoke_test` | [L2054](../../kernel/core/kernel_main.cpp#L2054) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第235行） | [K(c)](images/kernel-bringup.png) |
| `run_file_handle_smoke_test` | [L2295](../../kernel/core/kernel_main.cpp#L2295) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第236行） | [K(c)](images/kernel-bringup.png) |
| `run_directory_handle_smoke_test` | [L2398](../../kernel/core/kernel_main.cpp#L2398) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第237行） | [K(c)](images/kernel-bringup.png) |
| `run_vfs_smoke_test` | [L2525](../../kernel/core/kernel_main.cpp#L2525) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第238行） | [K(c)](images/kernel-bringup.png) |
| `run_filesystem_write_smoke_test` | [L2629](../../kernel/core/kernel_main.cpp#L2629) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第239行） | [K(c)、K(d)](images/kernel-bringup.png) |
| `run_file_descriptor_smoke_test` | [L2859](../../kernel/core/kernel_main.cpp#L2859) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第240行） | [S(d)](images/syscall-boundary.png)；[K(c)](images/kernel-bringup.png) |
| `invoke_int80_syscall` | [L2952](../../kernel/core/kernel_main.cpp#L2952) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第241行） | [S(a)](images/syscall-boundary.png) |
| `run_syscall_smoke_test` | [L2973](../../kernel/core/kernel_main.cpp#L2973) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第242行） | [S(d)、S(f)](images/syscall-boundary.png) |
| `run_int80_syscall_smoke_test` | [L3185](../../kernel/core/kernel_main.cpp#L3185) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第243行） | [S(a)、S(b)、S(f)](images/syscall-boundary.png) |
| `run_timer_smoke_test` | [L3444](../../kernel/core/kernel_main.cpp#L3444) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第244行） | [K(d)](images/kernel-bringup.png) |
| `append_scheduler_trace` | [L3526](../../kernel/core/kernel_main.cpp#L3526) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第245行） | [K(d)](images/kernel-bringup.png) |
| `observe_scheduler_thread_tid` | [L3540](../../kernel/core/kernel_main.cpp#L3540) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第246行） | [K(d)](images/kernel-bringup.png) |
| `run_scheduler_phase_until_idle` | [L3552](../../kernel/core/kernel_main.cpp#L3552) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第247行） | [K(d)](images/kernel-bringup.png) |
| `scheduler_priority_thread_entry` | [L3559](../../kernel/core/kernel_main.cpp#L3559) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第248行） | [K(d)](images/kernel-bringup.png) |
| `scheduler_sleep_thread_entry` | [L3580](../../kernel/core/kernel_main.cpp#L3580) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第249行） | [K(d)](images/kernel-bringup.png) |
| `scheduler_blocked_thread_entry` | [L3598](../../kernel/core/kernel_main.cpp#L3598) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第250行） | [K(d)](images/kernel-bringup.png) |
| `scheduler_wake_thread_entry` | [L3621](../../kernel/core/kernel_main.cpp#L3621) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第251行） | [K(d)](images/kernel-bringup.png) |
| `scheduler_user_yield_helper_thread_entry` | [L3642](../../kernel/core/kernel_main.cpp#L3642) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第252行） | [K(d)](images/kernel-bringup.png) |
| `run_scheduler_smoke_test` | [L3690](../../kernel/core/kernel_main.cpp#L3690) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第253行） | [K(d)](images/kernel-bringup.png) |
| `run_scheduler_user_thread_smoke_test` | [L4012](../../kernel/core/kernel_main.cpp#L4012) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第254行） | [K(d)](images/kernel-bringup.png)；[S(b)](images/syscall-boundary.png) |
| `stdin_blocking_reader_thread_entry` | [L4405](../../kernel/core/kernel_main.cpp#L4405) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第255行） | [K(d)](images/kernel-bringup.png)；[S(e)](images/syscall-boundary.png) |
| `stdin_blocking_injector_thread_entry` | [L4419](../../kernel/core/kernel_main.cpp#L4419) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第256行） | [K(d)](images/kernel-bringup.png) |
| `run_stdin_blocking_scheduler_smoke_test` | [L4431](../../kernel/core/kernel_main.cpp#L4431) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第257行） | [K(d)](images/kernel-bringup.png)；[S(e)](images/syscall-boundary.png) |
| `wait_for_keyboard_irq_count` | [L4517](../../kernel/core/kernel_main.cpp#L4517) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第258行） | [K(d)](images/kernel-bringup.png) |
| `run_keyboard_smoke_test` | [L4532](../../kernel/core/kernel_main.cpp#L4532) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第259行） | [K(d)](images/kernel-bringup.png) |
| `run_stdin_syscall_smoke_test` | [L4646](../../kernel/core/kernel_main.cpp#L4646) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第260行） | [K(d)](images/kernel-bringup.png)；[S(e)](images/syscall-boundary.png) |
| `run_console_input_smoke_test` | [L4797](../../kernel/core/kernel_main.cpp#L4797) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第261行） | [K(d)](images/kernel-bringup.png) |
| `inject_scancode_sequence` | [L4957](../../kernel/core/kernel_main.cpp#L4957) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第262行） | [K(d)](images/kernel-bringup.png) |
| `run_shell_smoke_test` | [L4988](../../kernel/core/kernel_main.cpp#L4988) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第263行） | [K(d)](images/kernel-bringup.png) |
| `SmokeSchedulerStorage::~SmokeSchedulerStorage` | [L5000](../../kernel/core/kernel_main.cpp#L5000) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第264行） | [K(d)](images/kernel-bringup.png)；[H(d)](images/heap-memory.png) |
| `kernel_shell_thread_entry` | [L5070](../../kernel/core/kernel_main.cpp#L5070) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第265行） | [K(e)](images/kernel-bringup.png) |
| `network_worker_entry` | [L5094](../../kernel/core/kernel_main.cpp#L5094) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第266行） | [K(e)](images/kernel-bringup.png) |
| `start_kernel_shell_under_scheduler` | [L5108](../../kernel/core/kernel_main.cpp#L5108) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第267行） | [K(e)](images/kernel-bringup.png) |
| `kernel_user_mode_exit_is_armed` | [L5181](../../kernel/core/kernel_main.cpp#L5181) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第268行） | [S(e)](images/syscall-boundary.png)；[U(d)](images/elf-user-abi.png) |
| `kernel_handle_user_mode_exit` | [L5191](../../kernel/core/kernel_main.cpp#L5191) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第269行） | [U(d)](images/elf-user-abi.png) |
| `kernel_main` | [L5221](../../kernel/core/kernel_main.cpp#L5221) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第270行） | [K(a)、K(f)](images/kernel-bringup.png) |
| `kernel_handle_exception` | [L5565](../../kernel/core/kernel_main.cpp#L5565) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelcorekernel_maincpp)（讲义第271行） | [K(f)](images/kernel-bringup.png)；[S(e)](images/syscall-boundary.png) |

### kernel/cpu/cpu.cpp

6 个定义；已讲解 6 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `cpuid` | [L9](../../kernel/cpu/cpu.cpp#L9) | [讲解](SMP_BOOT_FUNCTIONS.md#其余-cpu-函数--bege)（讲义第233行） | [B(e)](images/smp-boot.png) |
| `cpu_initialize` | [L17](../../kernel/cpu/cpu.cpp#L17) | [讲解](SMP_BOOT_FUNCTIONS.md#cpu_initialize--be) | [B(e)](images/smp-boot.png) |
| `cpu_information` | [L77](../../kernel/cpu/cpu.cpp#L77) | [讲解](SMP_BOOT_FUNCTIONS.md#其余-cpu-函数--bege)（讲义第234行） | [B(e)](images/smp-boot.png) |
| `cpu_initialize_local` | [L78](../../kernel/cpu/cpu.cpp#L78) | [讲解](SMP_BOOT_FUNCTIONS.md#其余-cpu-函数--bege)（讲义第235行） | [B(e)](images/smp-boot.png) |
| `cpu_initialize_floating_state` | [L90](../../kernel/cpu/cpu.cpp#L90) | [讲解](SMP_BOOT_FUNCTIONS.md#其余-cpu-函数--bege)（讲义第236行） | [B(e)](images/smp-boot.png) |
| `cpu_read_tsc` | [L93](../../kernel/cpu/cpu.cpp#L93) | [讲解](SMP_BOOT_FUNCTIONS.md#其余-cpu-函数--bege)（讲义第237行） | [G(e)](images/kernel-gate.png) |

### kernel/cpu/smp.cpp

23 个定义；已讲解 23 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `hardware_apic_id` | [L47](../../kernel/cpu/smp.cpp#L47) | [讲解](SMP_BOOT_FUNCTIONS.md#设备与启动辅助函数--babbtf)（讲义第97行） | [B(a)](images/smp-boot.png) |
| `read_msr` | [L52](../../kernel/cpu/smp.cpp#L52) | [讲解](SMP_BOOT_FUNCTIONS.md#设备与启动辅助函数--babbtf)（讲义第98行） | [B(a)](images/smp-boot.png) |
| `write_msr` | [L57](../../kernel/cpu/smp.cpp#L57) | [讲解](SMP_BOOT_FUNCTIONS.md#设备与启动辅助函数--babbtf)（讲义第99行） | [B(a)](images/smp-boot.png) |
| `apic_read` | [L63](../../kernel/cpu/smp.cpp#L63) | [讲解](SMP_BOOT_FUNCTIONS.md#设备与启动辅助函数--babbtf)（讲义第100行） | [B(a)](images/smp-boot.png) |
| `apic_write` | [L66](../../kernel/cpu/smp.cpp#L66) | [讲解](SMP_BOOT_FUNCTIONS.md#设备与启动辅助函数--babbtf)（讲义第101行） | [B(a)](images/smp-boot.png) |
| `local_apic_initialize` | [L70](../../kernel/cpu/smp.cpp#L70) | [讲解](SMP_BOOT_FUNCTIONS.md#local_apic_initializebsp--bage) | [B(a)](images/smp-boot.png)；[G(e)](images/kernel-gate.png) |
| `wait_delivery` | [L87](../../kernel/cpu/smp.cpp#L87) | [讲解](SMP_BOOT_FUNCTIONS.md#send_ipiapic_id-command-与-wait_delivery--bbgf)（讲义第88行） | [B(b)](images/smp-boot.png)；[G(f)](images/kernel-gate.png) |
| `send_ipi` | [L95](../../kernel/cpu/smp.cpp#L95) | [讲解](SMP_BOOT_FUNCTIONS.md#send_ipiapic_id-command-与-wait_delivery--bbgf)（讲义第89行） | [B(b)](images/smp-boot.png)；[G(f)](images/kernel-gate.png) |
| `delay_tick` | [L102](../../kernel/cpu/smp.cpp#L102) | [讲解](SMP_BOOT_FUNCTIONS.md#设备与启动辅助函数--babbtf)（讲义第102行） | [B(b)](images/smp-boot.png) |
| `read_firmware` | [L107](../../kernel/cpu/smp.cpp#L107) | [讲解](SMP_BOOT_FUNCTIONS.md#设备与启动辅助函数--babbtf)（讲义第103行） | [T(a)](images/topology.png) |
| `calibrate_timer` | [L113](../../kernel/cpu/smp.cpp#L113) | [讲解](SMP_BOOT_FUNCTIONS.md#calibrate_timer--bage) | [B(a)](images/smp-boot.png)；[G(e)](images/kernel-gate.png) |
| `park_cpu` | [L127](../../kernel/cpu/smp.cpp#L127) | [讲解](SMP_BOOT_FUNCTIONS.md#设备与启动辅助函数--babbtf)（讲义第104行） | [B(f)](images/smp-boot.png) |
| `smp_ap_entry` | [L131](../../kernel/cpu/smp.cpp#L131) | [讲解](SMP_BOOT_FUNCTIONS.md#smp_ap_entryindex--bdbebf) | [B(d)、B(e)、B(f)](images/smp-boot.png) |
| `smp_current_cpu_index` | [L153](../../kernel/cpu/smp.cpp#L153) | [讲解](SMP_BOOT_FUNCTIONS.md#smpcpp运行期查询门铃与观测)（讲义第147行） | [G(b)](images/kernel-gate.png) |
| `smp_is_enabled` | [L159](../../kernel/cpu/smp.cpp#L159) | [讲解](SMP_BOOT_FUNCTIONS.md#smpcpp运行期查询门铃与观测)（讲义第148行） | [G(b)](images/kernel-gate.png) |
| `smp_online_cpu_count` | [L162](../../kernel/cpu/smp.cpp#L162) | [讲解](SMP_BOOT_FUNCTIONS.md#smpcpp运行期查询门铃与观测)（讲义第149行） | [B(f)](images/smp-boot.png) |
| `kernel_gate_enter` | [L167](../../kernel/cpu/smp.cpp#L167) | [讲解](SMP_BOOT_FUNCTIONS.md#kernel_gate_enter--gbgc) | [G(b)、G(c)](images/kernel-gate.png) |
| `kernel_gate_leave_user` | [L188](../../kernel/cpu/smp.cpp#L188) | [讲解](SMP_BOOT_FUNCTIONS.md#kernel_gate_leave_user--gd-的用户出口) | [G(d)](images/kernel-gate.png) |
| `kernel_gate_release_idle` | [L196](../../kernel/cpu/smp.cpp#L196) | [讲解](SMP_BOOT_FUNCTIONS.md#kernel_gate_release_idle--gd-的空闲出口) | [G(d)](images/kernel-gate.png) |
| `smp_local_apic_eoi` | [L200](../../kernel/cpu/smp.cpp#L200) | [讲解](SMP_BOOT_FUNCTIONS.md#smpcpp运行期查询门铃与观测)（讲义第150行） | [G(f)](images/kernel-gate.png) |
| `smp_send_reschedule` | [L204](../../kernel/cpu/smp.cpp#L204) | [讲解](SMP_BOOT_FUNCTIONS.md#smpcpp运行期查询门铃与观测)（讲义第151行） | [G(f)](images/kernel-gate.png) |
| `smp_snapshot` | [L211](../../kernel/cpu/smp.cpp#L211) | [讲解](SMP_BOOT_FUNCTIONS.md#smpcpp运行期查询门铃与观测)（讲义第152行） | [G(a)、G(e)](images/kernel-gate.png) |
| `smp_initialize` | [L234](../../kernel/cpu/smp.cpp#L234) | [讲解](SMP_BOOT_FUNCTIONS.md#smp_initializescheduler-allocator--babbbf) | [B(a)、B(b)、B(f)](images/smp-boot.png) |

### kernel/cpu/topology.cpp

12 个定义；已讲解 12 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `u16` | [L4](../../kernel/cpu/topology.cpp#L4) | [讲解](SMP_BOOT_FUNCTIONS.md#字节解析与校验辅助函数--tetc)（讲义第202行） | [T(e)](images/topology.png) |
| `u32` | [L5](../../kernel/cpu/topology.cpp#L5) | [讲解](SMP_BOOT_FUNCTIONS.md#字节解析与校验辅助函数--tetc)（讲义第203行） | [T(e)](images/topology.png) |
| `u64` | [L6](../../kernel/cpu/topology.cpp#L6) | [讲解](SMP_BOOT_FUNCTIONS.md#字节解析与校验辅助函数--tetc)（讲义第204行） | [T(e)](images/topology.png) |
| `signature` | [L7](../../kernel/cpu/topology.cpp#L7) | [讲解](SMP_BOOT_FUNCTIONS.md#字节解析与校验辅助函数--tetc)（讲义第205行） | [T(e)](images/topology.png) |
| `checksum` | [L15](../../kernel/cpu/topology.cpp#L15) | [讲解](SMP_BOOT_FUNCTIONS.md#字节解析与校验辅助函数--tetc)（讲义第206行） | [T(e)](images/topology.png) |
| `add_cpu` | [L21](../../kernel/cpu/topology.cpp#L21) | [讲解](SMP_BOOT_FUNCTIONS.md#字节解析与校验辅助函数--tetc)（讲义第207行） | [T(c)](images/topology.png) |
| `sdt` | [L29](../../kernel/cpu/topology.cpp#L29) | [讲解](SMP_BOOT_FUNCTIONS.md#字节解析与校验辅助函数--tetc)（讲义第208行） | [T(b)](images/topology.png) |
| `madt` | [L40](../../kernel/cpu/topology.cpp#L40) | [讲解](SMP_BOOT_FUNCTIONS.md#madtread-context-physical-bsp-output--tctf) | [T(c)、T(f)](images/topology.png) |
| `acpi` | [L78](../../kernel/cpu/topology.cpp#L78) | [讲解](SMP_BOOT_FUNCTIONS.md#acpiread-context-rsdp-bsp-output--tb) | [T(b)](images/topology.png) |
| `mp` | [L107](../../kernel/cpu/topology.cpp#L107) | [讲解](SMP_BOOT_FUNCTIONS.md#mpread-context-floating-bsp-output--td) | [T(d)](images/topology.png) |
| `scan` | [L145](../../kernel/cpu/topology.cpp#L145) | [讲解](SMP_BOOT_FUNCTIONS.md#scanread-context-start-end-want_acpi-bsp-output--ta) | [T(a)](images/topology.png) |
| `firmware_find_cpu_topology` | [L172](../../kernel/cpu/topology.cpp#L172) | [讲解](SMP_BOOT_FUNCTIONS.md#firmware_find_cpu_topologyread-context-bsp-output--tatf) | [T(a)、T(f)](images/topology.png) |

### kernel/cpu/xapic.hpp

2 个定义；已讲解 2 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `xapic_unicast_id_valid` | [L5](../../kernel/cpu/xapic.hpp#L5) | [讲解](SMP_BOOT_FUNCTIONS.md#xapic_unicast_id_validid-与-xapic_base_supportedmsr-physical--tf)（讲义第214行） | [T(f)](images/topology.png) |
| `xapic_base_supported` | [L8](../../kernel/cpu/xapic.hpp#L8) | [讲解](SMP_BOOT_FUNCTIONS.md#xapic_unicast_id_validid-与-xapic_base_supportedmsr-physical--tf)（讲义第215行） | [T(f)](images/topology.png) |

### kernel/device/pci.cpp

7 个定义；已讲解 7 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `address` | [L4](../../kernel/device/pci.cpp#L4) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第140行） | [H(a)](images/interrupt-devices.png) |
| `select` | [L9](../../kernel/device/pci.cpp#L9) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第141行） | [H(a)](images/interrupt-devices.png) |
| `pci_read32` | [L14](../../kernel/device/pci.cpp#L14) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第142行） | [H(a)](images/interrupt-devices.png) |
| `pci_read16` | [L20](../../kernel/device/pci.cpp#L20) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第143行） | [H(a)](images/interrupt-devices.png) |
| `pci_write16` | [L27](../../kernel/device/pci.cpp#L27) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第144行） | [H(a)](images/interrupt-devices.png) |
| `pci_find_device` | [L33](../../kernel/device/pci.cpp#L33) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第145行） | [H(a)](images/interrupt-devices.png) |
| `pci_disable_msix` | [L52](../../kernel/device/pci.cpp#L52) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第146行） | [H(a)、H(b)](images/interrupt-devices.png) |

### kernel/fs/directory.cpp

8 个定义；已讲解 8 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `copy_dir_entry_to_public_entry` | [L8](../../kernel/fs/directory.cpp#L8) | [讲解](FILESYSTEM_FUNCTIONS.md#directorycopy_dir_entry_to_public_entry) | [V(d)](images/vfs-paths.png) |
| `directory_open` | [L32](../../kernel/fs/directory.cpp#L32) | [讲解](FILESYSTEM_FUNCTIONS.md#directorydirectory_open) | [V(d)](images/vfs-paths.png) |
| `directory_is_open` | [L62](../../kernel/fs/directory.cpp#L62) | [讲解](FILESYSTEM_FUNCTIONS.md#directorydirectory_is_open) | [V(d)](images/vfs-paths.png) |
| `directory_close` | [L69](../../kernel/fs/directory.cpp#L69) | [讲解](FILESYSTEM_FUNCTIONS.md#directorydirectory_close) | [V(d)](images/vfs-paths.png) |
| `directory_entry_count` | [L78](../../kernel/fs/directory.cpp#L78) | [讲解](FILESYSTEM_FUNCTIONS.md#directorydirectory_entry_count) | [V(d)](images/vfs-paths.png) |
| `directory_read` | [L86](../../kernel/fs/directory.cpp#L86) | [讲解](FILESYSTEM_FUNCTIONS.md#directorydirectory_read) | [V(d)](images/vfs-paths.png) |
| `directory_rewind` | [L109](../../kernel/fs/directory.cpp#L109) | [讲解](FILESYSTEM_FUNCTIONS.md#directorydirectory_rewind) | [V(d)](images/vfs-paths.png) |
| `directory_tell` | [L118](../../kernel/fs/directory.cpp#L118) | [讲解](FILESYSTEM_FUNCTIONS.md#directorydirectory_tell) | [V(d)](images/vfs-paths.png) |

### kernel/fs/fd.cpp

36 个定义；已讲解 36 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `save_interrupt_flags_and_disable` | [L19](../../kernel/fs/fd.cpp#L19) | [讲解](COOPERATION_FUNCTIONS.md#save_interrupt_flags_and_disable) | [P(e)](images/pipe-cooperation.png) |
| `restore_interrupt_flags` | [L24](../../kernel/fs/fd.cpp#L24) | [讲解](COOPERATION_FUNCTIONS.md#restore_interrupt_flags) | [P(e)](images/pipe-cooperation.png) |
| `valid` | [L27](../../kernel/fs/fd.cpp#L27) | [讲解](COOPERATION_FUNCTIONS.md#valid) | [P(a)](images/pipe-cooperation.png) |
| `structural` | [L28](../../kernel/fs/fd.cpp#L28) | [讲解](COOPERATION_FUNCTIONS.md#structural) | [P(a)](images/pipe-cooperation.png) |
| `description` | [L29](../../kernel/fs/fd.cpp#L29) | [讲解](COOPERATION_FUNCTIONS.md#description) | [P(a)](images/pipe-cooperation.png) |
| `empty_slot` | [L32](../../kernel/fs/fd.cpp#L32) | [讲解](COOPERATION_FUNCTIONS.md#empty_slot) | [P(a)](images/pipe-cooperation.png) |
| `wake` | [L36](../../kernel/fs/fd.cpp#L36) | [讲解](COOPERATION_FUNCTIONS.md#wake) | [P(e)](images/pipe-cooperation.png) |
| `wait_on` | [L42](../../kernel/fs/fd.cpp#L42) | [讲解](COOPERATION_FUNCTIONS.md#wait_on) | [P(e)](images/pipe-cooperation.png) |
| `pipe_read` | [L55](../../kernel/fs/fd.cpp#L55) | [讲解](COOPERATION_FUNCTIONS.md#pipe_read) | [P(d)、P(e)、P(f)](images/pipe-cooperation.png) |
| `pipe_write` | [L77](../../kernel/fs/fd.cpp#L77) | [讲解](COOPERATION_FUNCTIONS.md#pipe_write) | [P(c)、P(e)、P(f)](images/pipe-cooperation.png) |
| `release` | [L106](../../kernel/fs/fd.cpp#L106) | [讲解](COOPERATION_FUNCTIONS.md#release) | [P(b)、P(f)](images/pipe-cooperation.png) |
| `attach` | [L120](../../kernel/fs/fd.cpp#L120) | [讲解](COOPERATION_FUNCTIONS.md#attach) | [P(a)、P(b)](images/pipe-cooperation.png) |
| `fd_public_to_slot` | [L125](../../kernel/fs/fd.cpp#L125) | [讲解](COOPERATION_FUNCTIONS.md#fd_public_to_slot) | [P(a)](images/pipe-cooperation.png) |
| `fd_slot_to_public` | [L129](../../kernel/fs/fd.cpp#L129) | [讲解](COOPERATION_FUNCTIONS.md#fd_slot_to_public) | [P(a)](images/pipe-cooperation.png) |
| `initialize_file_descriptor_table` | [L132](../../kernel/fs/fd.cpp#L132) | [讲解](COOPERATION_FUNCTIONS.md#initialize_file_descriptor_table) | [P(a)](images/pipe-cooperation.png) |
| `file_descriptor_table_is_ready` | [L138](../../kernel/fs/fd.cpp#L138) | [讲解](COOPERATION_FUNCTIONS.md#file_descriptor_table_is_ready) | [P(a)](images/pipe-cooperation.png) |
| `fd_kind` | [L139](../../kernel/fs/fd.cpp#L139) | [讲解](COOPERATION_FUNCTIONS.md#fd_kind) | [P(a)](images/pipe-cooperation.png) |
| `fd_is_open` | [L145](../../kernel/fs/fd.cpp#L145) | [讲解](COOPERATION_FUNCTIONS.md#fd_is_open) | [P(a)](images/pipe-cooperation.png) |
| `fd_terminal_number` | [L146](../../kernel/fs/fd.cpp#L146) | [讲解](COOPERATION_FUNCTIONS.md#fd_terminal_number) | [P(a)](images/pipe-cooperation.png) |
| `fd_open` | [L150](../../kernel/fs/fd.cpp#L150) | [讲解](COOPERATION_FUNCTIONS.md#fd_open) | [P(a)](images/pipe-cooperation.png) |
| `fd_can_read` | [L167](../../kernel/fs/fd.cpp#L167) | [讲解](COOPERATION_FUNCTIONS.md#fd_can_read) | [P(a)](images/pipe-cooperation.png) |
| `fd_can_write` | [L173](../../kernel/fs/fd.cpp#L173) | [讲解](COOPERATION_FUNCTIONS.md#fd_can_write) | [P(a)](images/pipe-cooperation.png) |
| `fd_read` | [L178](../../kernel/fs/fd.cpp#L178) | [讲解](COOPERATION_FUNCTIONS.md#fd_read) | [P(d)](images/pipe-cooperation.png) |
| `fd_write` | [L185](../../kernel/fs/fd.cpp#L185) | [讲解](COOPERATION_FUNCTIONS.md#fd_write) | [P(c)](images/pipe-cooperation.png) |
| `fd_close` | [L205](../../kernel/fs/fd.cpp#L205) | [讲解](COOPERATION_FUNCTIONS.md#fd_close) | [P(f)](images/pipe-cooperation.png) |
| `fd_close_nonstandard` | [L213](../../kernel/fs/fd.cpp#L213) | [讲解](COOPERATION_FUNCTIONS.md#fd_close_nonstandard) | [P(f)](images/pipe-cooperation.png) |
| `fd_close_all` | [L216](../../kernel/fs/fd.cpp#L216) | [讲解](COOPERATION_FUNCTIONS.md#fd_close_all) | [P(f)](images/pipe-cooperation.png) |
| `fd_inherit` | [L219](../../kernel/fs/fd.cpp#L219) | [讲解](COOPERATION_FUNCTIONS.md#fd_inherit) | [P(b)](images/pipe-cooperation.png)；[M(a)](images/parallel-workers.png) |
| `fd_dup2` | [L230](../../kernel/fs/fd.cpp#L230) | [讲解](COOPERATION_FUNCTIONS.md#fd_dup2) | [P(b)](images/pipe-cooperation.png) |
| `fd_dup` | [L244](../../kernel/fs/fd.cpp#L244) | [讲解](COOPERATION_FUNCTIONS.md#fd_dup) | [P(b)](images/pipe-cooperation.png) |
| `fd_pipe` | [L248](../../kernel/fs/fd.cpp#L248) | [讲解](COOPERATION_FUNCTIONS.md#fd_pipe) | [P(a)](images/pipe-cooperation.png) |
| `fd_references_inode` | [L262](../../kernel/fs/fd.cpp#L262) | [讲解](COOPERATION_FUNCTIONS.md#fd_references_inode) | [P(f)](images/pipe-cooperation.png) |
| `fd_stat` | [L270](../../kernel/fs/fd.cpp#L270) | [讲解](COOPERATION_FUNCTIONS.md#fd_stat) | [P(a)](images/pipe-cooperation.png) |
| `fd_seek` | [L273](../../kernel/fs/fd.cpp#L273) | [讲解](COOPERATION_FUNCTIONS.md#fd_seek) | [P(b)](images/pipe-cooperation.png) |
| `fd_tell` | [L276](../../kernel/fs/fd.cpp#L276) | [讲解](COOPERATION_FUNCTIONS.md#fd_tell) | [P(b)](images/pipe-cooperation.png) |
| `fd_open_count` | [L279](../../kernel/fs/fd.cpp#L279) | [讲解](COOPERATION_FUNCTIONS.md#fd_open_count) | [P(a)](images/pipe-cooperation.png) |

### kernel/fs/file.cpp

9 个定义；已讲解 9 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `copy_inode_to_stat` | [L9](../../kernel/fs/file.cpp#L9) | [讲解](FILESYSTEM_FUNCTIONS.md#filecopy_inode_to_stat) | [V(a)](images/vfs-paths.png) |
| `file_open` | [L31](../../kernel/fs/file.cpp#L31) | [讲解](FILESYSTEM_FUNCTIONS.md#filefile_open) | [V(c)](images/vfs-paths.png) |
| `file_is_open` | [L60](../../kernel/fs/file.cpp#L60) | [讲解](FILESYSTEM_FUNCTIONS.md#filefile_is_open) | [V(c)](images/vfs-paths.png) |
| `file_close` | [L67](../../kernel/fs/file.cpp#L67) | [讲解](FILESYSTEM_FUNCTIONS.md#filefile_close) | [V(c)](images/vfs-paths.png) |
| `file_stat` | [L76](../../kernel/fs/file.cpp#L76) | [讲解](FILESYSTEM_FUNCTIONS.md#filefile_stat) | [V(a)](images/vfs-paths.png) |
| `file_handle_stat` | [L92](../../kernel/fs/file.cpp#L92) | [讲解](FILESYSTEM_FUNCTIONS.md#filefile_handle_stat) | [V(e)](images/vfs-paths.png) |
| `file_read` | [L102](../../kernel/fs/file.cpp#L102) | [讲解](FILESYSTEM_FUNCTIONS.md#filefile_read) | [V(e)](images/vfs-paths.png) |
| `file_seek` | [L138](../../kernel/fs/file.cpp#L138) | [讲解](FILESYSTEM_FUNCTIONS.md#filefile_seek) | [V(c)、V(e)](images/vfs-paths.png) |
| `file_tell` | [L152](../../kernel/fs/file.cpp#L152) | [讲解](FILESYSTEM_FUNCTIONS.md#filefile_tell) | [V(c)](images/vfs-paths.png) |

### kernel/fs/os64fs.cpp

87 个定义；已讲解 87 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `signature_matches` | [L14](../../kernel/fs/os64fs.cpp#L14) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fssignature_matches) | [L(a)](images/os64fs-layout.png) |
| `is_path_separator` | [L28](../../kernel/fs/os64fs.cpp#L28) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsis_path_separator) | [V(b)](images/vfs-paths.png) |
| `skip_path_separators` | [L34](../../kernel/fs/os64fs.cpp#L34) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsskip_path_separators) | [V(b)](images/vfs-paths.png) |
| `component_length` | [L46](../../kernel/fs/os64fs.cpp#L46) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fscomponent_length) | [V(b)](images/vfs-paths.png) |
| `name_matches` | [L55](../../kernel/fs/os64fs.cpp#L55) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsname_matches) | [V(b)](images/vfs-paths.png) |
| `component_is_dot` | [L73](../../kernel/fs/os64fs.cpp#L73) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fscomponent_is_dot) | [V(b)](images/vfs-paths.png) |
| `component_is_dot_dot` | [L79](../../kernel/fs/os64fs.cpp#L79) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fscomponent_is_dot_dot) | [V(b)](images/vfs-paths.png) |
| `filesystem_data_block_count` | [L87](../../kernel/fs/os64fs.cpp#L87) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsfilesystem_data_block_count) | [L(a)](images/os64fs-layout.png) |
| `filesystem_indirect_entry_capacity` | [L103](../../kernel/fs/os64fs.cpp#L103) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsfilesystem_indirect_entry_capacity) | [L(d)](images/os64fs-layout.png) |
| `bitmap_required_bytes` | [L111](../../kernel/fs/os64fs.cpp#L111) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsbitmap_required_bytes) | [L(b)](images/os64fs-layout.png) |
| `bitmap_bit_is_set` | [L115](../../kernel/fs/os64fs.cpp#L115) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsbitmap_bit_is_set) | [L(b)](images/os64fs-layout.png) |
| `bitmap_set_bit` | [L130](../../kernel/fs/os64fs.cpp#L130) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsbitmap_set_bit) | [L(b)](images/os64fs-layout.png) |
| `bitmap_clear_bit` | [L145](../../kernel/fs/os64fs.cpp#L145) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsbitmap_clear_bit) | [L(b)](images/os64fs-layout.png) |
| `data_block_index_is_valid` | [L160](../../kernel/fs/os64fs.cpp#L160) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsdata_block_index_is_valid) | [L(d)](images/os64fs-layout.png) |
| `read_superblock` | [L166](../../kernel/fs/os64fs.cpp#L166) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsread_superblock) | [L(a)](images/os64fs-layout.png) |
| `write_superblock` | [L177](../../kernel/fs/os64fs.cpp#L177) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fswrite_superblock) | [T(c)](images/filesystem-transactions.png) |
| `read_inode_table` | [L189](../../kernel/fs/os64fs.cpp#L189) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsread_inode_table) | [L(c)](images/os64fs-layout.png) |
| `read_bitmap` | [L227](../../kernel/fs/os64fs.cpp#L227) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsread_bitmap) | [L(b)](images/os64fs-layout.png) |
| `write_cached_sectors` | [L265](../../kernel/fs/os64fs.cpp#L265) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fswrite_cached_sectors) | [T(c)](images/filesystem-transactions.png) |
| `write_inode_table` | [L288](../../kernel/fs/os64fs.cpp#L288) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fswrite_inode_table) | [T(c)](images/filesystem-transactions.png) |
| `write_inode_bitmap` | [L296](../../kernel/fs/os64fs.cpp#L296) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fswrite_inode_bitmap) | [T(c)](images/filesystem-transactions.png) |
| `write_data_bitmap` | [L304](../../kernel/fs/os64fs.cpp#L304) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fswrite_data_bitmap) | [T(c)](images/filesystem-transactions.png) |
| `filesystem_layout_is_valid` | [L312](../../kernel/fs/os64fs.cpp#L312) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsfilesystem_layout_is_valid) | [L(f)](images/os64fs-layout.png) |
| `inode_is_valid` | [L399](../../kernel/fs/os64fs.cpp#L399) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsinode_is_valid) | [L(c)、L(f)](images/os64fs-layout.png) |
| `read_u32_from_data_block` | [L443](../../kernel/fs/os64fs.cpp#L443) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsread_u32_from_data_block) | [L(d)](images/os64fs-layout.png) |
| `inode_block_index_for_file_block` | [L480](../../kernel/fs/os64fs.cpp#L480) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsinode_block_index_for_file_block) | [L(d)](images/os64fs-layout.png) |
| `read_inode_block_bytes` | [L511](../../kernel/fs/os64fs.cpp#L511) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsread_inode_block_bytes) | [L(e)](images/os64fs-layout.png) |
| `cached_inode_pointer` | [L585](../../kernel/fs/os64fs.cpp#L585) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fscached_inode_pointer) | [L(c)](images/os64fs-layout.png) |
| `inode_bytes_are_zero` | [L602](../../kernel/fs/os64fs.cpp#L602) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsinode_bytes_are_zero) | [L(f)](images/os64fs-layout.png) |
| `set_validation_debug` | [L617](../../kernel/fs/os64fs.cpp#L617) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsset_validation_debug) | [L(f)](images/os64fs-layout.png) |
| `mark_data_block_allocated` | [L632](../../kernel/fs/os64fs.cpp#L632) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsmark_data_block_allocated) | [L(f)](images/os64fs-layout.png) |
| `validate_directory_references` | [L655](../../kernel/fs/os64fs.cpp#L655) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsvalidate_directory_references) | [L(f)](images/os64fs-layout.png) |
| `validate_allocation_maps_and_collect_stats` | [L716](../../kernel/fs/os64fs.cpp#L716) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsvalidate_allocation_maps_and_collect_stats) | [L(f)](images/os64fs-layout.png)；[T(d)](images/filesystem-transactions.png) |
| `find_child_inode` | [L857](../../kernel/fs/os64fs.cpp#L857) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsfind_child_inode) | [V(b)](images/vfs-paths.png) |
| `refresh_runtime_stats_from_superblock` | [L901](../../kernel/fs/os64fs.cpp#L901) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsrefresh_runtime_stats_from_superblock) | [L(b)](images/os64fs-layout.png) |
| `block_count_for_size` | [L915](../../kernel/fs/os64fs.cpp#L915) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsblock_count_for_size) | [L(d)](images/os64fs-layout.png) |
| `mutable_cached_inode_pointer` | [L924](../../kernel/fs/os64fs.cpp#L924) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsmutable_cached_inode_pointer) | [T(b)](images/filesystem-transactions.png) |
| `zero_inode_bytes` | [L942](../../kernel/fs/os64fs.cpp#L942) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fszero_inode_bytes) | [T(b)](images/filesystem-transactions.png) |
| `initialize_empty_inode_layout` | [L950](../../kernel/fs/os64fs.cpp#L950) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsinitialize_empty_inode_layout) | [L(c)](images/os64fs-layout.png)；[T(b)](images/filesystem-transactions.png) |
| `write_inode_to_cache` | [L962](../../kernel/fs/os64fs.cpp#L962) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fswrite_inode_to_cache) | [T(b)](images/filesystem-transactions.png) |
| `split_parent_path_and_name` | [L978](../../kernel/fs/os64fs.cpp#L978) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fssplit_parent_path_and_name) | [T(a)](images/filesystem-transactions.png)；[V(b)](images/vfs-paths.png) |
| `lookup_mutation_target` | [L1051](../../kernel/fs/os64fs.cpp#L1051) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fslookup_mutation_target) | [T(a)](images/filesystem-transactions.png) |
| `write_data_block_bytes` | [L1100](../../kernel/fs/os64fs.cpp#L1100) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fswrite_data_block_bytes) | [T(c)](images/filesystem-transactions.png) |
| `zero_data_block` | [L1163](../../kernel/fs/os64fs.cpp#L1163) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fszero_data_block) | [T(c)](images/filesystem-transactions.png) |
| `write_u32_to_data_block` | [L1188](../../kernel/fs/os64fs.cpp#L1188) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fswrite_u32_to_data_block) | [L(d)](images/os64fs-layout.png)；[T(c)](images/filesystem-transactions.png) |
| `sync_metadata` | [L1197](../../kernel/fs/os64fs.cpp#L1197) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fssync_metadata) | [T(c)](images/filesystem-transactions.png) |
| `allocate_inode_number` | [L1207](../../kernel/fs/os64fs.cpp#L1207) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsallocate_inode_number) | [L(b)](images/os64fs-layout.png)；[T(b)](images/filesystem-transactions.png) |
| `free_inode_number` | [L1235](../../kernel/fs/os64fs.cpp#L1235) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsfree_inode_number) | [L(b)](images/os64fs-layout.png)；[T(b)](images/filesystem-transactions.png) |
| `allocate_data_block_index` | [L1251](../../kernel/fs/os64fs.cpp#L1251) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsallocate_data_block_index) | [L(b)](images/os64fs-layout.png)；[T(c)](images/filesystem-transactions.png) |
| `free_data_block_index` | [L1283](../../kernel/fs/os64fs.cpp#L1283) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsfree_data_block_index) | [L(b)](images/os64fs-layout.png)；[T(b)](images/filesystem-transactions.png) |
| `ensure_indirect_block_initialized` | [L1298](../../kernel/fs/os64fs.cpp#L1298) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsensure_indirect_block_initialized) | [L(d)](images/os64fs-layout.png)；[T(c)](images/filesystem-transactions.png) |
| `ensure_inode_file_block` | [L1329](../../kernel/fs/os64fs.cpp#L1329) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsensure_inode_file_block) | [L(d)](images/os64fs-layout.png)；[T(c)](images/filesystem-transactions.png) |
| `free_inode_file_block` | [L1380](../../kernel/fs/os64fs.cpp#L1380) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsfree_inode_file_block) | [T(b)](images/filesystem-transactions.png)；[L(d)](images/os64fs-layout.png) |
| `truncate_inode_to_size` | [L1414](../../kernel/fs/os64fs.cpp#L1414) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fstruncate_inode_to_size) | [T(b)](images/filesystem-transactions.png) |
| `write_inode_bytes` | [L1446](../../kernel/fs/os64fs.cpp#L1446) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fswrite_inode_bytes) | [T(c)](images/filesystem-transactions.png) |
| `append_directory_entry` | [L1504](../../kernel/fs/os64fs.cpp#L1504) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsappend_directory_entry) | [T(a)、T(c)](images/filesystem-transactions.png) |
| `remove_directory_entry` | [L1516](../../kernel/fs/os64fs.cpp#L1516) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsremove_directory_entry) | [T(a)](images/filesystem-transactions.png) |
| `initialize_new_inode` | [L1547](../../kernel/fs/os64fs.cpp#L1547) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsinitialize_new_inode) | [T(b)](images/filesystem-transactions.png) |
| `initialize_os64fs` | [L1564](../../kernel/fs/os64fs.cpp#L1564) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsinitialize_os64fs) | [L(f)](images/os64fs-layout.png) |
| `os64fs_is_mounted` | [L1648](../../kernel/fs/os64fs.cpp#L1648) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_is_mounted) | [L(f)](images/os64fs-layout.png) |
| `os64fs_superblock` | [L1654](../../kernel/fs/os64fs.cpp#L1654) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_superblock) | [L(a)](images/os64fs-layout.png) |
| `os64fs_inode_type_name` | [L1662](../../kernel/fs/os64fs.cpp#L1662) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_inode_type_name) | [L(c)](images/os64fs-layout.png) |
| `os64fs_query_stats` | [L1673](../../kernel/fs/os64fs.cpp#L1673) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_query_stats) | [L(b)](images/os64fs-layout.png) |
| `os64fs_query_validation_debug` | [L1682](../../kernel/fs/os64fs.cpp#L1682) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_query_validation_debug) | [L(f)](images/os64fs-layout.png) |
| `os64fs_mount_error` | [L1692](../../kernel/fs/os64fs.cpp#L1692) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_mount_error) | [L(f)](images/os64fs-layout.png) |
| `os64fs_read_inode` | [L1696](../../kernel/fs/os64fs.cpp#L1696) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_read_inode) | [L(c)](images/os64fs-layout.png) |
| `os64fs_lookup_path` | [L1717](../../kernel/fs/os64fs.cpp#L1717) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_lookup_path) | [V(b)](images/vfs-paths.png) |
| `os64fs_directory_entry_count` | [L1794](../../kernel/fs/os64fs.cpp#L1794) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_directory_entry_count) | [V(d)](images/vfs-paths.png) |
| `os64fs_read_directory_entry` | [L1805](../../kernel/fs/os64fs.cpp#L1805) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_read_directory_entry) | [V(d)](images/vfs-paths.png) |
| `os64fs_read_inode_data` | [L1829](../../kernel/fs/os64fs.cpp#L1829) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_read_inode_data) | [L(e)](images/os64fs-layout.png)；[V(e)](images/vfs-paths.png) |
| `os64fs_create_file_impl` | [L1838](../../kernel/fs/os64fs.cpp#L1838) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_create_file_impl) | [T(a)、T(c)](images/filesystem-transactions.png) |
| `os64fs_create_directory_impl` | [L1889](../../kernel/fs/os64fs.cpp#L1889) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_create_directory_impl) | [T(a)、T(c)](images/filesystem-transactions.png) |
| `write_file_common` | [L1934](../../kernel/fs/os64fs.cpp#L1934) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fswrite_file_common) | [T(a)、T(c)](images/filesystem-transactions.png) |
| `os64fs_write_file_impl` | [L1990](../../kernel/fs/os64fs.cpp#L1990) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_write_file_impl) | [T(c)](images/filesystem-transactions.png) |
| `os64fs_append_file_impl` | [L1995](../../kernel/fs/os64fs.cpp#L1995) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_append_file_impl) | [T(c)](images/filesystem-transactions.png) |
| `os64fs_unlink_impl` | [L2000](../../kernel/fs/os64fs.cpp#L2000) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_unlink_impl) | [T(a)、T(c)](images/filesystem-transactions.png) |
| `os64fs_sync` | [L2043](../../kernel/fs/os64fs.cpp#L2043) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_sync) | [T(f)](images/filesystem-transactions.png) |
| `find_mutation_sector` | [L2067](../../kernel/fs/os64fs.cpp#L2067) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsfind_mutation_sector) | [T(b)](images/filesystem-transactions.png) |
| `staged_read` | [L2073](../../kernel/fs/os64fs.cpp#L2073) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsstaged_read) | [T(b)](images/filesystem-transactions.png) |
| `staged_write` | [L2081](../../kernel/fs/os64fs.cpp#L2081) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsstaged_write) | [T(c)](images/filesystem-transactions.png) |
| `staged_flush` | [L2095](../../kernel/fs/os64fs.cpp#L2095) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsstaged_flush) | [T(c)](images/filesystem-transactions.png) |
| `mutate` | [L2099](../../kernel/fs/os64fs.cpp#L2099) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsmutate) | [T(a)、T(f)](images/filesystem-transactions.png) |
| `os64fs_create_file` | [L2165](../../kernel/fs/os64fs.cpp#L2165) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_create_file) | [T(a)](images/filesystem-transactions.png) |
| `os64fs_create_directory` | [L2168](../../kernel/fs/os64fs.cpp#L2168) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_create_directory) | [T(a)](images/filesystem-transactions.png) |
| `os64fs_write_file` | [L2171](../../kernel/fs/os64fs.cpp#L2171) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_write_file) | [T(c)](images/filesystem-transactions.png) |
| `os64fs_append_file` | [L2174](../../kernel/fs/os64fs.cpp#L2174) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_append_file) | [T(c)](images/filesystem-transactions.png) |
| `os64fs_unlink` | [L2177](../../kernel/fs/os64fs.cpp#L2177) | [讲解](FILESYSTEM_FUNCTIONS.md#os64fsos64fs_unlink) | [T(a)](images/filesystem-transactions.png) |

### kernel/fs/vfs.cpp

27 个定义；已讲解 27 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `to_vfs_node_type` | [L9](../../kernel/fs/vfs.cpp#L9) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsto_vfs_node_type) | [V(a)](images/vfs-paths.png) |
| `copy_file_stat_to_vfs_stat` | [L21](../../kernel/fs/vfs.cpp#L21) | [讲解](FILESYSTEM_FUNCTIONS.md#vfscopy_file_stat_to_vfs_stat) | [V(a)](images/vfs-paths.png) |
| `copy_directory_entry_to_vfs_entry` | [L44](../../kernel/fs/vfs.cpp#L44) | [讲解](FILESYSTEM_FUNCTIONS.md#vfscopy_directory_entry_to_vfs_entry) | [V(d)](images/vfs-paths.png) |
| `initialize_vfs` | [L68](../../kernel/fs/vfs.cpp#L68) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsinitialize_vfs) | [V(a)](images/vfs-paths.png) |
| `vfs_is_mounted` | [L86](../../kernel/fs/vfs.cpp#L86) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_is_mounted) | [V(a)](images/vfs-paths.png) |
| `vfs_node_type_name` | [L92](../../kernel/fs/vfs.cpp#L92) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_node_type_name) | [V(a)](images/vfs-paths.png) |
| `vfs_stat` | [L103](../../kernel/fs/vfs.cpp#L103) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_stat) | [V(a)、V(b)](images/vfs-paths.png) |
| `vfs_open_file` | [L118](../../kernel/fs/vfs.cpp#L118) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_open_file) | [V(c)](images/vfs-paths.png) |
| `vfs_file_is_open` | [L131](../../kernel/fs/vfs.cpp#L131) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_file_is_open) | [V(c)](images/vfs-paths.png) |
| `vfs_close_file` | [L135](../../kernel/fs/vfs.cpp#L135) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_close_file) | [V(c)](images/vfs-paths.png) |
| `vfs_file_stat` | [L143](../../kernel/fs/vfs.cpp#L143) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_file_stat) | [V(e)](images/vfs-paths.png) |
| `vfs_read_file` | [L156](../../kernel/fs/vfs.cpp#L156) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_read_file) | [V(e)](images/vfs-paths.png) |
| `vfs_seek_file` | [L165](../../kernel/fs/vfs.cpp#L165) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_seek_file) | [V(c)](images/vfs-paths.png) |
| `vfs_tell_file` | [L173](../../kernel/fs/vfs.cpp#L173) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_tell_file) | [V(c)](images/vfs-paths.png) |
| `vfs_open_directory` | [L181](../../kernel/fs/vfs.cpp#L181) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_open_directory) | [V(d)](images/vfs-paths.png) |
| `vfs_directory_is_open` | [L194](../../kernel/fs/vfs.cpp#L194) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_directory_is_open) | [V(d)](images/vfs-paths.png) |
| `vfs_close_directory` | [L199](../../kernel/fs/vfs.cpp#L199) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_close_directory) | [V(d)](images/vfs-paths.png) |
| `vfs_directory_entry_count` | [L207](../../kernel/fs/vfs.cpp#L207) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_directory_entry_count) | [V(d)](images/vfs-paths.png) |
| `vfs_read_directory` | [L215](../../kernel/fs/vfs.cpp#L215) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_read_directory) | [V(d)](images/vfs-paths.png) |
| `vfs_rewind_directory` | [L231](../../kernel/fs/vfs.cpp#L231) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_rewind_directory) | [V(d)](images/vfs-paths.png) |
| `vfs_tell_directory` | [L239](../../kernel/fs/vfs.cpp#L239) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_tell_directory) | [V(d)](images/vfs-paths.png) |
| `vfs_create_file` | [L247](../../kernel/fs/vfs.cpp#L247) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_create_file) | [V(f)](images/vfs-paths.png) |
| `vfs_create_directory` | [L253](../../kernel/fs/vfs.cpp#L253) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_create_directory) | [V(f)](images/vfs-paths.png) |
| `vfs_write_file` | [L259](../../kernel/fs/vfs.cpp#L259) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_write_file) | [V(f)](images/vfs-paths.png) |
| `vfs_append_file` | [L266](../../kernel/fs/vfs.cpp#L266) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_append_file) | [V(f)](images/vfs-paths.png) |
| `vfs_unlink` | [L273](../../kernel/fs/vfs.cpp#L273) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_unlink) | [V(f)](images/vfs-paths.png) |
| `vfs_sync` | [L279](../../kernel/fs/vfs.cpp#L279) | [讲解](FILESYSTEM_FUNCTIONS.md#vfsvfs_sync) | [V(f)](images/vfs-paths.png) |

### kernel/interrupts/interrupts.cpp

25 个定义；已讲解 25 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `cpu_interrupt_state` | [L116](../../kernel/interrupts/interrupts.cpp#L116) | [讲解](SMP_BOOT_FUNCTIONS.md#中断结构辅助函数--bdgc)（讲义第271行） | [B(d)](images/smp-boot.png) |
| `set_idt_gate` | [L123](../../kernel/interrupts/interrupts.cpp#L123) | [讲解](SMP_BOOT_FUNCTIONS.md#中断结构辅助函数--bdgc)（讲义第272行） | [B(d)](images/smp-boot.png) |
| `load_idt` | [L141](../../kernel/interrupts/interrupts.cpp#L141) | [讲解](SMP_BOOT_FUNCTIONS.md#中断结构辅助函数--bdgc)（讲义第273行） | [B(d)](images/smp-boot.png) |
| `stack_top_address` | [L145](../../kernel/interrupts/interrupts.cpp#L145) | [讲解](SMP_BOOT_FUNCTIONS.md#中断结构辅助函数--bdgc)（讲义第274行） | [B(d)](images/smp-boot.png) |
| `build_kernel_gdt` | [L149](../../kernel/interrupts/interrupts.cpp#L149) | [讲解](SMP_BOOT_FUNCTIONS.md#build_kernel_gdt-与-load_kernel_gdt_and_tsspointer--bd)（讲义第255行） | [B(d)](images/smp-boot.png) |
| `load_kernel_gdt_and_tss` | [L186](../../kernel/interrupts/interrupts.cpp#L186) | [讲解](SMP_BOOT_FUNCTIONS.md#build_kernel_gdt-与-load_kernel_gdt_and_tsspointer--bd)（讲义第256行） | [B(d)](images/smp-boot.png) |
| `read_task_register_selector` | [L217](../../kernel/interrupts/interrupts.cpp#L217) | [讲解](SMP_BOOT_FUNCTIONS.md#中断结构辅助函数--bdgc)（讲义第275行） | [B(d)](images/smp-boot.png) |
| `register_frame_came_from_user_mode` | [L223](../../kernel/interrupts/interrupts.cpp#L223) | [讲解](SMP_BOOT_FUNCTIONS.md#中断结构辅助函数--bdgc)（讲义第276行） | [G(c)](images/kernel-gate.png) |
| `capture_current_user_preempt_trap_frame` | [L227](../../kernel/interrupts/interrupts.cpp#L227) | [讲解](SMP_BOOT_FUNCTIONS.md#capture_current_user_preempt_trap_frameframe--gcgf) | [G(c)、G(f)](images/kernel-gate.png) |
| `initialize_tss` | [L279](../../kernel/interrupts/interrupts.cpp#L279) | [讲解](SMP_BOOT_FUNCTIONS.md#initialize_tss--bd) | [B(d)](images/smp-boot.png) |
| `tss_is_ready` | [L313](../../kernel/interrupts/interrupts.cpp#L313) | [讲解](SMP_BOOT_FUNCTIONS.md#tss-查询和更新函数--bd-的-rsp0ist1-小盒)（讲义第282行） | [B(d)](images/smp-boot.png) |
| `tss_task_register_selector` | [L317](../../kernel/interrupts/interrupts.cpp#L317) | [讲解](SMP_BOOT_FUNCTIONS.md#tss-查询和更新函数--bd-的-rsp0ist1-小盒)（讲义第283行） | [B(d)](images/smp-boot.png) |
| `tss_kernel_rsp0` | [L325](../../kernel/interrupts/interrupts.cpp#L325) | [讲解](SMP_BOOT_FUNCTIONS.md#tss-查询和更新函数--bd-的-rsp0ist1-小盒)（讲义第284行） | [B(d)](images/smp-boot.png) |
| `tss_default_kernel_rsp0` | [L329](../../kernel/interrupts/interrupts.cpp#L329) | [讲解](SMP_BOOT_FUNCTIONS.md#tss-查询和更新函数--bd-的-rsp0ist1-小盒)（讲义第285行） | [B(d)](images/smp-boot.png) |
| `tss_set_kernel_rsp0` | [L333](../../kernel/interrupts/interrupts.cpp#L333) | [讲解](SMP_BOOT_FUNCTIONS.md#tss-查询和更新函数--bd-的-rsp0ist1-小盒)（讲义第286行） | [B(d)](images/smp-boot.png) |
| `tss_double_fault_ist1` | [L342](../../kernel/interrupts/interrupts.cpp#L342) | [讲解](SMP_BOOT_FUNCTIONS.md#tss-查询和更新函数--bd-的-rsp0ist1-小盒)（讲义第287行） | [B(d)](images/smp-boot.png) |
| `tss_io_map_base` | [L346](../../kernel/interrupts/interrupts.cpp#L346) | [讲解](SMP_BOOT_FUNCTIONS.md#tss-查询和更新函数--bd-的-rsp0ist1-小盒)（讲义第288行） | [B(d)](images/smp-boot.png) |
| `initialize_idt` | [L350](../../kernel/interrupts/interrupts.cpp#L350) | [讲解](SMP_BOOT_FUNCTIONS.md#initialize_idt-与-initialize_secondary_interrupts--bdgf)（讲义第264行） | [B(d)](images/smp-boot.png)；[G(f)](images/kernel-gate.png) |
| `initialize_secondary_interrupts` | [L391](../../kernel/interrupts/interrupts.cpp#L391) | [讲解](SMP_BOOT_FUNCTIONS.md#initialize_idt-与-initialize_secondary_interrupts--bdgf)（讲义第265行） | [B(d)](images/smp-boot.png)；[G(f)](images/kernel-gate.png) |
| `exception_name` | [L400](../../kernel/interrupts/interrupts.cpp#L400) | [讲解](SMP_BOOT_FUNCTIONS.md#中断开关与名称工具--gcgd)（讲义第318行） | [G(c)](images/kernel-gate.png) |
| `enable_interrupts` | [L408](../../kernel/interrupts/interrupts.cpp#L408) | [讲解](SMP_BOOT_FUNCTIONS.md#中断开关与名称工具--gcgd)（讲义第319行） | [G(c)、G(d)](images/kernel-gate.png) |
| `disable_interrupts` | [L412](../../kernel/interrupts/interrupts.cpp#L412) | [讲解](SMP_BOOT_FUNCTIONS.md#中断开关与名称工具--gcgd)（讲义第320行） | [G(c)、G(d)](images/kernel-gate.png) |
| `interrupts_are_enabled` | [L416](../../kernel/interrupts/interrupts.cpp#L416) | [讲解](SMP_BOOT_FUNCTIONS.md#中断开关与名称工具--gcgd)（讲义第321行） | [B(a)](images/smp-boot.png)；[G(c)](images/kernel-gate.png) |
| `wait_for_interrupt` | [L422](../../kernel/interrupts/interrupts.cpp#L422) | [讲解](SMP_BOOT_FUNCTIONS.md#中断开关与名称工具--gcgd)（讲义第322行） | [G(d)](images/kernel-gate.png) |
| `kernel_handle_irq` | [L426](../../kernel/interrupts/interrupts.cpp#L426) | [讲解](SMP_BOOT_FUNCTIONS.md#kernel_handle_irqframe门铃到了以后怎么处理--gegf) | [G(e)、G(f)](images/kernel-gate.png) |

### kernel/interrupts/keyboard.cpp

34 个定义；已讲解 34 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `out8` | [L32](../../kernel/interrupts/keyboard.cpp#L32) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第82行） | [I(a)](images/console-input.png) |
| `in8` | [L36](../../kernel/interrupts/keyboard.cpp#L36) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第83行） | [I(a)](images/console-input.png) |
| `save_interrupt_flags_and_disable` | [L42](../../kernel/interrupts/keyboard.cpp#L42) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第84行） | [I(c)](images/console-input.png) |
| `restore_interrupt_flags` | [L48](../../kernel/interrupts/keyboard.cpp#L48) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第85行） | [I(c)](images/console-input.png) |
| `advance_buffer_index` | [L55](../../kernel/interrupts/keyboard.cpp#L55) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第86行） | [I(b)](images/console-input.png) |
| `wait_for_input_buffer_empty` | [L64](../../kernel/interrupts/keyboard.cpp#L64) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第87行） | [I(a)](images/console-input.png) |
| `drain_output_buffer` | [L74](../../kernel/interrupts/keyboard.cpp#L74) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第88行） | [I(a)](images/console-input.png) |
| `translate_regular_scancode` | [L86](../../kernel/interrupts/keyboard.cpp#L86) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第89行） | [I(a)](images/console-input.png) |
| `translate_extended_scancode` | [L159](../../kernel/interrupts/keyboard.cpp#L159) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第90行） | [I(a)](images/console-input.png) |
| `translate_scancode_to_input_event` | [L184](../../kernel/interrupts/keyboard.cpp#L184) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第91行） | [I(a)](images/console-input.png) |
| `enqueue_input_event` | [L201](../../kernel/interrupts/keyboard.cpp#L201) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第92行） | [I(b)](images/console-input.png) |
| `try_dequeue_input_event` | [L219](../../kernel/interrupts/keyboard.cpp#L219) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第93行） | [I(b)、I(c)](images/console-input.png) |
| `stream_waiter_is_registered` | [L253](../../kernel/interrupts/keyboard.cpp#L253) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第94行） | [I(c)](images/console-input.png) |
| `register_stream_waiter` | [L267](../../kernel/interrupts/keyboard.cpp#L267) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第95行） | [I(c)](images/console-input.png) |
| `unregister_stream_waiter` | [L288](../../kernel/interrupts/keyboard.cpp#L288) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第96行） | [I(c)](images/console-input.png) |
| `input_waiter_is_registered` | [L300](../../kernel/interrupts/keyboard.cpp#L300) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第97行） | [I(c)](images/console-input.png) |
| `register_input_waiter` | [L314](../../kernel/interrupts/keyboard.cpp#L314) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第98行） | [I(c)](images/console-input.png) |
| `unregister_input_waiter` | [L336](../../kernel/interrupts/keyboard.cpp#L336) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第99行） | [I(c)](images/console-input.png) |
| `wake_input_waiters` | [L348](../../kernel/interrupts/keyboard.cpp#L348) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第100行） | [I(c)](images/console-input.png) |
| `wake_stream_waiters` | [L361](../../kernel/interrupts/keyboard.cpp#L361) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第101行） | [I(c)](images/console-input.png) |
| `initialize_keyboard` | [L377](../../kernel/interrupts/keyboard.cpp#L377) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第102行） | [I(a)、I(b)](images/console-input.png) |
| `keyboard_is_ready` | [L398](../../kernel/interrupts/keyboard.cpp#L398) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第103行） | [I(a)](images/console-input.png) |
| `keyboard_submit_input_event` | [L402](../../kernel/interrupts/keyboard.cpp#L402) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第104行） | [I(b)、I(c)](images/console-input.png) |
| `handle_keyboard_irq` | [L415](../../kernel/interrupts/keyboard.cpp#L415) | [讲解](DEVICES_IO_FUNCTIONS.md#keyboardhandle_keyboard_irq--iaib) | [I(a)、I(b)](images/console-input.png) |
| `keyboard_irq_count` | [L465](../../kernel/interrupts/keyboard.cpp#L465) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第105行） | [I(a)](images/console-input.png)；[O(e)](images/logging-observability.png) |
| `keyboard_last_scancode` | [L469](../../kernel/interrupts/keyboard.cpp#L469) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第106行） | [I(a)](images/console-input.png)；[O(e)](images/logging-observability.png) |
| `keyboard_buffered_char_count` | [L473](../../kernel/interrupts/keyboard.cpp#L473) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第107行） | [I(b)](images/console-input.png)；[O(e)](images/logging-observability.png) |
| `keyboard_dropped_char_count` | [L477](../../kernel/interrupts/keyboard.cpp#L477) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第108行） | [I(b)](images/console-input.png)；[O(e)](images/logging-observability.png) |
| `keyboard_try_read_input_event` | [L481](../../kernel/interrupts/keyboard.cpp#L481) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第109行） | [I(b)、I(d)](images/console-input.png) |
| `keyboard_wait_for_input_event` | [L494](../../kernel/interrupts/keyboard.cpp#L494) | [讲解](DEVICES_IO_FUNCTIONS.md#keyboardkeyboard_wait_for_input_event--ic) | [I(c)](images/console-input.png) |
| `keyboard_try_read_char` | [L537](../../kernel/interrupts/keyboard.cpp#L537) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第110行） | [I(b)](images/console-input.png) |
| `keyboard_try_read_stream_char` | [L556](../../kernel/interrupts/keyboard.cpp#L556) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第111行） | [I(b)、I(c)](images/console-input.png) |
| `keyboard_wait_for_stream_char` | [L582](../../kernel/interrupts/keyboard.cpp#L582) | [讲解](DEVICES_IO_FUNCTIONS.md#keyboardkeyboard_wait_for_stream_char--ic) | [I(c)](images/console-input.png) |
| `keyboard_inject_test_scancode` | [L622](../../kernel/interrupts/keyboard.cpp#L622) | [讲解](DEVICES_IO_FUNCTIONS.md#键盘辅助与公开查询逐函数表)（讲义第112行） | [I(a)](images/console-input.png) |

### kernel/interrupts/pic.cpp

7 个定义；已讲解 7 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `out8` | [L19](../../kernel/interrupts/pic.cpp#L19) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第147行） | [H(b)](images/interrupt-devices.png) |
| `in8` | [L23](../../kernel/interrupts/pic.cpp#L23) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第148行） | [H(b)](images/interrupt-devices.png) |
| `io_wait` | [L29](../../kernel/interrupts/pic.cpp#L29) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第149行） | [H(b)](images/interrupt-devices.png) |
| `initialize_pic` | [L37](../../kernel/interrupts/pic.cpp#L37) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第150行） | [H(b)](images/interrupt-devices.png) |
| `enable_pic_irq` | [L70](../../kernel/interrupts/pic.cpp#L70) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第151行） | [H(b)](images/interrupt-devices.png) |
| `disable_pic_irq` | [L95](../../kernel/interrupts/pic.cpp#L95) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第152行） | [H(b)](images/interrupt-devices.png) |
| `send_pic_eoi` | [L116](../../kernel/interrupts/pic.cpp#L116) | [讲解](DEVICES_IO_FUNCTIONS.md#pci-与-pic找到设备再把门铃接对)（讲义第153行） | [H(d)](images/interrupt-devices.png) |

### kernel/interrupts/pit.cpp

8 个定义；已讲解 8 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `out8` | [L18](../../kernel/interrupts/pit.cpp#L18) | [讲解](DEVICES_IO_FUNCTIONS.md#pit一份系统时间不是四核相加)（讲义第161行） | [H(c)](images/interrupt-devices.png) |
| `initialize_pit` | [L24](../../kernel/interrupts/pit.cpp#L24) | [讲解](DEVICES_IO_FUNCTIONS.md#pit一份系统时间不是四核相加)（讲义第162行） | [H(c)](images/interrupt-devices.png) |
| `handle_timer_irq` | [L55](../../kernel/interrupts/pit.cpp#L55) | [讲解](DEVICES_IO_FUNCTIONS.md#pit一份系统时间不是四核相加)（讲义第163行） | [H(c)、H(d)](images/interrupt-devices.png) |
| `timer_tick_count` | [L65](../../kernel/interrupts/pit.cpp#L65) | [讲解](DEVICES_IO_FUNCTIONS.md#pit一份系统时间不是四核相加)（讲义第164行） | [H(c)](images/interrupt-devices.png)；[O(e)](images/logging-observability.png) |
| `timer_frequency_hz` | [L69](../../kernel/interrupts/pit.cpp#L69) | [讲解](DEVICES_IO_FUNCTIONS.md#pit一份系统时间不是四核相加)（讲义第165行） | [H(c)](images/interrupt-devices.png)；[O(e)](images/logging-observability.png) |
| `timer_is_ready` | [L73](../../kernel/interrupts/pit.cpp#L73) | [讲解](DEVICES_IO_FUNCTIONS.md#pit一份系统时间不是四核相加)（讲义第166行） | [H(c)](images/interrupt-devices.png) |
| `timer_wait_ticks` | [L77](../../kernel/interrupts/pit.cpp#L77) | [讲解](DEVICES_IO_FUNCTIONS.md#pit一份系统时间不是四核相加)（讲义第167行） | [H(e)](images/interrupt-devices.png) |
| `timer_sleep_ms` | [L104](../../kernel/interrupts/pit.cpp#L104) | [讲解](DEVICES_IO_FUNCTIONS.md#pit一份系统时间不是四核相加)（讲义第168行） | [H(e)](images/interrupt-devices.png) |

### kernel/interrupts/serial.cpp

11 个定义；已讲解 11 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `out8` | [L13](../../kernel/interrupts/serial.cpp#L13) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第120行） | [I(f)](images/console-input.png) |
| `in8` | [L16](../../kernel/interrupts/serial.cpp#L16) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第121行） | [I(f)](images/console-input.png) |
| `put` | [L21](../../kernel/interrupts/serial.cpp#L21) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第122行） | [I(f)](images/console-input.png) |
| `movement` | [L26](../../kernel/interrupts/serial.cpp#L26) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第123行） | [I(f)](images/console-input.png) |
| `submit` | [L34](../../kernel/interrupts/serial.cpp#L34) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第124行） | [I(a)、I(f)](images/console-input.png) |
| `initialize_serial_input` | [L71](../../kernel/interrupts/serial.cpp#L71) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第125行） | [I(a)](images/console-input.png)；[H(b)](images/interrupt-devices.png) |
| `serial_begin_input_line` | [L84](../../kernel/interrupts/serial.cpp#L84) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第126行） | [I(d)、I(f)](images/console-input.png) |
| `serial_move_input_cursor` | [L85](../../kernel/interrupts/serial.cpp#L85) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第127行） | [I(f)](images/console-input.png) |
| `serial_redraw_input_line` | [L91](../../kernel/interrupts/serial.cpp#L91) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第128行） | [I(d)、I(f)](images/console-input.png) |
| `serial_end_input_line` | [L99](../../kernel/interrupts/serial.cpp#L99) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第129行） | [I(f)](images/console-input.png) |
| `handle_serial_irq` | [L104](../../kernel/interrupts/serial.cpp#L104) | [讲解](DEVICES_IO_FUNCTIONS.md#串口字节也能变成同一组编辑事件)（讲义第130行） | [I(a)](images/console-input.png)；[H(d)](images/interrupt-devices.png) |

### kernel/log/log.cpp

9 个定义；已讲解 9 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `InterruptGuard::InterruptGuard` | [L16](../../kernel/log/log.cpp#L16) | [讲解](DEVICES_IO_FUNCTIONS.md#日志与性能先准确记录再解释数字)（讲义第295行） | [O(a)](images/logging-observability.png) |
| `InterruptGuard::~InterruptGuard` | [L17](../../kernel/log/log.cpp#L17) | [讲解](DEVICES_IO_FUNCTIONS.md#日志与性能先准确记录再解释数字)（讲义第296行） | [O(a)](images/logging-observability.png) |
| `copy_text` | [L20](../../kernel/log/log.cpp#L20) | [讲解](DEVICES_IO_FUNCTIONS.md#日志与性能先准确记录再解释数字)（讲义第297行） | [O(a)](images/logging-observability.png) |
| `kernel_log_initialize` | [L26](../../kernel/log/log.cpp#L26) | [讲解](DEVICES_IO_FUNCTIONS.md#日志与性能先准确记录再解释数字)（讲义第298行） | [O(b)](images/logging-observability.png) |
| `kernel_log_write` | [L31](../../kernel/log/log.cpp#L31) | [讲解](DEVICES_IO_FUNCTIONS.md#日志与性能先准确记录再解释数字)（讲义第299行） | [O(a)、O(b)、O(c)](images/logging-observability.png) |
| `kernel_log_process_event` | [L43](../../kernel/log/log.cpp#L43) | [讲解](DEVICES_IO_FUNCTIONS.md#日志与性能先准确记录再解释数字)（讲义第300行） | [O(a)](images/logging-observability.png) |
| `kernel_log_read` | [L53](../../kernel/log/log.cpp#L53) | [讲解](DEVICES_IO_FUNCTIONS.md#日志与性能先准确记录再解释数字)（讲义第301行） | [O(c)、O(d)](images/logging-observability.png) |
| `kernel_log_stats` | [L64](../../kernel/log/log.cpp#L64) | [讲解](DEVICES_IO_FUNCTIONS.md#日志与性能先准确记录再解释数字)（讲义第302行） | [O(c)、O(e)](images/logging-observability.png) |
| `kernel_log_level_name` | [L68](../../kernel/log/log.cpp#L68) | [讲解](DEVICES_IO_FUNCTIONS.md#日志与性能先准确记录再解释数字)（讲义第303行） | [O(a)](images/logging-observability.png) |

### kernel/memory/address_space.cpp

16 个定义；已讲解 16 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `table_from_physical_address` | [L12](../../kernel/memory/address_space.cpp#L12) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第102行） | [V(b)](images/paging-address-space.png) |
| `allocate_clone_table_page` | [L20](../../kernel/memory/address_space.cpp#L20) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第103行） | [V(d)](images/paging-address-space.png) |
| `destroy_table` | [L40](../../kernel/memory/address_space.cpp#L40) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第104行） | [V(e)](images/paging-address-space.png) |
| `clone_page_table_level` | [L67](../../kernel/memory/address_space.cpp#L67) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第105行） | [V(d)](images/paging-address-space.png) |
| `fill_common_layout` | [L121](../../kernel/memory/address_space.cpp#L121) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第106行） | [V(d)](images/paging-address-space.png) |
| `user_region_contains` | [L133](../../kernel/memory/address_space.cpp#L133) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第107行） | [V(d)](images/paging-address-space.png) |
| `table_is_empty` | [L139](../../kernel/memory/address_space.cpp#L139) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第108行） | [V(e)](images/paging-address-space.png) |
| `initialize_kernel_address_space_view` | [L150](../../kernel/memory/address_space.cpp#L150) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第109行） | [V(d)](images/paging-address-space.png) |
| `clone_current_address_space` | [L171](../../kernel/memory/address_space.cpp#L171) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第110行） | [V(d)](images/paging-address-space.png) |
| `clone_address_space_from_root` | [L176](../../kernel/memory/address_space.cpp#L176) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第111行） | [V(d)](images/paging-address-space.png) |
| `address_space_map_user_page` | [L205](../../kernel/memory/address_space.cpp#L205) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第112行） | [V(c)、V(d)](images/paging-address-space.png) |
| `address_space_unmap_user_page` | [L239](../../kernel/memory/address_space.cpp#L239) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第113行） | [V(e)](images/paging-address-space.png) |
| `address_space_resolve_mapping` | [L288](../../kernel/memory/address_space.cpp#L288) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第114行） | [V(a)](images/paging-address-space.png) |
| `address_space_destroy` | [L298](../../kernel/memory/address_space.cpp#L298) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第115行） | [V(e)](images/paging-address-space.png) |
| `address_space_user_range_valid` | [L309](../../kernel/memory/address_space.cpp#L309) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第116行） | [S(c)](images/syscall-boundary.png) |
| `address_space_copy_to_user` | [L345](../../kernel/memory/address_space.cpp#L345) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryaddress_spacecpp)（讲义第117行） | [S(c)](images/syscall-boundary.png)；[V(d)](images/paging-address-space.png) |

### kernel/memory/heap.cpp

21 个定义；已讲解 21 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `record_failed_allocation` | [L23](../../kernel/memory/heap.cpp#L23) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第123行） | [H(f)](images/heap-memory.png) |
| `is_power_of_two` | [L29](../../kernel/memory/heap.cpp#L29) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第124行） | [H(b)](images/heap-memory.png) |
| `align_up` | [L33](../../kernel/memory/heap.cpp#L33) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第125行） | [H(b)](images/heap-memory.png) |
| `address_of` | [L42](../../kernel/memory/heap.cpp#L42) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第126行） | [H(b)](images/heap-memory.png) |
| `free_region_start` | [L46](../../kernel/memory/heap.cpp#L46) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第127行） | [H(c)](images/heap-memory.png) |
| `free_region_end` | [L50](../../kernel/memory/heap.cpp#L50) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第128行） | [H(c)](images/heap-memory.png) |
| `minimum_growth_bytes` | [L54](../../kernel/memory/heap.cpp#L54) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第129行） | [H(a)、H(b)](images/heap-memory.png) |
| `merge_forward` | [L61](../../kernel/memory/heap.cpp#L61) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第130行） | [H(c)](images/heap-memory.png) |
| `insert_free_region` | [L69](../../kernel/memory/heap.cpp#L69) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第131行） | [H(c)](images/heap-memory.png) |
| `ensure_heap_mapping` | [L106](../../kernel/memory/heap.cpp#L106) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第132行） | [H(a)](images/heap-memory.png) |
| `try_allocate_from_region` | [L146](../../kernel/memory/heap.cpp#L146) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第133行） | [H(b)](images/heap-memory.png) |
| `initialize_kernel_heap` | [L212](../../kernel/memory/heap.cpp#L212) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第134行） | [H(a)](images/heap-memory.png) |
| `heap_reserve` | [L229](../../kernel/memory/heap.cpp#L229) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第135行） | [H(a)](images/heap-memory.png) |
| `heap_alloc` | [L241](../../kernel/memory/heap.cpp#L241) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第136行） | [H(a)、H(b)](images/heap-memory.png) |
| `heap_free` | [L298](../../kernel/memory/heap.cpp#L298) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第137行） | [H(c)](images/heap-memory.png) |
| `heap_used_bytes` | [L337](../../kernel/memory/heap.cpp#L337) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第138行） | [H(f)](images/heap-memory.png) |
| `heap_mapped_bytes` | [L345](../../kernel/memory/heap.cpp#L345) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第139行） | [H(f)](images/heap-memory.png) |
| `heap_free_bytes` | [L353](../../kernel/memory/heap.cpp#L353) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第140行） | [H(f)](images/heap-memory.png) |
| `heap_active_allocations` | [L368](../../kernel/memory/heap.cpp#L368) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第141行） | [H(f)](images/heap-memory.png) |
| `heap_total_allocations` | [L376](../../kernel/memory/heap.cpp#L376) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第142行） | [H(f)](images/heap-memory.png) |
| `heap_failed_allocations` | [L384](../../kernel/memory/heap.cpp#L384) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemoryheapcpp)（讲义第143行） | [H(f)](images/heap-memory.png) |

### kernel/memory/kmemory.cpp

9 个定义；已讲解 9 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `multiply_would_overflow` | [L19](../../kernel/memory/kmemory.cpp#L19) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemorycpp)（讲义第149行） | [H(d)](images/heap-memory.png) |
| `initialize_kernel_memory_system` | [L29](../../kernel/memory/kmemory.cpp#L29) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemorycpp)（讲义第150行） | [H(d)](images/heap-memory.png) |
| `kernel_memory_system_ready` | [L43](../../kernel/memory/kmemory.cpp#L43) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemorycpp)（讲义第151行） | [H(d)](images/heap-memory.png) |
| `kernel_memory_page_allocator` | [L48](../../kernel/memory/kmemory.cpp#L48) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemorycpp)（讲义第152行） | [A(f)](images/physical-pages.png) |
| `kernel_memory_heap` | [L56](../../kernel/memory/kmemory.cpp#L56) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemorycpp)（讲义第153行） | [H(d)](images/heap-memory.png) |
| `kmalloc` | [L64](../../kernel/memory/kmemory.cpp#L64) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemorycpp)（讲义第154行） | [H(d)](images/heap-memory.png) |
| `kmalloc_aligned` | [L68](../../kernel/memory/kmemory.cpp#L68) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemorycpp)（讲义第155行） | [H(d)](images/heap-memory.png) |
| `kcalloc` | [L79](../../kernel/memory/kmemory.cpp#L79) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemorycpp)（讲义第156行） | [H(d)](images/heap-memory.png) |
| `kfree` | [L87](../../kernel/memory/kmemory.cpp#L87) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemorycpp)（讲义第157行） | [H(c)、H(d)](images/heap-memory.png) |

### kernel/memory/kmemory.hpp

6 个定义；已讲解 6 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `kforward` | [L50](../../kernel/memory/kmemory.hpp#L50) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemoryhpp)（讲义第163行） | [H(d)](images/heap-memory.png) |
| `kforward` | [L56](../../kernel/memory/kmemory.hpp#L56) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemoryhpp)（讲义第164行） | [H(d)](images/heap-memory.png) |
| `operator new` | [L63](../../kernel/memory/kmemory.hpp#L63) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemoryhpp)（讲义第165行） | [H(d)](images/heap-memory.png) |
| `operator delete` | [L67](../../kernel/memory/kmemory.hpp#L67) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemoryhpp)（讲义第166行） | [H(d)](images/heap-memory.png) |
| `knew` | [L71](../../kernel/memory/kmemory.hpp#L71) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemoryhpp)（讲义第167行） | [H(d)](images/heap-memory.png) |
| `kdelete` | [L83](../../kernel/memory/kmemory.hpp#L83) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorykmemoryhpp)（讲义第168行） | [H(d)](images/heap-memory.png) |

### kernel/memory/page_allocator.cpp

12 个定义；已讲解 12 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `align_up` | [L5](../../kernel/memory/page_allocator.cpp#L5) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第57行） | [A(b)](images/physical-pages.png) |
| `clipped_end` | [L9](../../kernel/memory/page_allocator.cpp#L9) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第58行） | [A(a)](images/physical-pages.png) |
| `change_available_range` | [L18](../../kernel/memory/page_allocator.cpp#L18) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第59行） | [A(b)](images/physical-pages.png) |
| `usable` | [L31](../../kernel/memory/page_allocator.cpp#L31) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第60行） | [A(a)](images/physical-pages.png) |
| `allocate_between` | [L35](../../kernel/memory/page_allocator.cpp#L35) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第61行） | [A(c)](images/physical-pages.png) |
| `initialize_page_allocator` | [L78](../../kernel/memory/page_allocator.cpp#L78) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第62行） | [A(a)、A(b)](images/physical-pages.png) |
| `alloc_page` | [L158](../../kernel/memory/page_allocator.cpp#L158) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第63行） | [A(c)](images/physical-pages.png) |
| `alloc_page_below` | [L162](../../kernel/memory/page_allocator.cpp#L162) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第64行） | [A(c)](images/physical-pages.png) |
| `alloc_page_at_least` | [L166](../../kernel/memory/page_allocator.cpp#L166) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第65行） | [A(c)](images/physical-pages.png) |
| `page_allocator_owns_page` | [L170](../../kernel/memory/page_allocator.cpp#L170) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第66行） | [A(d)](images/physical-pages.png) |
| `free_page` | [L182](../../kernel/memory/page_allocator.cpp#L182) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第67行） | [A(d)](images/physical-pages.png) |
| `count_free_pages` | [L192](../../kernel/memory/page_allocator.cpp#L192) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypage_allocatorcpp)（讲义第68行） | [A(f)](images/physical-pages.png) |

### kernel/memory/paging.cpp

23 个定义；已讲解 23 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `read_cr3` | [L16](../../kernel/memory/paging.cpp#L16) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第74行） | [V(a)](images/paging-address-space.png) |
| `invalidate_page` | [L22](../../kernel/memory/paging.cpp#L22) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第75行） | [V(e)](images/paging-address-space.png) |
| `is_page_aligned` | [L26](../../kernel/memory/paging.cpp#L26) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第76行） | [V(a)](images/paging-address-space.png) |
| `pml4_index` | [L30](../../kernel/memory/paging.cpp#L30) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第77行） | [V(a)](images/paging-address-space.png) |
| `pdpt_index` | [L36](../../kernel/memory/paging.cpp#L36) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第78行） | [V(a)](images/paging-address-space.png) |
| `pd_index` | [L40](../../kernel/memory/paging.cpp#L40) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第79行） | [V(a)](images/paging-address-space.png) |
| `pt_index` | [L44](../../kernel/memory/paging.cpp#L44) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第80行） | [V(a)](images/paging-address-space.png) |
| `table_from_physical_address` | [L48](../../kernel/memory/paging.cpp#L48) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第81行） | [V(b)](images/paging-address-space.png) |
| `table_from_entry` | [L54](../../kernel/memory/paging.cpp#L54) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第82行） | [V(a)、V(b)](images/paging-address-space.png) |
| `allocate_page_table_page` | [L58](../../kernel/memory/paging.cpp#L58) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第83行） | [V(c)](images/paging-address-space.png) |
| `ensure_next_level` | [L78](../../kernel/memory/paging.cpp#L78) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第84行） | [V(c)](images/paging-address-space.png) |
| `enable_no_execute` | [L114](../../kernel/memory/paging.cpp#L114) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第85行） | [V(f)](images/paging-address-space.png) |
| `paging_initialize_direct_map` | [L135](../../kernel/memory/paging.cpp#L135) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第86行） | [V(b)](images/paging-address-space.png) |
| `paging_physical_pointer` | [L172](../../kernel/memory/paging.cpp#L172) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第87行） | [V(b)](images/paging-address-space.png) |
| `paging_managed_physical_limit` | [L183](../../kernel/memory/paging.cpp#L183) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第88行） | [V(b)](images/paging-address-space.png) |
| `paging_no_execute_enabled` | [L187](../../kernel/memory/paging.cpp#L187) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第89行） | [V(f)](images/paging-address-space.png) |
| `paging_current_root_physical` | [L191](../../kernel/memory/paging.cpp#L191) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第90行） | [V(a)](images/paging-address-space.png) |
| `map_page` | [L195](../../kernel/memory/paging.cpp#L195) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第91行） | [V(c)](images/paging-address-space.png) |
| `map_page_internal` | [L201](../../kernel/memory/paging.cpp#L201) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第92行） | [V(c)、V(e)](images/paging-address-space.png) |
| `map_page_in_root` | [L258](../../kernel/memory/paging.cpp#L258) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第93行） | [V(c)](images/paging-address-space.png) |
| `map_device_page` | [L263](../../kernel/memory/paging.cpp#L263) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第94行） | [V(f)](images/paging-address-space.png) |
| `map_identity_range` | [L270](../../kernel/memory/paging.cpp#L270) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第95行） | [V(b)、V(c)](images/paging-address-space.png) |
| `resolve_physical_address_in_root` | [L286](../../kernel/memory/paging.cpp#L286) | [讲解](SYSTEM_MEMORY_FUNCTIONS.md#kernelmemorypagingcpp)（讲义第96行） | [V(a)](images/paging-address-space.png) |

### kernel/net/network.cpp

41 个定义；已讲解 41 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `restore_interrupts` | [L52](../../kernel/net/network.cpp#L52) | [讲解](COOPERATION_FUNCTIONS.md#restore_interrupts) | [U(c)](images/udp-wait.png) |
| `wake_udp_receivers` | [L53](../../kernel/net/network.cpp#L53) | [讲解](COOPERATION_FUNCTIONS.md#wake_udp_receivers) | [U(d)、U(f)](images/udp-wait.png) |
| `read16` | [L65](../../kernel/net/network.cpp#L65) | [讲解](COOPERATION_FUNCTIONS.md#read16) | [U(d)](images/udp-wait.png) |
| `read32` | [L66](../../kernel/net/network.cpp#L66) | [讲解](COOPERATION_FUNCTIONS.md#read32) | [U(d)](images/udp-wait.png) |
| `write16` | [L67](../../kernel/net/network.cpp#L67) | [讲解](COOPERATION_FUNCTIONS.md#write16) | [U(d)](images/udp-wait.png) |
| `write32` | [L68](../../kernel/net/network.cpp#L68) | [讲解](COOPERATION_FUNCTIONS.md#write32) | [U(d)](images/udp-wait.png) |
| `equal_mac` | [L72](../../kernel/net/network.cpp#L72) | [讲解](COOPERATION_FUNCTIONS.md#equal_mac) | [U(d)](images/udp-wait.png) |
| `broadcast_mac` | [L76](../../kernel/net/network.cpp#L76) | [讲解](COOPERATION_FUNCTIONS.md#broadcast_mac) | [U(d)](images/udp-wait.png) |
| `unicast_mac` | [L80](../../kernel/net/network.cpp#L80) | [讲解](COOPERATION_FUNCTIONS.md#unicast_mac) | [U(d)](images/udp-wait.png) |
| `checksum_sum` | [L85](../../kernel/net/network.cpp#L85) | [讲解](COOPERATION_FUNCTIONS.md#checksum_sum) | [U(d)](images/udp-wait.png) |
| `checksum_finish` | [L90](../../kernel/net/network.cpp#L90) | [讲解](COOPERATION_FUNCTIONS.md#checksum_finish) | [U(d)](images/udp-wait.png) |
| `checksum` | [L94](../../kernel/net/network.cpp#L94) | [讲解](COOPERATION_FUNCTIONS.md#checksum) | [U(d)](images/udp-wait.png) |
| `udp_checksum` | [L95](../../kernel/net/network.cpp#L95) | [讲解](COOPERATION_FUNCTIONS.md#udp_checksum) | [U(d)](images/udp-wait.png) |
| `timeout_ticks` | [L101](../../kernel/net/network.cpp#L101) | [讲解](COOPERATION_FUNCTIONS.md#timeout_ticks) | [U(b)](images/udp-wait.png) |
| `valid_destination` | [L104](../../kernel/net/network.cpp#L104) | [讲解](COOPERATION_FUNCTIONS.md#valid_destination) | [U(a)、U(d)](images/udp-wait.png) |
| `next_hop` | [L110](../../kernel/net/network.cpp#L110) | [讲解](COOPERATION_FUNCTIONS.md#next_hop) | [U(d)](images/udp-wait.png) |
| `learn_arp` | [L114](../../kernel/net/network.cpp#L114) | [讲解](COOPERATION_FUNCTIONS.md#learn_arp) | [U(d)](images/udp-wait.png) |
| `find_arp` | [L128](../../kernel/net/network.cpp#L128) | [讲解](COOPERATION_FUNCTIONS.md#find_arp) | [U(d)](images/udp-wait.png) |
| `ethernet_header` | [L137](../../kernel/net/network.cpp#L137) | [讲解](COOPERATION_FUNCTIONS.md#ethernet_header) | [U(d)](images/udp-wait.png) |
| `transmit_frame` | [L142](../../kernel/net/network.cpp#L142) | [讲解](COOPERATION_FUNCTIONS.md#transmit_frame) | [U(d)](images/udp-wait.png) |
| `send_arp` | [L150](../../kernel/net/network.cpp#L150) | [讲解](COOPERATION_FUNCTIONS.md#send_arp) | [U(d)](images/udp-wait.png) |
| `resolve_mac` | [L162](../../kernel/net/network.cpp#L162) | [讲解](COOPERATION_FUNCTIONS.md#resolve_mac) | [U(b)、U(d)](images/udp-wait.png) |
| `send_ipv4` | [L185](../../kernel/net/network.cpp#L185) | [讲解](COOPERATION_FUNCTIONS.md#send_ipv4) | [U(d)](images/udp-wait.png) |
| `send_udp` | [L199](../../kernel/net/network.cpp#L199) | [讲解](COOPERATION_FUNCTIONS.md#send_udp) | [U(d)](images/udp-wait.png) |
| `receive_arp` | [L212](../../kernel/net/network.cpp#L212) | [讲解](COOPERATION_FUNCTIONS.md#receive_arp) | [U(d)](images/udp-wait.png) |
| `receive_icmp` | [L228](../../kernel/net/network.cpp#L228) | [讲解](COOPERATION_FUNCTIONS.md#receive_icmp) | [U(d)](images/udp-wait.png) |
| `receive_udp` | [L245](../../kernel/net/network.cpp#L245) | [讲解](COOPERATION_FUNCTIONS.md#receive_udp) | [U(d)](images/udp-wait.png) |
| `receive_frame` | [L273](../../kernel/net/network.cpp#L273) | [讲解](COOPERATION_FUNCTIONS.md#receive_frame) | [U(d)](images/udp-wait.png) |
| `lookup_socket` | [L299](../../kernel/net/network.cpp#L299) | [讲解](COOPERATION_FUNCTIONS.md#lookup_socket) | [U(a)、U(e)](images/udp-wait.png) |
| `network_initialize` | [L309](../../kernel/net/network.cpp#L309) | [讲解](COOPERATION_FUNCTIONS.md#network_initialize) | [U(d)](images/udp-wait.png) |
| `network_status` | [L319](../../kernel/net/network.cpp#L319) | [讲解](COOPERATION_FUNCTIONS.md#network_status) | [U(f)](images/udp-wait.png) |
| `network_poll` | [L329](../../kernel/net/network.cpp#L329) | [讲解](COOPERATION_FUNCTIONS.md#network_poll) | [U(d)](images/udp-wait.png) |
| `network_parse_ipv4` | [L340](../../kernel/net/network.cpp#L340) | [讲解](COOPERATION_FUNCTIONS.md#network_parse_ipv4) | [U(a)](images/udp-wait.png) |
| `network_format_ipv4` | [L356](../../kernel/net/network.cpp#L356) | [讲解](COOPERATION_FUNCTIONS.md#network_format_ipv4) | [U(a)](images/udp-wait.png) |
| `network_ping` | [L368](../../kernel/net/network.cpp#L368) | [讲解](COOPERATION_FUNCTIONS.md#network_ping) | [U(b)、U(d)](images/udp-wait.png) |
| `network_udp_open` | [L397](../../kernel/net/network.cpp#L397) | [讲解](COOPERATION_FUNCTIONS.md#network_udp_open) | [U(a)](images/udp-wait.png) |
| `network_udp_close` | [L410](../../kernel/net/network.cpp#L410) | [讲解](COOPERATION_FUNCTIONS.md#network_udp_close) | [U(f)](images/udp-wait.png) |
| `network_udp_close_owner` | [L420](../../kernel/net/network.cpp#L420) | [讲解](COOPERATION_FUNCTIONS.md#network_udp_close_owner) | [U(f)](images/udp-wait.png) |
| `network_udp_send` | [L429](../../kernel/net/network.cpp#L429) | [讲解](COOPERATION_FUNCTIONS.md#network_udp_send) | [U(a)、U(d)](images/udp-wait.png) |
| `network_udp_receive` | [L442](../../kernel/net/network.cpp#L442) | [讲解](COOPERATION_FUNCTIONS.md#network_udp_receive) | [U(e)](images/udp-wait.png) |
| `network_udp_receive_wait` | [L459](../../kernel/net/network.cpp#L459) | [讲解](COOPERATION_FUNCTIONS.md#network_udp_receive_wait) | [U(a)、U(f)](images/udp-wait.png) |

### kernel/net/network.hpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `network_ipv4` | [L10](../../kernel/net/network.hpp#L10) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第285行） | [V(e)](images/virtio-dma.png) |

### kernel/net/network_irq.cpp

3 个定义；已讲解 3 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `network_enable_irq` | [L9](../../kernel/net/network_irq.cpp#L9) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第282行） | [V(e)](images/virtio-dma.png)；[H(b)](images/interrupt-devices.png) |
| `network_handle_irq` | [L22](../../kernel/net/network_irq.cpp#L22) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第283行） | [V(e)](images/virtio-dma.png)；[H(d)](images/interrupt-devices.png) |
| `network_wait_for_event` | [L28](../../kernel/net/network_irq.cpp#L28) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第284行） | [V(e)](images/virtio-dma.png)；[H(e)](images/interrupt-devices.png) |

### kernel/net/virtio_net.cpp

24 个定义；已讲解 24 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `in8` | [L42](../../kernel/net/virtio_net.cpp#L42) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第262行） | [V(d)](images/virtio-dma.png)；[H(d)](images/interrupt-devices.png) |
| `in16` | [L43](../../kernel/net/virtio_net.cpp#L43) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第263行） | [V(a)](images/virtio-dma.png) |
| `in32` | [L44](../../kernel/net/virtio_net.cpp#L44) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第264行） | [V(a)](images/virtio-dma.png) |
| `out8` | [L45](../../kernel/net/virtio_net.cpp#L45) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第265行） | [V(f)](images/virtio-dma.png) |
| `out16` | [L46](../../kernel/net/virtio_net.cpp#L46) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第266行） | [V(c)](images/virtio-dma.png) |
| `out32` | [L47](../../kernel/net/virtio_net.cpp#L47) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第267行） | [V(a)](images/virtio-dma.png) |
| `publish_barrier` | [L48](../../kernel/net/virtio_net.cpp#L48) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第268行） | [V(c)](images/virtio-dma.png) |
| `acquire_barrier` | [L49](../../kernel/net/virtio_net.cpp#L49) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第269行） | [V(d)](images/virtio-dma.png) |
| `align_page` | [L50](../../kernel/net/virtio_net.cpp#L50) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第270行） | [V(a)](images/virtio-dma.png) |
| `allocate_ring` | [L54](../../kernel/net/virtio_net.cpp#L54) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio_netallocate_ringqueue-id--vavf) | [V(a)、V(f)](images/virtio-dma.png) |
| `release_pages` | [L96](../../kernel/net/virtio_net.cpp#L96) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第271行） | [V(f)](images/virtio-dma.png) |
| `enqueue` | [L105](../../kernel/net/virtio_net.cpp#L105) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第272行） | [V(c)](images/virtio-dma.png) |
| `notify` | [L109](../../kernel/net/virtio_net.cpp#L109) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第273行） | [V(c)](images/virtio-dma.png) |
| `configure_chain` | [L116](../../kernel/net/virtio_net.cpp#L116) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第274行） | [V(b)](images/virtio-dma.png) |
| `fail_device` | [L128](../../kernel/net/virtio_net.cpp#L128) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第275行） | [V(f)](images/virtio-dma.png) |
| `reap_transmit` | [L135](../../kernel/net/virtio_net.cpp#L135) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第276行） | [V(d)、V(f)](images/virtio-dma.png) |
| `virtio_net_initialize` | [L149](../../kernel/net/virtio_net.cpp#L149) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio_netvirtio_net_initializeallocator--havavbvf) | [H(a)](images/interrupt-devices.png)；[V(a)、V(b)、V(f)](images/virtio-dma.png) |
| `virtio_net_status` | [L213](../../kernel/net/virtio_net.cpp#L213) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第277行） | [O(e)](images/logging-observability.png)；[V(f)](images/virtio-dma.png) |
| `virtio_net_poll` | [L214](../../kernel/net/virtio_net.cpp#L214) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio_netvirtio_net_pollbudget-handler-context--vdvevf) | [V(d)、V(e)、V(f)](images/virtio-dma.png) |
| `virtio_net_send` | [L255](../../kernel/net/virtio_net.cpp#L255) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio_netvirtio_net_sendframe-bytes--vbvcvd) | [V(b)、V(c)、V(d)](images/virtio-dma.png) |
| `virtio_net_enable_receive_interrupts` | [L276](../../kernel/net/virtio_net.cpp#L276) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第278行） | [H(b)](images/interrupt-devices.png)；[V(e)](images/virtio-dma.png) |
| `virtio_net_disable_receive_interrupts` | [L296](../../kernel/net/virtio_net.cpp#L296) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第279行） | [H(b)](images/interrupt-devices.png)；[V(f)](images/virtio-dma.png) |
| `virtio_net_acknowledge_irq` | [L306](../../kernel/net/virtio_net.cpp#L306) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第280行） | [H(d)](images/interrupt-devices.png)；[V(e)](images/virtio-dma.png) |
| `virtio_net_receive_pending` | [L317](../../kernel/net/virtio_net.cpp#L317) | [讲解](DEVICES_IO_FUNCTIONS.md#virtio-其余函数与网络irq逐一对照)（讲义第281行） | [V(e)](images/virtio-dma.png) |

### kernel/perf/perf.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `performance_snapshot` | [L8](../../kernel/perf/perf.cpp#L8) | [讲解](DEVICES_IO_FUNCTIONS.md#日志与性能先准确记录再解释数字)（讲义第304行） | [O(e)](images/logging-observability.png) |

### kernel/runtime/runtime.cpp

6 个定义；已讲解 6 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `memory_set` | [L3](../../kernel/runtime/runtime.cpp#L3) | [讲解](DEVICES_IO_FUNCTIONS.md#freestanding-字节工具逐函数表--of)（讲义第312行） | [O(f)](images/logging-observability.png) |
| `memset` | [L17](../../kernel/runtime/runtime.cpp#L17) | [讲解](DEVICES_IO_FUNCTIONS.md#freestanding-字节工具逐函数表--of)（讲义第313行） | [O(f)](images/logging-observability.png) |
| `memcpy` | [L21](../../kernel/runtime/runtime.cpp#L21) | [讲解](DEVICES_IO_FUNCTIONS.md#freestanding-字节工具逐函数表--of)（讲义第314行） | [O(f)](images/logging-observability.png) |
| `memmove` | [L25](../../kernel/runtime/runtime.cpp#L25) | [讲解](DEVICES_IO_FUNCTIONS.md#freestanding-字节工具逐函数表--of)（讲义第315行） | [O(f)](images/logging-observability.png) |
| `memcmp` | [L36](../../kernel/runtime/runtime.cpp#L36) | [讲解](DEVICES_IO_FUNCTIONS.md#freestanding-字节工具逐函数表--of)（讲义第316行） | [O(f)](images/logging-observability.png) |
| `memory_copy` | [L46](../../kernel/runtime/runtime.cpp#L46) | [讲解](DEVICES_IO_FUNCTIONS.md#freestanding-字节工具逐函数表--of)（讲义第317行） | [O(f)](images/logging-observability.png) |

### kernel/shell/parser.cpp

10 个定义；已讲解 10 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `space` | [L7](../../kernel/shell/parser.cpp#L7) | [讲解](SHELL_FUNCTIONS.md#parserspace) | [Q(a)](images/shell-parser.png) |
| `name_char` | [L8](../../kernel/shell/parser.cpp#L8) | [讲解](SHELL_FUNCTIONS.md#parsername_char) | [Q(c)](images/shell-parser.png) |
| `equal` | [L12](../../kernel/shell/parser.cpp#L12) | [讲解](SHELL_FUNCTIONS.md#parserequal) | [Q(c)](images/shell-parser.png) |
| `append` | [L16](../../kernel/shell/parser.cpp#L16) | [讲解](SHELL_FUNCTIONS.md#parserappend) | [Q(b)](images/shell-parser.png) |
| `append_text` | [L20](../../kernel/shell/parser.cpp#L20) | [讲解](SHELL_FUNCTIONS.md#parserappend_text) | [Q(c)](images/shell-parser.png) |
| `copy` | [L25](../../kernel/shell/parser.cpp#L25) | [讲解](SHELL_FUNCTIONS.md#parsercopy) | [Q(f)](images/shell-parser.png) |
| `expand` | [L28](../../kernel/shell/parser.cpp#L28) | [讲解](SHELL_FUNCTIONS.md#parserexpand) | [Q(c)](images/shell-parser.png) |
| `next` | [L58](../../kernel/shell/parser.cpp#L58) | [讲解](SHELL_FUNCTIONS.md#parsernext) | [Q(b)](images/shell-parser.png) |
| `shell_parse_line` | [L104](../../kernel/shell/parser.cpp#L104) | [讲解](SHELL_FUNCTIONS.md#parsershell_parse_line) | [Q(a)、Q(b)、Q(c)、Q(d)、Q(e)、Q(f)](images/shell-parser.png) |
| `shell_expand_command` | [L160](../../kernel/shell/parser.cpp#L160) | [讲解](SHELL_FUNCTIONS.md#parsershell_expand_command) | [Q(c)、Q(e)](images/shell-parser.png) |

### kernel/shell/shell.cpp

77 个定义；已讲解 77 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `write_char` | [L33](../../kernel/shell/shell.cpp#L33) | [讲解](SHELL_FUNCTIONS.md#shellwrite_char) | [I(e)、I(f)](images/console-input.png) |
| `write_string` | [L41](../../kernel/shell/shell.cpp#L41) | [讲解](SHELL_FUNCTIONS.md#shellwrite_string) | [I(e)、I(f)](images/console-input.png) |
| `write_newline` | [L51](../../kernel/shell/shell.cpp#L51) | [讲解](SHELL_FUNCTIONS.md#shellwrite_newline) | [I(e)、I(f)](images/console-input.png) |
| `clear_output` | [L55](../../kernel/shell/shell.cpp#L55) | [讲解](SHELL_FUNCTIONS.md#shellclear_output) | [I(e)、I(f)](images/console-input.png) |
| `set_output_color` | [L63](../../kernel/shell/shell.cpp#L63) | [讲解](SHELL_FUNCTIONS.md#shellset_output_color) | [I(e)、I(f)](images/console-input.png) |
| `write_u64` | [L71](../../kernel/shell/shell.cpp#L71) | [讲解](SHELL_FUNCTIONS.md#shellwrite_u64) | [I(e)、I(f)](images/console-input.png) |
| `write_hex_nibble` | [L90](../../kernel/shell/shell.cpp#L90) | [讲解](SHELL_FUNCTIONS.md#shellwrite_hex_nibble) | [I(e)、I(f)](images/console-input.png) |
| `write_hex64` | [L99](../../kernel/shell/shell.cpp#L99) | [讲解](SHELL_FUNCTIONS.md#shellwrite_hex64) | [I(e)、I(f)](images/console-input.png) |
| `write_bounded_string` | [L106](../../kernel/shell/shell.cpp#L106) | [讲解](SHELL_FUNCTIONS.md#shellwrite_bounded_string) | [I(e)、I(f)](images/console-input.png) |
| `is_space_char` | [L118](../../kernel/shell/shell.cpp#L118) | [讲解](SHELL_FUNCTIONS.md#shellis_space_char) | [Q(a)](images/shell-parser.png) |
| `skip_spaces` | [L122](../../kernel/shell/shell.cpp#L122) | [讲解](SHELL_FUNCTIONS.md#shellskip_spaces) | [Q(a)](images/shell-parser.png) |
| `is_empty_after_trim` | [L134](../../kernel/shell/shell.cpp#L134) | [讲解](SHELL_FUNCTIONS.md#shellis_empty_after_trim) | [I(d)](images/console-input.png) |
| `string_length` | [L139](../../kernel/shell/shell.cpp#L139) | [讲解](SHELL_FUNCTIONS.md#shellstring_length) | [I(e)、I(f)](images/console-input.png) |
| `path_leaf_name` | [L152](../../kernel/shell/shell.cpp#L152) | [讲解](SHELL_FUNCTIONS.md#shellpath_leaf_name) | [Q(e)](images/shell-parser.png)；[X(a)、X(f)](images/shell-pipeline.png) |
| `trim_trailing_spaces` | [L170](../../kernel/shell/shell.cpp#L170) | [讲解](SHELL_FUNCTIONS.md#shelltrim_trailing_spaces) | [Q(a)](images/shell-parser.png) |
| `copy_text_slice` | [L178](../../kernel/shell/shell.cpp#L178) | [讲解](SHELL_FUNCTIONS.md#shellcopy_text_slice) | [Q(f)](images/shell-parser.png) |
| `split_path_and_text` | [L199](../../kernel/shell/shell.cpp#L199) | [讲解](SHELL_FUNCTIONS.md#shellsplit_path_and_text) | [Q(e)](images/shell-parser.png)；[X(a)、X(f)](images/shell-pipeline.png) |
| `is_boot_info_valid` | [L233](../../kernel/shell/shell.cpp#L233) | [讲解](SHELL_FUNCTIONS.md#shellis_boot_info_valid) | [O(e)](images/logging-observability.png) |
| `memory_kind_name` | [L240](../../kernel/shell/shell.cpp#L240) | [讲解](SHELL_FUNCTIONS.md#shellmemory_kind_name) | [O(e)](images/logging-observability.png) |
| `read_cpuid` | [L248](../../kernel/shell/shell.cpp#L248) | [讲解](SHELL_FUNCTIONS.md#shellread_cpuid) | [O(e)](images/logging-observability.png) |
| `history_slot_index` | [L261](../../kernel/shell/shell.cpp#L261) | [讲解](SHELL_FUNCTIONS.md#shellhistory_slot_index) | [I(d)](images/console-input.png) |
| `history_provider_entry_count` | [L276](../../kernel/shell/shell.cpp#L276) | [讲解](SHELL_FUNCTIONS.md#shellhistory_provider_entry_count) | [I(d)](images/console-input.png) |
| `history_provider_entry_text` | [L281](../../kernel/shell/shell.cpp#L281) | [讲解](SHELL_FUNCTIONS.md#shellhistory_provider_entry_text) | [I(d)](images/console-input.png) |
| `record_history_line` | [L292](../../kernel/shell/shell.cpp#L292) | [讲解](SHELL_FUNCTIONS.md#shellrecord_history_line) | [I(d)](images/console-input.png) |
| `command_matches` | [L337](../../kernel/shell/shell.cpp#L337) | [讲解](SHELL_FUNCTIONS.md#shellcommand_matches) | [Q(e)](images/shell-parser.png)；[X(a)、X(f)](images/shell-pipeline.png) |
| `handle_help_command` | [L375](../../kernel/shell/shell.cpp#L375) | [讲解](SHELL_FUNCTIONS.md#shellhandle_help_command) | [Q(e)](images/shell-parser.png)；[X(a)、X(f)](images/shell-pipeline.png) |
| `handle_mem_command` | [L440](../../kernel/shell/shell.cpp#L440) | [讲解](SHELL_FUNCTIONS.md#shellhandle_mem_command) | [O(e)](images/logging-observability.png)；[L(b)](images/os64fs-layout.png) |
| `handle_ticks_command` | [L459](../../kernel/shell/shell.cpp#L459) | [讲解](SHELL_FUNCTIONS.md#shellhandle_ticks_command) | [H(c)](images/interrupt-devices.png) |
| `handle_heap_command` | [L465](../../kernel/shell/shell.cpp#L465) | [讲解](SHELL_FUNCTIONS.md#shellhandle_heap_command) | [O(e)](images/logging-observability.png) |
| `handle_disk_command` | [L497](../../kernel/shell/shell.cpp#L497) | [讲解](SHELL_FUNCTIONS.md#shellhandle_disk_command) | [L(a)、L(b)](images/os64fs-layout.png) |
| `handle_pwd_command` | [L570](../../kernel/shell/shell.cpp#L570) | [讲解](SHELL_FUNCTIONS.md#shellhandle_pwd_command) | [V(b)](images/vfs-paths.png) |
| `handle_cd_command` | [L590](../../kernel/shell/shell.cpp#L590) | [讲解](SHELL_FUNCTIONS.md#shellhandle_cd_command) | [V(b)](images/vfs-paths.png) |
| `handle_ls_command` | [L633](../../kernel/shell/shell.cpp#L633) | [讲解](SHELL_FUNCTIONS.md#shellhandle_ls_command) | [V(d)](images/vfs-paths.png) |
| `handle_cat_command` | [L732](../../kernel/shell/shell.cpp#L732) | [讲解](SHELL_FUNCTIONS.md#shellhandle_cat_command) | [V(c)、V(e)](images/vfs-paths.png) |
| `handle_stat_command` | [L819](../../kernel/shell/shell.cpp#L819) | [讲解](SHELL_FUNCTIONS.md#shellhandle_stat_command) | [V(a)、V(b)](images/vfs-paths.png) |
| `handle_touch_command` | [L906](../../kernel/shell/shell.cpp#L906) | [讲解](SHELL_FUNCTIONS.md#shellhandle_touch_command) | [T(a)、T(b)、T(c)、T(d)、T(e)](images/filesystem-transactions.png) |
| `handle_mkdir_command` | [L970](../../kernel/shell/shell.cpp#L970) | [讲解](SHELL_FUNCTIONS.md#shellhandle_mkdir_command) | [T(a)、T(b)、T(c)、T(d)、T(e)](images/filesystem-transactions.png) |
| `handle_write_command` | [L1014](../../kernel/shell/shell.cpp#L1014) | [讲解](SHELL_FUNCTIONS.md#shellhandle_write_command) | [T(a)、T(b)、T(c)、T(d)、T(e)](images/filesystem-transactions.png) |
| `handle_append_command` | [L1061](../../kernel/shell/shell.cpp#L1061) | [讲解](SHELL_FUNCTIONS.md#shellhandle_append_command) | [T(a)、T(b)、T(c)、T(d)、T(e)](images/filesystem-transactions.png) |
| `handle_rm_command` | [L1108](../../kernel/shell/shell.cpp#L1108) | [讲解](SHELL_FUNCTIONS.md#shellhandle_rm_command) | [T(a)、T(b)、T(c)、T(d)、T(e)](images/filesystem-transactions.png) |
| `handle_sync_command` | [L1146](../../kernel/shell/shell.cpp#L1146) | [讲解](SHELL_FUNCTIONS.md#shellhandle_sync_command) | [T(f)](images/filesystem-transactions.png) |
| `handle_run_command` | [L1169](../../kernel/shell/shell.cpp#L1169) | [讲解](SHELL_FUNCTIONS.md#shellhandle_run_command) | [X(d)、X(f)](images/shell-pipeline.png) |
| `handle_ps_command` | [L1356](../../kernel/shell/shell.cpp#L1356) | [讲解](SHELL_FUNCTIONS.md#shellhandle_ps_command) | [O(e)](images/logging-observability.png) |
| `handle_power_command` | [L1371](../../kernel/shell/shell.cpp#L1371) | [讲解](SHELL_FUNCTIONS.md#shellhandle_power_command) | [H(f)](images/interrupt-devices.png) |
| `handle_irq_command` | [L1399](../../kernel/shell/shell.cpp#L1399) | [讲解](SHELL_FUNCTIONS.md#shellhandle_irq_command) | [H(b)、H(c)、H(d)](images/interrupt-devices.png) |
| `handle_bootinfo_command` | [L1421](../../kernel/shell/shell.cpp#L1421) | [讲解](SHELL_FUNCTIONS.md#shellhandle_bootinfo_command) | [H(f)](images/interrupt-devices.png) |
| `handle_e820_command` | [L1461](../../kernel/shell/shell.cpp#L1461) | [讲解](SHELL_FUNCTIONS.md#shellhandle_e820_command) | [L(b)](images/os64fs-layout.png) |
| `handle_smp_command` | [L1492](../../kernel/shell/shell.cpp#L1492) | [讲解](SHELL_FUNCTIONS.md#shellhandle_smp_command) | [O(e)](images/logging-observability.png) |
| `handle_cpu_command` | [L1505](../../kernel/shell/shell.cpp#L1505) | [讲解](SHELL_FUNCTIONS.md#shellhandle_cpu_command) | [O(e)](images/logging-observability.png) |
| `handle_uptime_command` | [L1547](../../kernel/shell/shell.cpp#L1547) | [讲解](SHELL_FUNCTIONS.md#shellhandle_uptime_command) | [H(c)](images/interrupt-devices.png) |
| `handle_echo_command` | [L1570](../../kernel/shell/shell.cpp#L1570) | [讲解](SHELL_FUNCTIONS.md#shellhandle_echo_command) | [Q(e)](images/shell-parser.png)；[X(a)、X(f)](images/shell-pipeline.png) |
| `handle_history_command` | [L1579](../../kernel/shell/shell.cpp#L1579) | [讲解](SHELL_FUNCTIONS.md#shellhandle_history_command) | [I(d)](images/console-input.png) |
| `initialize_shell` | [L1610](../../kernel/shell/shell.cpp#L1610) | [讲解](SHELL_FUNCTIONS.md#shellinitialize_shell) | [I(d)、I(e)](images/console-input.png)；[X(b)](images/shell-pipeline.png) |
| `shell_print_prompt` | [L1652](../../kernel/shell/shell.cpp#L1652) | [讲解](SHELL_FUNCTIONS.md#shellshell_print_prompt) | [I(e)、I(f)](images/console-input.png) |
| `shell_history_entry_count` | [L1659](../../kernel/shell/shell.cpp#L1659) | [讲解](SHELL_FUNCTIONS.md#shellshell_history_entry_count) | [I(d)](images/console-input.png) |
| `shell_history_entry_text` | [L1667](../../kernel/shell/shell.cpp#L1667) | [讲解](SHELL_FUNCTIONS.md#shellshell_history_entry_text) | [I(d)](images/console-input.png) |
| `shell_execute_legacy_line` | [L1676](../../kernel/shell/shell.cpp#L1676) | [讲解](SHELL_FUNCTIONS.md#shellshell_execute_legacy_line) | [Q(e)](images/shell-parser.png)；[X(a)、X(f)](images/shell-pipeline.png) |
| `same_word` | [L1856](../../kernel/shell/shell.cpp#L1856) | [讲解](SHELL_FUNCTIONS.md#shellsame_word) | [Q(e)](images/shell-parser.png)；[X(a)、X(f)](images/shell-pipeline.png) |
| `legacy_builtin` | [L1860](../../kernel/shell/shell.cpp#L1860) | [讲解](SHELL_FUNCTIONS.md#shelllegacy_builtin) | [Q(e)](images/shell-parser.png)；[X(a)、X(f)](images/shell-pipeline.png) |
| `show_performance` | [L1867](../../kernel/shell/shell.cpp#L1867) | [讲解](SHELL_FUNCTIONS.md#shellshow_performance) | [O(e)](images/logging-observability.png) |
| `show_network` | [L1884](../../kernel/shell/shell.cpp#L1884) | [讲解](SHELL_FUNCTIONS.md#shellshow_network) | [O(e)](images/logging-observability.png) |
| `ping_command` | [L1913](../../kernel/shell/shell.cpp#L1913) | [讲解](SHELL_FUNCTIONS.md#shellping_command) | [H(e)](images/interrupt-devices.png) |
| `append_log_number` | [L1924](../../kernel/shell/shell.cpp#L1924) | [讲解](SHELL_FUNCTIONS.md#shellappend_log_number) | [O(d)](images/logging-observability.png) |
| `append_log_text` | [L1930](../../kernel/shell/shell.cpp#L1930) | [讲解](SHELL_FUNCTIONS.md#shellappend_log_text) | [O(d)](images/logging-observability.png) |
| `format_log_record` | [L1933](../../kernel/shell/shell.cpp#L1933) | [讲解](SHELL_FUNCTIONS.md#shellformat_log_record) | [O(a)、O(d)](images/logging-observability.png) |
| `show_or_save_log` | [L1941](../../kernel/shell/shell.cpp#L1941) | [讲解](SHELL_FUNCTIONS.md#shellshow_or_save_log) | [O(d)](images/logging-observability.png)；[T(e)、T(f)](images/filesystem-transactions.png) |
| `redirect_present` | [L1966](../../kernel/shell/shell.cpp#L1966) | [讲解](SHELL_FUNCTIONS.md#shellredirect_present) | [X(c)](images/shell-pipeline.png) |
| `refresh_job` | [L1969](../../kernel/shell/shell.cpp#L1969) | [讲解](SHELL_FUNCTIONS.md#shellrefresh_job) | [X(f)](images/shell-pipeline.png) |
| `jobs_command` | [L1983](../../kernel/shell/shell.cpp#L1983) | [讲解](SHELL_FUNCTIONS.md#shelljobs_command) | [X(f)](images/shell-pipeline.png) |
| `restore_streams` | [L1998](../../kernel/shell/shell.cpp#L1998) | [讲解](SHELL_FUNCTIONS.md#shellrestore_streams) | [X(b)、X(e)](images/shell-pipeline.png) |
| `close_descriptor` | [L2005](../../kernel/shell/shell.cpp#L2005) | [讲解](SHELL_FUNCTIONS.md#shellclose_descriptor) | [X(e)](images/shell-pipeline.png) |
| `apply_redirect` | [L2008](../../kernel/shell/shell.cpp#L2008) | [讲解](SHELL_FUNCTIONS.md#shellapply_redirect) | [X(c)](images/shell-pipeline.png) |
| `launch_pipeline` | [L2017](../../kernel/shell/shell.cpp#L2017) | [讲解](SHELL_FUNCTIONS.md#shelllaunch_pipeline) | [X(a)、X(b)、X(c)、X(d)、X(e)、X(f)](images/shell-pipeline.png) |
| `shell_execute_line` | [L2108](../../kernel/shell/shell.cpp#L2108) | [讲解](SHELL_FUNCTIONS.md#shellshell_execute_line) | [Q(a)、Q(b)、Q(c)、Q(d)、Q(e)、Q(f)](images/shell-parser.png)；[X(a)、X(b)、X(c)、X(d)、X(e)、X(f)](images/shell-pipeline.png) |
| `shell_command_result_name` | [L2201](../../kernel/shell/shell.cpp#L2201) | [讲解](SHELL_FUNCTIONS.md#shellshell_command_result_name) | [Q(e)](images/shell-parser.png)；[X(a)、X(f)](images/shell-pipeline.png) |
| `shell_run_once` | [L2214](../../kernel/shell/shell.cpp#L2214) | [讲解](SHELL_FUNCTIONS.md#shellshell_run_once) | [I(d)、I(f)](images/console-input.png)；[Q(a)](images/shell-parser.png) |
| `shell_run_forever` | [L2252](../../kernel/shell/shell.cpp#L2252) | [讲解](SHELL_FUNCTIONS.md#shellshell_run_forever) | [I(e)、I(f)](images/console-input.png) |

### kernel/storage/ata_pio.cpp

15 个定义；已讲解 15 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `in8` | [L20](../../kernel/storage/ata_pio.cpp#L20) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第204行） | [S(c)](images/storage-io.png) |
| `out8` | [L25](../../kernel/storage/ata_pio.cpp#L25) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第205行） | [S(c)](images/storage-io.png) |
| `in16` | [L28](../../kernel/storage/ata_pio.cpp#L28) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第206行） | [S(d)](images/storage-io.png) |
| `out16` | [L33](../../kernel/storage/ata_pio.cpp#L33) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第207行） | [S(d)](images/storage-io.png) |
| `settle` | [L36](../../kernel/storage/ata_pio.cpp#L36) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第208行） | [S(c)](images/storage-io.png) |
| `wait_status` | [L40](../../kernel/storage/ata_pio.cpp#L40) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第209行） | [S(c)、S(f)](images/storage-io.png) |
| `issue` | [L55](../../kernel/storage/ata_pio.cpp#L55) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第210行） | [S(c)](images/storage-io.png) |
| `read_adapter` | [L70](../../kernel/storage/ata_pio.cpp#L70) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第211行） | [S(a)](images/storage-io.png) |
| `write_adapter` | [L74](../../kernel/storage/ata_pio.cpp#L74) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第212行） | [S(a)](images/storage-io.png) |
| `flush_adapter` | [L77](../../kernel/storage/ata_pio.cpp#L77) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第213行） | [S(a)、S(e)](images/storage-io.png) |
| `initialize_ata_pio_primary_master` | [L82](../../kernel/storage/ata_pio.cpp#L82) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第214行） | [S(c)、S(f)](images/storage-io.png) |
| `initialize_block_device_from_ata_pio` | [L116](../../kernel/storage/ata_pio.cpp#L116) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第215行） | [S(a)](images/storage-io.png) |
| `ata_pio_read_sector` | [L130](../../kernel/storage/ata_pio.cpp#L130) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第216行） | [S(d)、S(f)](images/storage-io.png) |
| `ata_pio_write_sector` | [L142](../../kernel/storage/ata_pio.cpp#L142) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第217行） | [S(d)、S(e)、S(f)](images/storage-io.png) |
| `ata_pio_flush` | [L153](../../kernel/storage/ata_pio.cpp#L153) | [讲解](DEVICES_IO_FUNCTIONS.md#ata-pio-逐函数表)（讲义第218行） | [S(e)](images/storage-io.png) |

### kernel/storage/block_device.cpp

8 个定义；已讲解 8 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `read_boot_volume_sector` | [L7](../../kernel/storage/block_device.cpp#L7) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第191行） | [S(a)、S(b)](images/storage-io.png) |
| `write_boot_volume_sector` | [L15](../../kernel/storage/block_device.cpp#L15) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第192行） | [S(a)、S(b)](images/storage-io.png) |
| `initialize_block_device_from_boot_volume` | [L25](../../kernel/storage/block_device.cpp#L25) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第193行） | [S(a)](images/storage-io.png) |
| `block_device_is_ready` | [L44](../../kernel/storage/block_device.cpp#L44) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第194行） | [S(a)](images/storage-io.png) |
| `block_device_total_bytes` | [L51](../../kernel/storage/block_device.cpp#L51) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第195行） | [S(a)](images/storage-io.png) |
| `block_device_read_sector` | [L60](../../kernel/storage/block_device.cpp#L60) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第196行） | [S(a)](images/storage-io.png) |
| `block_device_write_sector` | [L74](../../kernel/storage/block_device.cpp#L74) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第197行） | [S(a)、S(e)](images/storage-io.png) |
| `block_device_flush` | [L87](../../kernel/storage/block_device.cpp#L87) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第198行） | [S(e)](images/storage-io.png) |

### kernel/storage/boot_volume.cpp

5 个定义；已讲解 5 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `initialize_boot_volume` | [L5](../../kernel/storage/boot_volume.cpp#L5) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第186行） | [S(a)、S(b)](images/storage-io.png) |
| `boot_volume_is_ready` | [L38](../../kernel/storage/boot_volume.cpp#L38) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第187行） | [S(b)](images/storage-io.png) |
| `boot_volume_total_bytes` | [L42](../../kernel/storage/boot_volume.cpp#L42) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第188行） | [S(a)](images/storage-io.png) |
| `boot_volume_read_sector` | [L51](../../kernel/storage/boot_volume.cpp#L51) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第189行） | [S(b)](images/storage-io.png) |
| `boot_volume_write_sector` | [L66](../../kernel/storage/boot_volume.cpp#L66) | [讲解](DEVICES_IO_FUNCTIONS.md#bootvolume-与块抽象逐函数表)（讲义第190行） | [S(b)](images/storage-io.png) |

### kernel/syscall/syscall.cpp

60 个定义；已讲解 60 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `user_network_owner` | [L23](../../kernel/syscall/syscall.cpp#L23) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第59行） | [S(b)](images/syscall-boundary.png)；[U(f)](images/elf-user-abi.png) |
| `is_space_char` | [L35](../../kernel/syscall/syscall.cpp#L35) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第60行） | [S(d)](images/syscall-boundary.png) |
| `is_path_separator` | [L39](../../kernel/syscall/syscall.cpp#L39) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第61行） | [S(d)](images/syscall-boundary.png) |
| `skip_spaces` | [L43](../../kernel/syscall/syscall.cpp#L43) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第62行） | [S(d)](images/syscall-boundary.png) |
| `string_length` | [L55](../../kernel/syscall/syscall.cpp#L55) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第63行） | [S(d)](images/syscall-boundary.png) |
| `trim_trailing_spaces` | [L68](../../kernel/syscall/syscall.cpp#L68) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第64行） | [S(d)](images/syscall-boundary.png) |
| `copy_string` | [L76](../../kernel/syscall/syscall.cpp#L76) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第65行） | [S(d)](images/syscall-boundary.png) |
| `set_root_path` | [L93](../../kernel/syscall/syscall.cpp#L93) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第66行） | [S(d)](images/syscall-boundary.png) |
| `skip_path_separators` | [L103](../../kernel/syscall/syscall.cpp#L103) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第67行） | [S(d)](images/syscall-boundary.png) |
| `path_component_length` | [L111](../../kernel/syscall/syscall.cpp#L111) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第68行） | [S(d)](images/syscall-boundary.png) |
| `path_component_is_dot` | [L121](../../kernel/syscall/syscall.cpp#L121) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第69行） | [S(d)](images/syscall-boundary.png) |
| `path_component_is_dot_dot` | [L125](../../kernel/syscall/syscall.cpp#L125) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第70行） | [S(d)](images/syscall-boundary.png) |
| `append_path_component` | [L132](../../kernel/syscall/syscall.cpp#L132) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第71行） | [S(d)](images/syscall-boundary.png) |
| `pop_path_component` | [L163](../../kernel/syscall/syscall.cpp#L163) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第72行） | [S(d)](images/syscall-boundary.png) |
| `syscall_fd_is_open` | [L194](../../kernel/syscall/syscall.cpp#L194) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第73行） | [S(d)](images/syscall-boundary.png) |
| `table_fd_to_syscall_fd` | [L199](../../kernel/syscall/syscall.cpp#L199) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第74行） | [S(d)](images/syscall-boundary.png) |
| `syscall_fd_to_table_fd` | [L203](../../kernel/syscall/syscall.cpp#L203) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第75行） | [S(d)](images/syscall-boundary.png) |
| `syscall_write_handler_is_ready` | [L207](../../kernel/syscall/syscall.cpp#L207) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第76行） | [S(f)](images/syscall-boundary.png) |
| `encode_syscall_result` | [L211](../../kernel/syscall/syscall.cpp#L211) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第77行） | [S(a)、S(f)](images/syscall-boundary.png) |
| `syscall_status_result` | [L218](../../kernel/syscall/syscall.cpp#L218) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第78行） | [S(f)](images/syscall-boundary.png) |
| `read_stdin_stream` | [L222](../../kernel/syscall/syscall.cpp#L222) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第79行） | [S(e)](images/syscall-boundary.png) |
| `current_dispatch_context` | [L270](../../kernel/syscall/syscall.cpp#L270) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第80行） | [S(b)](images/syscall-boundary.png) |
| `frame_came_from_user_mode` | [L283](../../kernel/syscall/syscall.cpp#L283) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第81行） | [S(b)](images/syscall-boundary.png) |
| `user_range_valid` | [L287](../../kernel/syscall/syscall.cpp#L287) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第82行） | [S(c)](images/syscall-boundary.png) |
| `user_path_valid` | [L296](../../kernel/syscall/syscall.cpp#L296) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第83行） | [S(c)](images/syscall-boundary.png) |
| `user_syscall_arguments_valid` | [L308](../../kernel/syscall/syscall.cpp#L308) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第84行） | [S(c)](images/syscall-boundary.png) |
| `capture_current_user_trap_frame` | [L390](../../kernel/syscall/syscall.cpp#L390) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第85行） | [S(b)](images/syscall-boundary.png) |
| `dispatch_syscall_registers` | [L432](../../kernel/syscall/syscall.cpp#L432) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第86行） | [S(a)、S(e)、S(f)](images/syscall-boundary.png) |
| `stat_path_internal` | [L600](../../kernel/syscall/syscall.cpp#L600) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第87行） | [S(d)](images/syscall-boundary.png) |
| `initialize_syscall_context` | [L633](../../kernel/syscall/syscall.cpp#L633) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第88行） | [S(b)](images/syscall-boundary.png) |
| `syscall_context_is_ready` | [L651](../../kernel/syscall/syscall.cpp#L651) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第89行） | [S(b)](images/syscall-boundary.png) |
| `install_syscall_write_handler` | [L657](../../kernel/syscall/syscall.cpp#L657) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第90行） | [S(f)](images/syscall-boundary.png) |
| `install_syscall_dispatch_context` | [L669](../../kernel/syscall/syscall.cpp#L669) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第91行） | [S(b)](images/syscall-boundary.png) |
| `syscall_dispatch_is_ready` | [L680](../../kernel/syscall/syscall.cpp#L680) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第92行） | [S(b)](images/syscall-boundary.png) |
| `syscall_current_working_directory` | [L684](../../kernel/syscall/syscall.cpp#L684) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第93行） | [S(d)](images/syscall-boundary.png) |
| `syscall_resolve_path` | [L693](../../kernel/syscall/syscall.cpp#L693) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第94行） | [S(d)](images/syscall-boundary.png) |
| `sys_getcwd` | [L766](../../kernel/syscall/syscall.cpp#L766) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第95行） | [S(d)](images/syscall-boundary.png) |
| `sys_chdir` | [L781](../../kernel/syscall/syscall.cpp#L781) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第96行） | [S(d)](images/syscall-boundary.png) |
| `sys_open` | [L807](../../kernel/syscall/syscall.cpp#L807) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第97行） | [S(d)](images/syscall-boundary.png) |
| `sys_read` | [L851](../../kernel/syscall/syscall.cpp#L851) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第98行） | [S(c)、S(e)](images/syscall-boundary.png) |
| `sys_write` | [L884](../../kernel/syscall/syscall.cpp#L884) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第99行） | [S(e)、S(f)](images/syscall-boundary.png) |
| `sys_stat_path` | [L923](../../kernel/syscall/syscall.cpp#L923) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第100行） | [S(d)](images/syscall-boundary.png) |
| `sys_listdir` | [L928](../../kernel/syscall/syscall.cpp#L928) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第101行） | [S(d)](images/syscall-boundary.png) |
| `sys_close` | [L986](../../kernel/syscall/syscall.cpp#L986) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第102行） | [S(e)](images/syscall-boundary.png) |
| `sys_seek` | [L999](../../kernel/syscall/syscall.cpp#L999) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第103行） | [S(d)](images/syscall-boundary.png) |
| `sys_stat` | [L1014](../../kernel/syscall/syscall.cpp#L1014) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第104行） | [S(d)](images/syscall-boundary.png) |
| `install_syscall_process_services` | [L1030](../../kernel/syscall/syscall.cpp#L1030) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第105行） | [S(e)](images/syscall-boundary.png) |
| `sys_spawn` | [L1045](../../kernel/syscall/syscall.cpp#L1045) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第106行） | [U(d)](images/elf-user-abi.png)；[S(e)](images/syscall-boundary.png) |
| `sys_waitpid` | [L1095](../../kernel/syscall/syscall.cpp#L1095) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第107行） | [S(e)](images/syscall-boundary.png) |
| `sys_mkdir` | [L1113](../../kernel/syscall/syscall.cpp#L1113) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第108行） | [S(d)](images/syscall-boundary.png) |
| `sys_unlink` | [L1122](../../kernel/syscall/syscall.cpp#L1122) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第109行） | [S(d)](images/syscall-boundary.png) |
| `sys_sync` | [L1165](../../kernel/syscall/syscall.cpp#L1165) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第110行） | [S(f)](images/syscall-boundary.png) |
| `sys_read_log` | [L1173](../../kernel/syscall/syscall.cpp#L1173) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第111行） | [S(f)](images/syscall-boundary.png) |
| `sys_performance_snapshot` | [L1177](../../kernel/syscall/syscall.cpp#L1177) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第112行） | [S(f)](images/syscall-boundary.png) |
| `sys_replace_file` | [L1184](../../kernel/syscall/syscall.cpp#L1184) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第113行） | [S(d)、S(f)](images/syscall-boundary.png) |
| `sys_pipe` | [L1197](../../kernel/syscall/syscall.cpp#L1197) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第114行） | [S(e)](images/syscall-boundary.png) |
| `sys_dup2` | [L1205](../../kernel/syscall/syscall.cpp#L1205) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第115行） | [S(e)](images/syscall-boundary.png) |
| `sys_dup` | [L1210](../../kernel/syscall/syscall.cpp#L1210) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第116行） | [S(e)](images/syscall-boundary.png) |
| `sys_brk` | [L1218](../../kernel/syscall/syscall.cpp#L1218) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第117行） | [U(e)](images/elf-user-abi.png)；[H(e)](images/heap-memory.png) |
| `kernel_handle_syscall` | [L1273](../../kernel/syscall/syscall.cpp#L1273) | [讲解](USER_ABI_FUNCTIONS.md#kernelsyscallsyscallcpp)（讲义第118行） | [S(a)、S(b)、S(c)](images/syscall-boundary.png) |

### kernel/task/elf_loader.cpp

7 个定义；已讲解 7 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `StagingBuffer::~StagingBuffer` | [L23](../../kernel/task/elf_loader.cpp#L23) | [讲解](USER_ABI_FUNCTIONS.md#kerneltaskelf_loadercpp)（讲义第128行） | [U(a)](images/elf-user-abi.png) |
| `align_down` | [L28](../../kernel/task/elf_loader.cpp#L28) | [讲解](USER_ABI_FUNCTIONS.md#kerneltaskelf_loadercpp)（讲义第129行） | [U(b)](images/elf-user-abi.png) |
| `align_up` | [L36](../../kernel/task/elf_loader.cpp#L36) | [讲解](USER_ABI_FUNCTIONS.md#kerneltaskelf_loadercpp)（讲义第130行） | [U(b)](images/elf-user-abi.png) |
| `elf_header_is_valid` | [L44](../../kernel/task/elf_loader.cpp#L44) | [讲解](USER_ABI_FUNCTIONS.md#kerneltaskelf_loadercpp)（讲义第131行） | [U(a)](images/elf-user-abi.png) |
| `loadable_segment_is_valid` | [L86](../../kernel/task/elf_loader.cpp#L86) | [讲解](USER_ABI_FUNCTIONS.md#kerneltaskelf_loadercpp)（讲义第132行） | [U(b)](images/elf-user-abi.png) |
| `entry_belongs_to_any_loadable_segment` | [L133](../../kernel/task/elf_loader.cpp#L133) | [讲解](USER_ABI_FUNCTIONS.md#kerneltaskelf_loadercpp)（讲义第133行） | [U(b)](images/elf-user-abi.png) |
| `load_elf_user_program` | [L163](../../kernel/task/elf_loader.cpp#L163) | [讲解](USER_ABI_FUNCTIONS.md#kerneltaskelf_loadercpp)（讲义第134行） | [U(a)、U(b)、U(c)](images/elf-user-abi.png) |

### kernel/task/scheduler.cpp

88 个定义；已讲解 88 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `LocalIrqGuard::LocalIrqGuard` | [L34](../../kernel/task/scheduler.cpp#L34) | [讲解](SCHEDULER_FUNCTIONS.md#localirqguardlocalirqguard--cc) | [C(c)](images/context-switch.png) |
| `LocalIrqGuard::~LocalIrqGuard` | [L35](../../kernel/task/scheduler.cpp#L35) | [讲解](SCHEDULER_FUNCTIONS.md#localirqguardlocalirqguard--cc-1) | [C(c)](images/context-switch.png) |
| `local_cpu` | [L39](../../kernel/task/scheduler.cpp#L39) | [讲解](SCHEDULER_FUNCTIONS.md#local_cpu--ca) | [C(a)](images/context-switch.png) |
| `local_current` | [L43](../../kernel/task/scheduler.cpp#L43) | [讲解](SCHEDULER_FUNCTIONS.md#local_current--ca) | [C(a)](images/context-switch.png) |
| `local_idle` | [L47](../../kernel/task/scheduler.cpp#L47) | [讲解](SCHEDULER_FUNCTIONS.md#local_idle--cae) | [C(a)、C(e)](images/context-switch.png) |
| `local_slice` | [L51](../../kernel/task/scheduler.cpp#L51) | [讲解](SCHEDULER_FUNCTIONS.md#local_slice--cawb) | [C(a)](images/context-switch.png)；[W(b)](images/sleep-wakeup.png) |
| `local_preempt` | [L55](../../kernel/task/scheduler.cpp#L55) | [讲解](SCHEDULER_FUNCTIONS.md#local_preempt--care) | [C(a)](images/context-switch.png)；[R(e)](images/ready-queue.png) |
| `local_bootstrap_stack` | [L59](../../kernel/task/scheduler.cpp#L59) | [讲解](SCHEDULER_FUNCTIONS.md#local_bootstrap_stack--caf) | [C(a)、C(f)](images/context-switch.png) |
| `local_bootstrap_fp` | [L63](../../kernel/task/scheduler.cpp#L63) | [讲解](SCHEDULER_FUNCTIONS.md#local_bootstrap_fp--caf) | [C(a)、C(f)](images/context-switch.png) |
| `relative_block_deadline` | [L69](../../kernel/task/scheduler.cpp#L69) | [讲解](SCHEDULER_FUNCTIONS.md#relative_block_deadline--wc) | [W(c)](images/sleep-wakeup.png) |
| `process_is_current` | [L77](../../kernel/task/scheduler.cpp#L77) | [讲解](SCHEDULER_FUNCTIONS.md#process_is_current--pe) | [P(e)](images/process-reaping.png) |
| `least_loaded_user_cpu` | [L84](../../kernel/task/scheduler.cpp#L84) | [讲解](SCHEDULER_FUNCTIONS.md#least_loaded_user_cpu--rb) | [R(b)](images/ready-queue.png) |
| `thread_eligible` | [L99](../../kernel/task/scheduler.cpp#L99) | [讲解](SCHEDULER_FUNCTIONS.md#thread_eligible--rab) | [R(a)、R(b)](images/ready-queue.png) |
| `has_local_ready` | [L105](../../kernel/task/scheduler.cpp#L105) | [讲解](SCHEDULER_FUNCTIONS.md#has_local_ready--ra) | [R(a)](images/ready-queue.png) |
| `notify_ready_cpu` | [L111](../../kernel/task/scheduler.cpp#L111) | [讲解](SCHEDULER_FUNCTIONS.md#notify_ready_cpu--re) | [R(e)](images/ready-queue.png) |
| `align_down` | [L128](../../kernel/task/scheduler.cpp#L128) | [讲解](SCHEDULER_FUNCTIONS.md#align_down--cb) | [C(b)](images/context-switch.png) |
| `copy_name` | [L136](../../kernel/task/scheduler.cpp#L136) | [讲解](SCHEDULER_FUNCTIONS.md#copy_name--pa) | [P(a)](images/process-reaping.png) |
| `thread_slot_index` | [L156](../../kernel/task/scheduler.cpp#L156) | [讲解](SCHEDULER_FUNCTIONS.md#thread_slot_index--re) | [R(e)](images/ready-queue.png) |
| `is_runnable_priority` | [L173](../../kernel/task/scheduler.cpp#L173) | [讲解](SCHEDULER_FUNCTIONS.md#is_runnable_priority--ref) | [R(e)、R(f)](images/ready-queue.png) |
| `is_user_thread_ready_to_enter` | [L179](../../kernel/task/scheduler.cpp#L179) | [讲解](SCHEDULER_FUNCTIONS.md#is_user_thread_ready_to_enter--cd) | [C(d)](images/context-switch.png) |
| `scheduler_kernel_root_physical` | [L191](../../kernel/task/scheduler.cpp#L191) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_kernel_root_physical--cf) | [C(f)](images/context-switch.png) |
| `thread_resume_root_physical` | [L204](../../kernel/task/scheduler.cpp#L204) | [讲解](SCHEDULER_FUNCTIONS.md#thread_resume_root_physical--ccf) | [C(c)、C(f)](images/context-switch.png) |
| `initialize_thread_saved_root` | [L217](../../kernel/task/scheduler.cpp#L217) | [讲解](SCHEDULER_FUNCTIONS.md#initialize_thread_saved_root--cbf) | [C(b)、C(f)](images/context-switch.png) |
| `push_ready_thread` | [L227](../../kernel/task/scheduler.cpp#L227) | [讲解](SCHEDULER_FUNCTIONS.md#push_ready_thread--re) | [R(e)](images/ready-queue.png) |
| `pop_raw_ready_thread` | [L259](../../kernel/task/scheduler.cpp#L259) | [讲解](SCHEDULER_FUNCTIONS.md#pop_raw_ready_thread--rcdpe) | [R(c)、R(d)](images/ready-queue.png)；[P(e)](images/process-reaping.png) |
| `pop_ready_thread_from_priority` | [L273](../../kernel/task/scheduler.cpp#L273) | [讲解](SCHEDULER_FUNCTIONS.md#pop_ready_thread_from_priority--rd) | [R(d)](images/ready-queue.png) |
| `pop_highest_ready_thread` | [L296](../../kernel/task/scheduler.cpp#L296) | [讲解](SCHEDULER_FUNCTIONS.md#pop_highest_ready_thread--rf) | [R(f)](images/ready-queue.png) |
| `first_free_process_slot` | [L315](../../kernel/task/scheduler.cpp#L315) | [讲解](SCHEDULER_FUNCTIONS.md#first_free_process_slot--pa) | [P(a)](images/process-reaping.png) |
| `first_free_thread_slot` | [L329](../../kernel/task/scheduler.cpp#L329) | [讲解](SCHEDULER_FUNCTIONS.md#first_free_thread_slot--pa) | [P(a)](images/process-reaping.png) |
| `prepare_initial_thread_stack` | [L343](../../kernel/task/scheduler.cpp#L343) | [讲解](SCHEDULER_FUNCTIONS.md#prepare_initial_thread_stack--cb) | [C(b)](images/context-switch.png) |
| `mark_thread_running` | [L374](../../kernel/task/scheduler.cpp#L374) | [讲解](SCHEDULER_FUNCTIONS.md#mark_thread_running--ccd) | [C(c)、C(d)](images/context-switch.png) |
| `switch_thread_context` | [L414](../../kernel/task/scheduler.cpp#L414) | [讲解](SCHEDULER_FUNCTIONS.md#switch_thread_context--cc) | [C(c)](images/context-switch.png) |
| `switch_from_bootstrap_to_thread` | [L443](../../kernel/task/scheduler.cpp#L443) | [讲解](SCHEDULER_FUNCTIONS.md#switch_from_bootstrap_to_thread--cbf) | [C(b)、C(f)](images/context-switch.png) |
| `switch_from_thread_to_bootstrap` | [L462](../../kernel/task/scheduler.cpp#L462) | [讲解](SCHEDULER_FUNCTIONS.md#switch_from_thread_to_bootstrap--cfpb) | [C(f)](images/context-switch.png)；[P(b)](images/process-reaping.png) |
| `update_owner_ready_state` | [L478](../../kernel/task/scheduler.cpp#L478) | [讲解](SCHEDULER_FUNCTIONS.md#update_owner_ready_state--wa) | [W(a)](images/sleep-wakeup.png) |
| `select_next_runnable_thread` | [L489](../../kernel/task/scheduler.cpp#L489) | [讲解](SCHEDULER_FUNCTIONS.md#select_next_runnable_thread--rf) | [R(f)](images/ready-queue.png) |
| `wake_thread_internal` | [L510](../../kernel/task/scheduler.cpp#L510) | [讲解](SCHEDULER_FUNCTIONS.md#wake_thread_internal--we) | [W(e)](images/sleep-wakeup.png) |
| `wake_sleeping_threads` | [L553](../../kernel/task/scheduler.cpp#L553) | [讲解](SCHEDULER_FUNCTIONS.md#wake_sleeping_threads--we) | [W(e)](images/sleep-wakeup.png) |
| `idle_thread_entry` | [L570](../../kernel/task/scheduler.cpp#L570) | [讲解](SCHEDULER_FUNCTIONS.md#idle_thread_entry--ce) | [C(e)](images/context-switch.png) |
| `active_scheduler` | [L584](../../kernel/task/scheduler.cpp#L584) | [讲解](SCHEDULER_FUNCTIONS.md#active_scheduler--ca) | [C(a)](images/context-switch.png) |
| `release_thread` | [L588](../../kernel/task/scheduler.cpp#L588) | [讲解](SCHEDULER_FUNCTIONS.md#release_thread--pe) | [P(e)](images/process-reaping.png) |
| `release_process` | [L598](../../kernel/task/scheduler.cpp#L598) | [讲解](SCHEDULER_FUNCTIONS.md#release_process--pe) | [P(e)](images/process-reaping.png) |
| `reap_orphans` | [L607](../../kernel/task/scheduler.cpp#L607) | [讲解](SCHEDULER_FUNCTIONS.md#reap_orphans--pf) | [P(f)](images/process-reaping.png) |
| `scheduler_active_state` | [L621](../../kernel/task/scheduler.cpp#L621) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_active_state--ca) | [C(a)](images/context-switch.png) |
| `scheduler_find_process` | [L625](../../kernel/task/scheduler.cpp#L625) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_find_process--pd) | [P(d)](images/process-reaping.png) |
| `scheduler_discard_process` | [L637](../../kernel/task/scheduler.cpp#L637) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_discard_process--pae) | [P(a)、P(e)](images/process-reaping.png) |
| `scheduler_reap_process` | [L673](../../kernel/task/scheduler.cpp#L673) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_reap_process--pde) | [P(d)、P(e)](images/process-reaping.png) |
| `scheduler_destroy` | [L690](../../kernel/task/scheduler.cpp#L690) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_destroy--pf) | [P(f)](images/process-reaping.png) |
| `scheduler_prepare_user_arguments` | [L714](../../kernel/task/scheduler.cpp#L714) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_prepare_user_arguments--pa) | [P(a)](images/process-reaping.png) |
| `scheduler_user_range_valid` | [L771](../../kernel/task/scheduler.cpp#L771) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_user_range_valid--pacd) | [P(a)](images/process-reaping.png)；[C(d)](images/context-switch.png) |
| `scheduler_wait_process` | [L779](../../kernel/task/scheduler.cpp#L779) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_wait_process--pdwd) | [P(d)](images/process-reaping.png)；[W(d)](images/sleep-wakeup.png) |
| `scheduler_exit_current_user_process` | [L823](../../kernel/task/scheduler.cpp#L823) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_exit_current_user_process--pb) | [P(b)](images/process-reaping.png) |
| `scheduler_handle_user_exception` | [L839](../../kernel/task/scheduler.cpp#L839) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_handle_user_exception--pc) | [P(c)](images/process-reaping.png) |
| `initialize_scheduler` | [L851](../../kernel/task/scheduler.cpp#L851) | [讲解](SCHEDULER_FUNCTIONS.md#initialize_scheduler--cabpa) | [C(a)、C(b)](images/context-switch.png)；[P(a)](images/process-reaping.png) |
| `scheduler_is_ready` | [L912](../../kernel/task/scheduler.cpp#L912) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_is_ready--ca) | [C(a)](images/context-switch.png) |
| `scheduler_set_active` | [L916](../../kernel/task/scheduler.cpp#L916) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_set_active--capf) | [C(a)](images/context-switch.png)；[P(f)](images/process-reaping.png) |
| `scheduler_cpu_current_thread` | [L928](../../kernel/task/scheduler.cpp#L928) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_cpu_current_thread--cape) | [C(a)](images/context-switch.png)；[P(e)](images/process-reaping.png) |
| `scheduler_prepare_smp` | [L933](../../kernel/task/scheduler.cpp#L933) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_prepare_smp--cab) | [C(a)、C(b)](images/context-switch.png) |
| `scheduler_run_secondary_cpu` | [L972](../../kernel/task/scheduler.cpp#L972) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_run_secondary_cpu--caef) | [C(a)、C(e)、C(f)](images/context-switch.png) |
| `scheduler_create_kernel_process` | [L987](../../kernel/task/scheduler.cpp#L987) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_create_kernel_process--pa) | [P(a)](images/process-reaping.png) |
| `scheduler_create_user_process` | [L1015](../../kernel/task/scheduler.cpp#L1015) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_create_user_process--pa) | [P(a)](images/process-reaping.png) |
| `scheduler_initialize_process_syscall_view` | [L1055](../../kernel/task/scheduler.cpp#L1055) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_initialize_process_syscall_view--pa) | [P(a)](images/process-reaping.png) |
| `scheduler_create_user_elf_thread` | [L1081](../../kernel/task/scheduler.cpp#L1081) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_create_user_elf_thread--pa) | [P(a)](images/process-reaping.png) |
| `scheduler_create_kernel_thread` | [L1186](../../kernel/task/scheduler.cpp#L1186) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_create_kernel_thread--pacb) | [P(a)](images/process-reaping.png)；[C(b)](images/context-switch.png) |
| `scheduler_create_user_thread` | [L1255](../../kernel/task/scheduler.cpp#L1255) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_create_user_thread--pacd) | [P(a)](images/process-reaping.png)；[C(d)](images/context-switch.png) |
| `scheduler_run_until_idle` | [L1335](../../kernel/task/scheduler.cpp#L1335) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_run_until_idle--cfpf) | [C(f)](images/context-switch.png)；[P(f)](images/process-reaping.png) |
| `scheduler_yield_current_thread` | [L1355](../../kernel/task/scheduler.cpp#L1355) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_yield_current_thread--ware) | [W(a)](images/sleep-wakeup.png)；[R(e)](images/ready-queue.png) |
| `scheduler_yield_if_requested` | [L1411](../../kernel/task/scheduler.cpp#L1411) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_yield_if_requested--ware) | [W(a)](images/sleep-wakeup.png)；[R(e)](images/ready-queue.png) |
| `scheduler_sleep_current_thread` | [L1424](../../kernel/task/scheduler.cpp#L1424) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_sleep_current_thread--waf) | [W(a)、W(f)](images/sleep-wakeup.png) |
| `block_current_thread_internal` | [L1462](../../kernel/task/scheduler.cpp#L1462) | [讲解](SCHEDULER_FUNCTIONS.md#block_current_thread_internal--wdf) | [W(d)、W(f)](images/sleep-wakeup.png) |
| `scheduler_block_current_thread` | [L1490](../../kernel/task/scheduler.cpp#L1490) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_block_current_thread--wd) | [W(d)](images/sleep-wakeup.png) |
| `scheduler_block_current_thread_and_enable_interrupts` | [L1493](../../kernel/task/scheduler.cpp#L1493) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_block_current_thread_and_enable_interrupts--wd) | [W(d)](images/sleep-wakeup.png) |
| `scheduler_block_current_thread_until` | [L1496](../../kernel/task/scheduler.cpp#L1496) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_block_current_thread_until--wcd) | [W(c)、W(d)](images/sleep-wakeup.png) |
| `scheduler_wake_thread` | [L1500](../../kernel/task/scheduler.cpp#L1500) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_wake_thread--we) | [W(e)](images/sleep-wakeup.png) |
| `scheduler_exit_current_thread` | [L1511](../../kernel/task/scheduler.cpp#L1511) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_exit_current_thread--pbc) | [P(b)、P(c)](images/process-reaping.png) |
| `scheduler_handle_timer_tick` | [L1579](../../kernel/task/scheduler.cpp#L1579) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_handle_timer_tick--wbe) | [W(b)、W(e)](images/sleep-wakeup.png) |
| `scheduler_handle_local_timer_tick` | [L1586](../../kernel/task/scheduler.cpp#L1586) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_handle_local_timer_tick--wb) | [W(b)](images/sleep-wakeup.png) |
| `scheduler_current_thread` | [L1608](../../kernel/task/scheduler.cpp#L1608) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_current_thread--ca) | [C(a)](images/context-switch.png) |
| `scheduler_active_thread` | [L1617](../../kernel/task/scheduler.cpp#L1617) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_active_thread--ca) | [C(a)](images/context-switch.png) |
| `scheduler_ready_thread_count` | [L1621](../../kernel/task/scheduler.cpp#L1621) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_ready_thread_count--rce) | [R(c)、R(e)](images/ready-queue.png) |
| `scheduler_live_thread_count` | [L1629](../../kernel/task/scheduler.cpp#L1629) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_live_thread_count--pb) | [P(b)](images/process-reaping.png) |
| `scheduler_sleeping_thread_count` | [L1637](../../kernel/task/scheduler.cpp#L1637) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_sleeping_thread_count--wa) | [W(a)](images/sleep-wakeup.png) |
| `scheduler_blocked_thread_count` | [L1645](../../kernel/task/scheduler.cpp#L1645) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_blocked_thread_count--wa) | [W(a)](images/sleep-wakeup.png) |
| `scheduler_process_state_name` | [L1653](../../kernel/task/scheduler.cpp#L1653) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_process_state_name--pb) | [P(b)](images/process-reaping.png) |
| `scheduler_thread_state_name` | [L1668](../../kernel/task/scheduler.cpp#L1668) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_thread_state_name--wa) | [W(a)](images/sleep-wakeup.png) |
| `scheduler_thread_priority_name` | [L1687](../../kernel/task/scheduler.cpp#L1687) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_thread_priority_name--ref) | [R(e)、R(f)](images/ready-queue.png) |
| `run_current_user_thread` | [L1702](../../kernel/task/scheduler.cpp#L1702) | [讲解](SCHEDULER_FUNCTIONS.md#run_current_user_thread--cdepb) | [C(d)、C(e)](images/context-switch.png)；[P(b)](images/process-reaping.png) |
| `scheduler_thread_bootstrap` | [L1720](../../kernel/task/scheduler.cpp#L1720) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_thread_bootstrap--cbepb) | [C(b)、C(e)](images/context-switch.png)；[P(b)](images/process-reaping.png) |

### user/bench_workload.hpp

3 个定义；已讲解 3 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `bench_compute` | [L8](../../user/bench_workload.hpp#L8) | [讲解](COOPERATION_FUNCTIONS.md#bench_workloadbench_compute) | [M(c)、M(f)](images/parallel-workers.png) |
| `bench_expected` | [L14](../../user/bench_workload.hpp#L14) | [讲解](COOPERATION_FUNCTIONS.md#bench_workloadbench_expected) | [M(d)、M(f)](images/parallel-workers.png) |
| `bench_checksum` | [L24](../../user/bench_workload.hpp#L24) | [讲解](COOPERATION_FUNCTIONS.md#bench_workloadbench_checksum) | [M(f)](images/parallel-workers.png) |

### user/memory.cpp

10 个定义；已讲解 10 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `memcpy` | [L4](../../user/memory.cpp#L4) | [讲解](USER_ABI_FUNCTIONS.md#usermemorycpp)（讲义第144行） | [H(e)](images/heap-memory.png) |
| `memmove` | [L10](../../user/memory.cpp#L10) | [讲解](USER_ABI_FUNCTIONS.md#usermemorycpp)（讲义第145行） | [H(e)](images/heap-memory.png) |
| `memset` | [L21](../../user/memory.cpp#L21) | [讲解](USER_ABI_FUNCTIONS.md#usermemorycpp)（讲义第146行） | [H(e)](images/heap-memory.png) |
| `memcmp` | [L26](../../user/memory.cpp#L26) | [讲解](USER_ABI_FUNCTIONS.md#usermemorycpp)（讲义第147行） | [H(e)](images/heap-memory.png) |
| `memory::round_size` | [L48](../../user/memory.cpp#L48) | [讲解](USER_ABI_FUNCTIONS.md#usermemorycpp)（讲义第148行） | [H(e)](images/heap-memory.png)；[U(e)](images/elf-user-abi.png) |
| `memory::split` | [L54](../../user/memory.cpp#L54) | [讲解](USER_ABI_FUNCTIONS.md#usermemorycpp)（讲义第149行） | [H(e)](images/heap-memory.png) |
| `memory::merge_next` | [L69](../../user/memory.cpp#L69) | [讲解](USER_ABI_FUNCTIONS.md#usermemorycpp)（讲义第150行） | [H(e)](images/heap-memory.png) |
| `memory::allocate` | [L79](../../user/memory.cpp#L79) | [讲解](USER_ABI_FUNCTIONS.md#usermemorycpp)（讲义第151行） | [H(e)](images/heap-memory.png)；[U(e)](images/elf-user-abi.png) |
| `memory::release` | [L106](../../user/memory.cpp#L106) | [讲解](USER_ABI_FUNCTIONS.md#usermemorycpp)（讲义第152行） | [H(e)](images/heap-memory.png)；[U(e)](images/elf-user-abi.png) |
| `memory::resize` | [L128](../../user/memory.cpp#L128) | [讲解](USER_ABI_FUNCTIONS.md#usermemorycpp)（讲义第153行） | [H(e)](images/heap-memory.png) |

### user/os64.hpp

24 个定义；已讲解 24 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `syscall` | [L8](../../user/os64.hpp#L8) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第159行） | [S(a)](images/syscall-boundary.png) |
| `length` | [L16](../../user/os64.hpp#L16) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第160行） | [U(f)](images/elf-user-abi.png) |
| `write` | [L17](../../user/os64.hpp#L17) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第161行） | [S(f)](images/syscall-boundary.png) |
| `print` | [L20](../../user/os64.hpp#L20) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第162行） | [U(f)](images/elf-user-abi.png) |
| `error` | [L21](../../user/os64.hpp#L21) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第163行） | [U(f)](images/elf-user-abi.png) |
| `number` | [L22](../../user/os64.hpp#L22) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第164行） | [U(f)](images/elf-user-abi.png) |
| `parse_number` | [L27](../../user/os64.hpp#L27) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第165行） | [U(f)](images/elf-user-abi.png) |
| `open` | [L30](../../user/os64.hpp#L30) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第166行） | [S(d)](images/syscall-boundary.png) |
| `read` | [L33](../../user/os64.hpp#L33) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第167行） | [S(e)](images/syscall-boundary.png) |
| `close` | [L36](../../user/os64.hpp#L36) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第168行） | [S(e)](images/syscall-boundary.png) |
| `pipe` | [L37](../../user/os64.hpp#L37) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第169行） | [S(e)](images/syscall-boundary.png) |
| `dup2` | [L40](../../user/os64.hpp#L40) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第170行） | [S(e)](images/syscall-boundary.png) |
| `dup` | [L41](../../user/os64.hpp#L41) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第171行） | [S(e)](images/syscall-boundary.png) |
| `yield` | [L43](../../user/os64.hpp#L43) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第172行） | [S(e)](images/syscall-boundary.png) |
| `sleep` | [L44](../../user/os64.hpp#L44) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第173行） | [S(e)](images/syscall-boundary.png) |
| `sync` | [L45](../../user/os64.hpp#L45) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第174行） | [S(f)](images/syscall-boundary.png) |
| `ticks` | [L46](../../user/os64.hpp#L46) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第175行） | [S(f)](images/syscall-boundary.png) |
| `read_log` | [L47](../../user/os64.hpp#L47) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第176行） | [S(f)](images/syscall-boundary.png) |
| `perf_snapshot` | [L50](../../user/os64.hpp#L50) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第177行） | [S(f)](images/syscall-boundary.png) |
| `brk` | [L55](../../user/os64.hpp#L55) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第178行） | [U(e)](images/elf-user-abi.png) |
| `replace_file` | [L60](../../user/os64.hpp#L60) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第179行） | [S(d)、S(f)](images/syscall-boundary.png) |
| `spawn` | [L64](../../user/os64.hpp#L64) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第180行） | [U(d)](images/elf-user-abi.png) |
| `waitpid` | [L67](../../user/os64.hpp#L67) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第181行） | [S(e)](images/syscall-boundary.png) |
| `listdir` | [L75](../../user/os64.hpp#L75) | [讲解](USER_ABI_FUNCTIONS.md#useros64hpp)（讲义第182行） | [S(d)](images/syscall-boundary.png) |

### user/programs/badptr.cpp

6 个定义；已讲解 6 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `rejected` | [L14](../../user/programs/badptr.cpp#L14) | [讲解](USER_PROGRAM_FUNCTIONS.md#badptrrejected) | [U(f)](images/user-tools.png) |
| `ipc_boundaries` | [L20](../../user/programs/badptr.cpp#L20) | [讲解](USER_PROGRAM_FUNCTIONS.md#badptripc_boundaries) | [U(f)、U(b)](images/user-tools.png) |
| `performance_boundaries` | [L47](../../user/programs/badptr.cpp#L47) | [讲解](USER_PROGRAM_FUNCTIONS.md#badptrperformance_boundaries) | [U(f)](images/user-tools.png) |
| `udp_boundaries` | [L66](../../user/programs/badptr.cpp#L66) | [讲解](USER_PROGRAM_FUNCTIONS.md#badptrudp_boundaries) | [U(f)、U(e)](images/user-tools.png) |
| `smp_boundaries` | [L121](../../user/programs/badptr.cpp#L121) | [讲解](USER_PROGRAM_FUNCTIONS.md#badptrsmp_boundaries) | [U(f)](images/user-tools.png) |
| `main` | [L136](../../user/programs/badptr.cpp#L136) | [讲解](USER_PROGRAM_FUNCTIONS.md#badptrmain) | [U(f)](images/user-tools.png) |

### user/programs/bench.cpp

6 个定义；已讲解 6 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `equal` | [L9](../../user/programs/bench.cpp#L9) | [讲解](USER_PROGRAM_FUNCTIONS.md#benchequal) | [U(a)](images/user-tools.png) |
| `integer` | [L13](../../user/programs/bench.cpp#L13) | [讲解](USER_PROGRAM_FUNCTIONS.md#benchinteger) | [U(d)](images/user-tools.png) |
| `decimal` | [L22](../../user/programs/bench.cpp#L22) | [讲解](USER_PROGRAM_FUNCTIONS.md#benchdecimal) | [U(d)](images/user-tools.png) |
| `compute` | [L27](../../user/programs/bench.cpp#L27) | [讲解](USER_PROGRAM_FUNCTIONS.md#benchcompute) | [U(d)](images/user-tools.png) |
| `field` | [L36](../../user/programs/bench.cpp#L36) | [讲解](USER_PROGRAM_FUNCTIONS.md#benchfield) | [U(d)](images/user-tools.png) |
| `main` | [L38](../../user/programs/bench.cpp#L38) | [讲解](USER_PROGRAM_FUNCTIONS.md#benchmain) | [U(d)](images/user-tools.png) |

### user/programs/bench_ipc.cpp

6 个定义；已讲解 6 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `equal` | [L7](../../user/programs/bench_ipc.cpp#L7) | [讲解](USER_PROGRAM_FUNCTIONS.md#bench_ipcequal) | [U(a)](images/user-tools.png) |
| `integer` | [L8](../../user/programs/bench_ipc.cpp#L8) | [讲解](USER_PROGRAM_FUNCTIONS.md#bench_ipcinteger) | [U(d)](images/user-tools.png) |
| `decimal` | [L14](../../user/programs/bench_ipc.cpp#L14) | [讲解](USER_PROGRAM_FUNCTIONS.md#bench_ipcdecimal) | [U(d)](images/user-tools.png) |
| `field` | [L19](../../user/programs/bench_ipc.cpp#L19) | [讲解](USER_PROGRAM_FUNCTIONS.md#bench_ipcfield) | [U(d)](images/user-tools.png) |
| `reader` | [L20](../../user/programs/bench_ipc.cpp#L20) | [讲解](USER_PROGRAM_FUNCTIONS.md#bench_ipcreader) | [U(b)、U(d)](images/user-tools.png) |
| `main` | [L32](../../user/programs/bench_ipc.cpp#L32) | [讲解](USER_PROGRAM_FUNCTIONS.md#bench_ipcmain) | [U(b)、U(d)](images/user-tools.png) |

### user/programs/cat.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L3](../../user/programs/cat.cpp#L3) | [讲解](USER_PROGRAM_FUNCTIONS.md#catmain) | [U(b)](images/user-tools.png) |

### user/programs/coop_test.cpp

12 个定义；已讲解 12 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `equal` | [L20](../../user/programs/coop_test.cpp#L20) | [讲解](COOPERATION_FUNCTIONS.md#coop_testequal) | [M(e)](images/parallel-workers.png) |
| `integer` | [L24](../../user/programs/coop_test.cpp#L24) | [讲解](COOPERATION_FUNCTIONS.md#coop_testinteger) | [M(e)](images/parallel-workers.png) |
| `decimal` | [L35](../../user/programs/coop_test.cpp#L35) | [讲解](COOPERATION_FUNCTIONS.md#coop_testdecimal) | [M(e)](images/parallel-workers.png) |
| `field` | [L42](../../user/programs/coop_test.cpp#L42) | [讲解](COOPERATION_FUNCTIONS.md#coop_testfield) | [M(e)](images/parallel-workers.png) |
| `record_size` | [L45](../../user/programs/coop_test.cpp#L45) | [讲解](COOPERATION_FUNCTIONS.md#coop_testrecord_size) | [M(e)](images/parallel-workers.png)；[P(c)](images/pipe-cooperation.png) |
| `payload_byte` | [L48](../../user/programs/coop_test.cpp#L48) | [讲解](COOPERATION_FUNCTIONS.md#coop_testpayload_byte) | [M(e)](images/parallel-workers.png) |
| `worker` | [L53](../../user/programs/coop_test.cpp#L53) | [讲解](COOPERATION_FUNCTIONS.md#coop_testworker) | [M(c)、M(e)](images/parallel-workers.png)；[P(b)、P(e)](images/pipe-cooperation.png) |
| `blocked_writer` | [L86](../../user/programs/coop_test.cpp#L86) | [讲解](COOPERATION_FUNCTIONS.md#coop_testblocked_writer) | [P(e)、P(f)](images/pipe-cooperation.png) |
| `close_wakes_blocked_writer` | [L95](../../user/programs/coop_test.cpp#L95) | [讲解](COOPERATION_FUNCTIONS.md#coop_testclose_wakes_blocked_writer) | [P(f)](images/pipe-cooperation.png)；[M(e)](images/parallel-workers.png) |
| `check_record` | [L134](../../user/programs/coop_test.cpp#L134) | [讲解](COOPERATION_FUNCTIONS.md#coop_testcheck_record) | [M(e)](images/parallel-workers.png) |
| `cooperate` | [L151](../../user/programs/coop_test.cpp#L151) | [讲解](COOPERATION_FUNCTIONS.md#coop_testcooperate) | [M(a)、M(e)](images/parallel-workers.png)；[P(a)、P(f)](images/pipe-cooperation.png) |
| `main` | [L233](../../user/programs/coop_test.cpp#L233) | [讲解](COOPERATION_FUNCTIONS.md#coop_testmain) | [M(e)](images/parallel-workers.png) |

### user/programs/echo.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/echo.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#echomain) | [U(a)](images/user-tools.png) |

### user/programs/edit.cpp

16 个定义；已讲解 16 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `Buffer::reserve` | [L13](../../user/programs/edit.cpp#L13) | [讲解](USER_PROGRAM_FUNCTIONS.md#editbufferreserve) | [E(a)](images/user-editor.png) |
| `Buffer::clear` | [L23](../../user/programs/edit.cpp#L23) | [讲解](USER_PROGRAM_FUNCTIONS.md#editbufferclear) | [E(b)](images/user-editor.png) |
| `Buffer::destroy` | [L24](../../user/programs/edit.cpp#L24) | [讲解](USER_PROGRAM_FUNCTIONS.md#editbufferdestroy) | [E(f)](images/user-editor.png) |
| `equal` | [L27](../../user/programs/edit.cpp#L27) | [讲解](USER_PROGRAM_FUNCTIONS.md#editequal) | [U(a)](images/user-tools.png) |
| `begins` | [L31](../../user/programs/edit.cpp#L31) | [讲解](USER_PROGRAM_FUNCTIONS.md#editbegins) | [E(b)](images/user-editor.png) |
| `command_line` | [L37](../../user/programs/edit.cpp#L37) | [讲解](USER_PROGRAM_FUNCTIONS.md#editcommand_line) | [E(b)](images/user-editor.png) |
| `load` | [L70](../../user/programs/edit.cpp#L70) | [讲解](USER_PROGRAM_FUNCTIONS.md#editload) | [E(a)](images/user-editor.png) |
| `line_count` | [L108](../../user/programs/edit.cpp#L108) | [讲解](USER_PROGRAM_FUNCTIONS.md#editline_count) | [E(c)](images/user-editor.png) |
| `line_start` | [L114](../../user/programs/edit.cpp#L114) | [讲解](USER_PROGRAM_FUNCTIONS.md#editline_start) | [E(c)](images/user-editor.png) |
| `show` | [L121](../../user/programs/edit.cpp#L121) | [讲解](USER_PROGRAM_FUNCTIONS.md#editshow) | [E(c)](images/user-editor.png) |
| `insert_line` | [L134](../../user/programs/edit.cpp#L134) | [讲解](USER_PROGRAM_FUNCTIONS.md#editinsert_line) | [E(d)](images/user-editor.png) |
| `delete_line` | [L156](../../user/programs/edit.cpp#L156) | [讲解](USER_PROGRAM_FUNCTIONS.md#editdelete_line) | [E(d)](images/user-editor.png) |
| `line_argument` | [L170](../../user/programs/edit.cpp#L170) | [讲解](USER_PROGRAM_FUNCTIONS.md#editline_argument) | [E(b)、E(d)](images/user-editor.png) |
| `help` | [L184](../../user/programs/edit.cpp#L184) | [讲解](USER_PROGRAM_FUNCTIONS.md#edithelp) | [E(b)](images/user-editor.png) |
| `save` | [L198](../../user/programs/edit.cpp#L198) | [讲解](USER_PROGRAM_FUNCTIONS.md#editsave) | [E(e)](images/user-editor.png) |
| `main` | [L213](../../user/programs/edit.cpp#L213) | [讲解](USER_PROGRAM_FUNCTIONS.md#editmain) | [E(a)、E(b)、E(c)、E(d)、E(e)、E(f)](images/user-editor.png) |

### user/programs/false.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L1](../../user/programs/false.cpp#L1) | [讲解](USER_PROGRAM_FUNCTIONS.md#falsemain) | [U(a)](images/user-tools.png) |

### user/programs/fault.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/fault.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#faultmain) | [U(f)](images/user-tools.png) |

### user/programs/fp_test.cpp

3 个定义；已讲解 3 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `equal` | [L4](../../user/programs/fp_test.cpp#L4) | [讲解](USER_PROGRAM_FUNCTIONS.md#fp_testequal) | [U(a)](images/user-tools.png) |
| `worker` | [L7](../../user/programs/fp_test.cpp#L7) | [讲解](USER_PROGRAM_FUNCTIONS.md#fp_testworker) | [U(d)、U(f)](images/user-tools.png) |
| `main` | [L46](../../user/programs/fp_test.cpp#L46) | [讲解](USER_PROGRAM_FUNCTIONS.md#fp_testmain) | [U(d)、U(f)](images/user-tools.png) |

### user/programs/fs_test.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L3](../../user/programs/fs_test.cpp#L3) | [讲解](USER_PROGRAM_FUNCTIONS.md#fs_testmain) | [U(c)、U(f)](images/user-tools.png) |

### user/programs/hello.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/hello.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#hellomain) | [U(a)](images/user-tools.png) |

### user/programs/ls.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L3](../../user/programs/ls.cpp#L3) | [讲解](USER_PROGRAM_FUNCTIONS.md#lsmain) | [U(c)](images/user-tools.png) |

### user/programs/mem_test.cpp

6 个定义；已讲解 6 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `make_fixture` | [L9](../../user/programs/mem_test.cpp#L9) | [讲解](USER_PROGRAM_FUNCTIONS.md#mem_testmake_fixture) | [U(f)](images/user-tools.png) |
| `raw_break_test` | [L17](../../user/programs/mem_test.cpp#L17) | [讲解](USER_PROGRAM_FUNCTIONS.md#mem_testraw_break_test) | [U(f)](images/user-tools.png) |
| `allocator_test` | [L39](../../user/programs/mem_test.cpp#L39) | [讲解](USER_PROGRAM_FUNCTIONS.md#mem_testallocator_test) | [U(f)](images/user-tools.png) |
| `large_stack_test` | [L81](../../user/programs/mem_test.cpp#L81) | [讲解](USER_PROGRAM_FUNCTIONS.md#mem_testlarge_stack_test) | [U(f)](images/user-tools.png) |
| `seed_size` | [L89](../../user/programs/mem_test.cpp#L89) | [讲解](USER_PROGRAM_FUNCTIONS.md#mem_testseed_size) | [U(f)、U(c)](images/user-tools.png) |
| `main` | [L103](../../user/programs/mem_test.cpp#L103) | [讲解](USER_PROGRAM_FUNCTIONS.md#mem_testmain) | [U(f)、U(c)](images/user-tools.png) |

### user/programs/mkdir.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/mkdir.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#mkdirmain) | [U(c)](images/user-tools.png) |

### user/programs/nxfault.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L4](../../user/programs/nxfault.cpp#L4) | [讲解](USER_PROGRAM_FUNCTIONS.md#nxfaultmain) | [U(f)](images/user-tools.png) |

### user/programs/parallel_reduce.cpp

12 个定义；已讲解 12 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `equal` | [L22](../../user/programs/parallel_reduce.cpp#L22) | [讲解](PARALLEL_REDUCTION.md#equala-b--ab) | [A(b)](images/parallel-reduction.png) |
| `parse_bounded` | [L26](../../user/programs/parallel_reduce.cpp#L26) | [讲解](PARALLEL_REDUCTION.md#parse_boundedtext-limit-result--ae) | [A(e)](images/parallel-reduction.png) |
| `decimal` | [L38](../../user/programs/parallel_reduce.cpp#L38) | [讲解](PARALLEL_REDUCTION.md#decimalvalue-output--ab) | [A(b)](images/parallel-reduction.png) |
| `field` | [L45](../../user/programs/parallel_reduce.cpp#L45) | [讲解](PARALLEL_REDUCTION.md#fieldlabel-value--af) | [A(f)](images/parallel-reduction.png) |
| `read_message` | [L48](../../user/programs/parallel_reduce.cpp#L48) | [讲解](PARALLEL_REDUCTION.md#read_messagefd-message--adae) | [A(d)、A(e)](images/parallel-reduction.png) |
| `partition_task` | [L59](../../user/programs/parallel_reduce.cpp#L59) | [讲解](PARALLEL_REDUCTION.md#partition_taskjob-jobs-items--aa) | [A(a)](images/parallel-reduction.png) |
| `map_range` | [L62](../../user/programs/parallel_reduce.cpp#L62) | [讲解](PARALLEL_REDUCTION.md#map_rangebegin-end--ac) | [A(c)](images/parallel-reduction.png) |
| `expected_range` | [L68](../../user/programs/parallel_reduce.cpp#L68) | [讲解](PARALLEL_REDUCTION.md#expected_rangebegin-end--ad) | [A(d)](images/parallel-reduction.png) |
| `close_ordinary_except` | [L78](../../user/programs/parallel_reduce.cpp#L78) | [讲解](PARALLEL_REDUCTION.md#close_ordinary_exceptfirst-second--abae) | [A(b)、A(e)](images/parallel-reduction.png) |
| `worker` | [L83](../../user/programs/parallel_reduce.cpp#L83) | [讲解](PARALLEL_REDUCTION.md#workerid-task_read-result_write--abacae) | [A(b)、A(c)、A(e)](images/parallel-reduction.png) |
| `coordinator` | [L111](../../user/programs/parallel_reduce.cpp#L111) | [讲解](PARALLEL_REDUCTION.md#coordinatorpath-workers-jobs-items--aaabadaeaf) | [A(a)、A(b)、A(d)、A(e)、A(f)](images/parallel-reduction.png) |
| `main` | [L188](../../user/programs/parallel_reduce.cpp#L188) | [讲解](PARALLEL_REDUCTION.md#mainargc-argv--abae) | [A(b)、A(e)](images/parallel-reduction.png) |

### user/programs/perf_test.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/perf_test.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#perf_testmain) | [U(f)、U(d)](images/user-tools.png) |

### user/programs/pipe_test.cpp

7 个定义；已讲解 7 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `equal` | [L3](../../user/programs/pipe_test.cpp#L3) | [讲解](USER_PROGRAM_FUNCTIONS.md#pipe_testequal) | [U(a)](images/user-tools.png) |
| `decimal` | [L4](../../user/programs/pipe_test.cpp#L4) | [讲解](USER_PROGRAM_FUNCTIONS.md#pipe_testdecimal) | [U(b)](images/user-tools.png) |
| `write_records` | [L12](../../user/programs/pipe_test.cpp#L12) | [讲解](USER_PROGRAM_FUNCTIONS.md#pipe_testwrite_records) | [U(b)](images/user-tools.png) |
| `child_reader` | [L22](../../user/programs/pipe_test.cpp#L22) | [讲解](USER_PROGRAM_FUNCTIONS.md#pipe_testchild_reader) | [U(b)](images/user-tools.png) |
| `spawn_child` | [L33](../../user/programs/pipe_test.cpp#L33) | [讲解](USER_PROGRAM_FUNCTIONS.md#pipe_testspawn_child) | [U(b)](images/user-tools.png) |
| `await` | [L38](../../user/programs/pipe_test.cpp#L38) | [讲解](USER_PROGRAM_FUNCTIONS.md#pipe_testawait) | [U(b)](images/user-tools.png) |
| `main` | [L40](../../user/programs/pipe_test.cpp#L40) | [讲解](USER_PROGRAM_FUNCTIONS.md#pipe_testmain) | [U(b)、U(f)](images/user-tools.png) |

### user/programs/pwd.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L3](../../user/programs/pwd.cpp#L3) | [讲解](USER_PROGRAM_FUNCTIONS.md#pwdmain) | [U(a)、U(c)](images/user-tools.png) |

### user/programs/rm.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/rm.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#rmmain) | [U(c)](images/user-tools.png) |

### user/programs/sched_test.cpp

4 个定义；已讲解 4 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `equal` | [L7](../../user/programs/sched_test.cpp#L7) | [讲解](USER_PROGRAM_FUNCTIONS.md#sched_testequal) | [U(a)](images/user-tools.png) |
| `decimal` | [L8](../../user/programs/sched_test.cpp#L8) | [讲解](USER_PROGRAM_FUNCTIONS.md#sched_testdecimal) | [U(d)](images/user-tools.png) |
| `worker` | [L13](../../user/programs/sched_test.cpp#L13) | [讲解](USER_PROGRAM_FUNCTIONS.md#sched_testworker) | [U(d)、U(b)](images/user-tools.png) |
| `main` | [L27](../../user/programs/sched_test.cpp#L27) | [讲解](USER_PROGRAM_FUNCTIONS.md#sched_testmain) | [U(d)、U(b)](images/user-tools.png) |

### user/programs/sleep.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/sleep.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#sleepmain) | [U(d)](images/user-tools.png) |

### user/programs/smp_test.cpp

5 个定义；已讲解 5 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `equal` | [L10](../../user/programs/smp_test.cpp#L10) | [讲解](COOPERATION_FUNCTIONS.md#smp_testequal) | [M(d)](images/parallel-workers.png) |
| `decimal` | [L11](../../user/programs/smp_test.cpp#L11) | [讲解](COOPERATION_FUNCTIONS.md#smp_testdecimal) | [M(d)](images/parallel-workers.png) |
| `field` | [L16](../../user/programs/smp_test.cpp#L16) | [讲解](COOPERATION_FUNCTIONS.md#smp_testfield) | [M(d)](images/parallel-workers.png) |
| `worker` | [L17](../../user/programs/smp_test.cpp#L17) | [讲解](COOPERATION_FUNCTIONS.md#smp_testworker) | [M(b)、M(d)](images/parallel-workers.png) |
| `main` | [L42](../../user/programs/smp_test.cpp#L42) | [讲解](COOPERATION_FUNCTIONS.md#smp_testmain) | [M(a)、M(b)、M(d)](images/parallel-workers.png) |

### user/programs/spawn_test.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/spawn_test.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#spawn_testmain) | [U(b)](images/user-tools.png) |

### user/programs/spin.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/spin.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#spinmain) | [U(d)](images/user-tools.png) |

### user/programs/stackfault.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/stackfault.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#stackfaultmain) | [U(f)](images/user-tools.png) |

### user/programs/sync.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/sync.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#syncmain) | [U(c)](images/user-tools.png) |

### user/programs/true.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L1](../../user/programs/true.cpp#L1) | [讲解](USER_PROGRAM_FUNCTIONS.md#truemain) | [U(a)](images/user-tools.png) |

### user/programs/ud2.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/ud2.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#ud2main) | [U(f)](images/user-tools.png) |

### user/programs/udp_mixed.cpp

7 个定义；已讲解 7 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `equal` | [L6](../../user/programs/udp_mixed.cpp#L6) | [讲解](COOPERATION_FUNCTIONS.md#udp_mixedequal) | [M(f)](images/parallel-workers.png) |
| `integer` | [L7](../../user/programs/udp_mixed.cpp#L7) | [讲解](COOPERATION_FUNCTIONS.md#udp_mixedinteger) | [M(f)](images/parallel-workers.png) |
| `ipv4` | [L17](../../user/programs/udp_mixed.cpp#L17) | [讲解](COOPERATION_FUNCTIONS.md#udp_mixedipv4) | [M(f)](images/parallel-workers.png) |
| `decimal` | [L26](../../user/programs/udp_mixed.cpp#L26) | [讲解](COOPERATION_FUNCTIONS.md#udp_mixeddecimal) | [M(f)](images/parallel-workers.png) |
| `field` | [L30](../../user/programs/udp_mixed.cpp#L30) | [讲解](COOPERATION_FUNCTIONS.md#udp_mixedfield) | [M(f)](images/parallel-workers.png) |
| `receiver` | [L31](../../user/programs/udp_mixed.cpp#L31) | [讲解](COOPERATION_FUNCTIONS.md#udp_mixedreceiver) | [M(f)](images/parallel-workers.png)；[U(a)、U(f)](images/udp-wait.png) |
| `main` | [L62](../../user/programs/udp_mixed.cpp#L62) | [讲解](COOPERATION_FUNCTIONS.md#udp_mixedmain) | [M(a)、M(c)、M(f)](images/parallel-workers.png) |

### user/programs/udp_test.cpp

5 个定义；已讲解 5 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `equal` | [L4](../../user/programs/udp_test.cpp#L4) | [讲解](USER_PROGRAM_FUNCTIONS.md#udp_testequal) | [U(a)](images/user-tools.png) |
| `decimal` | [L5](../../user/programs/udp_test.cpp#L5) | [讲解](USER_PROGRAM_FUNCTIONS.md#udp_testdecimal) | [U(e)](images/user-tools.png) |
| `ipv4` | [L11](../../user/programs/udp_test.cpp#L11) | [讲解](USER_PROGRAM_FUNCTIONS.md#udp_testipv4) | [U(e)](images/user-tools.png) |
| `format_handle` | [L20](../../user/programs/udp_test.cpp#L20) | [讲解](USER_PROGRAM_FUNCTIONS.md#udp_testformat_handle) | [U(e)](images/user-tools.png) |
| `main` | [L25](../../user/programs/udp_test.cpp#L25) | [讲解](USER_PROGRAM_FUNCTIONS.md#udp_testmain) | [U(e)、U(f)](images/user-tools.png) |

### user/programs/wc.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L2](../../user/programs/wc.cpp#L2) | [讲解](USER_PROGRAM_FUNCTIONS.md#wcmain) | [U(b)](images/user-tools.png) |

### user/programs/writer.cpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `main` | [L3](../../user/programs/writer.cpp#L3) | [讲解](USER_PROGRAM_FUNCTIONS.md#writermain) | [U(c)](images/user-tools.png) |

### user/smp.hpp

1 个定义；已讲解 1 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `smp_snapshot` | [L14](../../user/smp.hpp#L14) | [讲解](USER_ABI_FUNCTIONS.md#usersmphpp)（讲义第188行） | [U(f)](images/elf-user-abi.png)；[S(f)](images/syscall-boundary.png) |

### user/udp.hpp

5 个定义；已讲解 5 个。

| 函数 | 源码起始行 | 讲义定位 | 图块 |
| --- | ---: | --- | --- |
| `udp_open` | [L17](../../user/udp.hpp#L17) | [讲解](USER_ABI_FUNCTIONS.md#userudphpp)（讲义第194行） | [U(f)](images/elf-user-abi.png) |
| `udp_close` | [L18](../../user/udp.hpp#L18) | [讲解](USER_ABI_FUNCTIONS.md#userudphpp)（讲义第195行） | [U(f)](images/elf-user-abi.png) |
| `udp_send` | [L19](../../user/udp.hpp#L19) | [讲解](USER_ABI_FUNCTIONS.md#userudphpp)（讲义第196行） | [S(a)](images/syscall-boundary.png)；[U(f)](images/elf-user-abi.png) |
| `udp_receive` | [L22](../../user/udp.hpp#L22) | [讲解](USER_ABI_FUNCTIONS.md#userudphpp)（讲义第197行） | [U(f)](images/elf-user-abi.png) |
| `udp_receive_wait` | [L28](../../user/udp.hpp#L28) | [讲解](USER_ABI_FUNCTIONS.md#userudphpp)（讲义第198行） | [S(e)](images/syscall-boundary.png)；[U(f)](images/elf-user-abi.png) |

## 汇编入口与自检区间（不计入 C++ 总数）

下面列出已有逐入口解释的启动桥、切换入口与用户自检代码区间。内部数据标签、区间末尾标签和宏展开出的中断 stub 不作为普通 C++ 函数计数；其它启动/入口汇编仍待后续逐入口图解。

| 源码入口或区间 | 源码行 | 讲义 | 图块 |
| --- | ---: | --- | --- |
| `smp_trampoline_start` | [kernel/cpu/ap_start.asm:8](../../kernel/cpu/ap_start.asm#L8) | [讲解](SMP_BOOT_FUNCTIONS.md#ap_startasm没有-c-栈之前的桥) | B(c) |
| `ap_protected` | [kernel/cpu/ap_start.asm:21](../../kernel/cpu/ap_start.asm#L21) | [讲解](SMP_BOOT_FUNCTIONS.md#ap_startasm没有-c-栈之前的桥) | B(c) |
| `ap_long` | [kernel/cpu/ap_start.asm:40](../../kernel/cpu/ap_start.asm#L40) | [讲解](SMP_BOOT_FUNCTIONS.md#ap_startasm没有-c-栈之前的桥) | B(c) |
| `.halt` | [kernel/cpu/ap_start.asm:51](../../kernel/cpu/ap_start.asm#L51) | [讲解](SMP_BOOT_FUNCTIONS.md#ap_startasm没有-c-栈之前的桥) | B(c) |
| `scheduler_switch_context` | [kernel/task/context_switch.asm:29](../../kernel/task/context_switch.asm#L29) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_switch_context汇编-cc) | C(c) |
| `scheduler_switch_context_and_root` | [kernel/task/context_switch.asm:65](../../kernel/task/context_switch.asm#L65) | [讲解](SCHEDULER_FUNCTIONS.md#scheduler_switch_context_and_root汇编-ccf) | C(c)、C(f) |
| `user_mode_enter` | [kernel/task/context_switch.asm:137](../../kernel/task/context_switch.asm#L137) | [讲解](SCHEDULER_FUNCTIONS.md#user_mode_enter汇编-cde) | C(d)、C(e) |
| `user_mode_resume_kernel` | [kernel/task/context_switch.asm:181](../../kernel/task/context_switch.asm#L181) | [讲解](SCHEDULER_FUNCTIONS.md#user_mode_resume_kernel汇编-cdpb) | C(d)、P(b) |
| `user_mode_smoke_program_start … user_mode_smoke_program_end` | [kernel/task/context_switch.asm:214](../../kernel/task/context_switch.asm#L214) | [讲解](SCHEDULER_FUNCTIONS.md#user_mode_smoke_program_start--user_mode_smoke_program_end--cdpab) | C(d)、P(a)、P(b) |
| `user_mode_yield_program_start … user_mode_yield_program_end` | [kernel/task/context_switch.asm:365](../../kernel/task/context_switch.asm#L365) | [讲解](SCHEDULER_FUNCTIONS.md#user_mode_yield_program_start--user_mode_yield_program_end--ccwad) | C(c)、W(a)、W(d) |
