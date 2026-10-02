#ifndef OS64_XAPIC_HPP
#define OS64_XAPIC_HPP
#include <stdint.h>
// xAPIC physical destination 0xff means broadcast, never a single AP.
constexpr bool xapic_unicast_id_valid(uint32_t id) { return id < 0xff; }
// This implementation maps one shared LAPIC address below 4 GiB.
// Compare every address bit, not only bits 31:12, and reject x2APIC first.
constexpr bool xapic_base_supported(uint64_t msr, uint64_t physical) {
  return physical != 0 && physical < 0x100000000ULL && (physical & 4095) == 0 &&
         (msr & (1ULL << 10)) == 0 && (msr & ~uint64_t(4095)) == physical;
}
#endif
