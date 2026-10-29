/*
 * 位运算辅助。
 *
 * 存在的理由：这些函数在驱动、编解码、位图（1bpp 帧缓冲）里反复出现，
 * 手写版本是 off-by-one 的高发区。集中一处、集中测试。
 */
#ifndef CM_BITS_H
#define CM_BITS_H

#include "cm_types.h"

/* 1 的个数 */
static inline unsigned cm_popcount32(uint32_t x)
{
    return (unsigned)__builtin_popcount(x);
}

static inline unsigned cm_popcount64(uint64_t x)
{
    return (unsigned)__builtin_popcountll(x);
}

/* 前导零个数；x == 0 时返回 32（与 __builtin_clz 未定义行为隔离） */
static inline unsigned cm_clz32(uint32_t x)
{
    return (x == 0u) ? 32u : (unsigned)__builtin_clz(x);
}

/* 尾随零个数；x == 0 时返回 32 */
static inline unsigned cm_ctz32(uint32_t x)
{
    return (x == 0u) ? 32u : (unsigned)__builtin_ctz(x);
}

/* 最低置位的位号（0..31）；x == 0 时返回 -1 */
static inline int cm_ffs32(uint32_t x)
{
    return (x == 0u) ? -1 : (int)__builtin_ctz(x);
}

/* 向上取整到 2 的幂；x == 0 返回 1；超过 2^31 返回 0（不可表示） */
static inline uint32_t cm_next_pow2(uint32_t x)
{
    if (x == 0u) {
        return 1u;
    }
    if (x > 0x80000000u) {
        return 0u;
    }
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1u;
}

/* 位图（1bpp 帧缓冲、分配位图）用：对 uint8_t 数组按位读写 */
static inline bool cm_bitmap_get(const uint8_t *bm, uint32_t i)
{
    return (bm[i >> 3] >> (i & 7u)) & 1u;
}

static inline void cm_bitmap_set(uint8_t *bm, uint32_t i)
{
    bm[i >> 3] |= (uint8_t)(1u << (i & 7u));
}

static inline void cm_bitmap_clear(uint8_t *bm, uint32_t i)
{
    bm[i >> 3] &= (uint8_t)~(1u << (i & 7u));
}

/* 第一个置位的位置（从 0 开始），没有则返回 n_bits */
static inline uint32_t cm_bitmap_find_first(const uint8_t *bm, uint32_t n_bits)
{
    uint32_t n_bytes = (n_bits + 7u) >> 3;
    for (uint32_t i = 0; i < n_bytes; i++) {
        uint8_t b = bm[i];
        if (b != 0u) {
            uint32_t bit = (uint32_t)cm_ctz32(b);
            uint32_t pos = i * 8u + bit;
            return (pos < n_bits) ? pos : n_bits;
        }
    }
    return n_bits;
}

#endif /* CM_BITS_H */
