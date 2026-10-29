/*
 * 原子操作。
 *
 * 为什么不用 <stdatomic.h>：
 *   - 交叉工具链（ESP-IDF / host gcc / clang）对 _Atomic 类型与某些操作的支持不一致，
 *     尤其在 -march 未开 A 扩展的 RISC-V 或旧 newlib 上会出现编译期/链接期差异。
 *   - __atomic_* 内建函数在所有目标上行为一致，且能明确表达内存序。
 *   - 结构体包装（而不是裸整数）是为了让"这是原子变量"在代码里一眼可见，
 *     防止有人对普通 int 做 ++ 然后以为它是原子的。
 *
 * 内存序：默认给 ACQ_REL / SEQ_CST，只有明确证明不需要时才降级。
 *         单核 MCU 上 acquire/release 几乎免费；双核 / DMA 场景下它们是真需要的。
 *
 * ISR 安全：本文件所有操作都不阻塞、不关中断，可在 ISR 中调用。
 */
#ifndef CM_ATOMIC_H
#define CM_ATOMIC_H

#include "cm_types.h"

/* 内存序，映射到 GCC/Clang 的 __ATOMIC_* */
#define CM_MO_RELAXED __ATOMIC_RELAXED
#define CM_MO_ACQUIRE __ATOMIC_ACQUIRE
#define CM_MO_RELEASE __ATOMIC_RELEASE
#define CM_MO_ACQ_REL __ATOMIC_ACQ_REL
#define CM_MO_SEQ_CST __ATOMIC_SEQ_CST

/* ---------------- 32 位有符号 ---------------- */

typedef struct { volatile int32_t v; } cm_atomic_i32;

static inline void cm_atomic_i32_init(cm_atomic_i32 *a, int32_t v)
{
    __atomic_store_n(&a->v, v, CM_MO_RELAXED);
}

static inline int32_t cm_atomic_i32_load(const cm_atomic_i32 *a, int mo)
{
    return __atomic_load_n(&a->v, mo);
}

static inline void cm_atomic_i32_store(cm_atomic_i32 *a, int32_t v, int mo)
{
    __atomic_store_n(&a->v, v, mo);
}

static inline int32_t cm_atomic_i32_fetch_add(cm_atomic_i32 *a, int32_t d)
{
    return __atomic_fetch_add(&a->v, d, CM_MO_SEQ_CST);
}

static inline int32_t cm_atomic_i32_fetch_sub(cm_atomic_i32 *a, int32_t d)
{
    return __atomic_fetch_sub(&a->v, d, CM_MO_SEQ_CST);
}

static inline int32_t cm_atomic_i32_exchange(cm_atomic_i32 *a, int32_t v)
{
    return __atomic_exchange_n(&a->v, v, CM_MO_SEQ_CST);
}

/* 成功返回 true。失败时 *expected 被写入实际值（CAS 的标准语义）。 */
static inline bool cm_atomic_i32_cas(cm_atomic_i32 *a, int32_t *expected, int32_t desired)
{
    return __atomic_compare_exchange_n(&a->v, expected, desired, false,
                                       CM_MO_SEQ_CST, CM_MO_SEQ_CST);
}

/* ---------------- 32 位无符号（环形缓冲索引等） ---------------- */

typedef struct { volatile uint32_t v; } cm_atomic_u32;

static inline void cm_atomic_u32_init(cm_atomic_u32 *a, uint32_t v)
{
    __atomic_store_n(&a->v, v, CM_MO_RELAXED);
}

static inline uint32_t cm_atomic_u32_load(const cm_atomic_u32 *a, int mo)
{
    return __atomic_load_n(&a->v, mo);
}

static inline void cm_atomic_u32_store(cm_atomic_u32 *a, uint32_t v, int mo)
{
    __atomic_store_n(&a->v, v, mo);
}

static inline uint32_t cm_atomic_u32_fetch_add(cm_atomic_u32 *a, uint32_t d)
{
    return __atomic_fetch_add(&a->v, d, CM_MO_SEQ_CST);
}

static inline bool cm_atomic_u32_cas(cm_atomic_u32 *a, uint32_t *expected, uint32_t desired)
{
    return __atomic_compare_exchange_n(&a->v, expected, desired, false,
                                       CM_MO_SEQ_CST, CM_MO_SEQ_CST);
}

/* ---------------- 指针（服务注册表等无锁发布） ---------------- */

typedef struct { void *volatile v; } cm_atomic_ptr;

static inline void cm_atomic_ptr_init(cm_atomic_ptr *a, void *v)
{
    __atomic_store_n(&a->v, v, CM_MO_RELAXED);
}

static inline void *cm_atomic_ptr_load(const cm_atomic_ptr *a, int mo)
{
    return __atomic_load_n(&a->v, mo);
}

static inline void cm_atomic_ptr_store(cm_atomic_ptr *a, void *v, int mo)
{
    __atomic_store_n(&a->v, v, mo);
}

/* 空指针 → v，否则不变。返回是否由本次调用写入（用于"只注册一次"）。 */
static inline bool cm_atomic_ptr_set_if_null(cm_atomic_ptr *a, void *v)
{
    void *expected = NULL;
    return __atomic_compare_exchange_n(&a->v, &expected, v, false,
                                       CM_MO_SEQ_CST, CM_MO_SEQ_CST);
}

#endif /* CM_ATOMIC_H */
