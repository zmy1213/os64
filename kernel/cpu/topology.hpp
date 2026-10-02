#ifndef OS64_CPU_TOPOLOGY_HPP
#define OS64_CPU_TOPOLOGY_HPP
#include "cpu/smp.hpp"
#include <stddef.h>
#include <stdint.h>
using FirmwareRead = const uint8_t *(*)(uint64_t, size_t, void *);
struct CpuTopology {
  uint32_t count;
  uint8_t apic_ids[kSmpMaxCpuCount];
  uint64_t local_apic_physical;
  bool from_acpi;
};
// Readers enforce physical bounds. A failed parse never publishes half a table.
bool firmware_find_cpu_topology(FirmwareRead read, void *context,
                                uint8_t bsp_apic_id, CpuTopology *output);
#endif
