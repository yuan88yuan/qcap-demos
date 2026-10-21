/* Single-width MMIO primitives for the SC6F0 diagnostic reader.
 * No memset/memcpy, Python buffer packing, byte stores or register RMW.
 * Caller validates the mapped address, alignment and allowed command first.
 */
#include <stdint.h>

uint32_t sc6f0_mmio32_abi(void)
{
    return 1;
}

uint32_t sc6f0_mmio32_read(const volatile uint32_t *address)
{
    uint32_t value = *address;
#if defined(__aarch64__)
    __asm__ __volatile__("dmb oshld" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
    return value;
}

void sc6f0_mmio32_write(volatile uint32_t *address, uint32_t value)
{
#if defined(__aarch64__)
    __asm__ __volatile__("dmb oshst" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
    *address = value;
#if defined(__aarch64__)
    __asm__ __volatile__("dsb oshst" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}
