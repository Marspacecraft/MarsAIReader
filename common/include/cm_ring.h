/*
 * 单生产者 / 单消费者 无锁字节环。
 *
 * 用途：ISR→任务、DMA→任务、任务→任务 的字节流，是这套系统里复用最多的原语
 *      （音频块、串口 AT 响应、事件序列化、日志缓冲）。
 *
 * 契约（违反即数据错乱，不是"可能出问题"）：
 *   - 同一个环，只能有一个线程/ISR 调 cm_ring_write，一个调 cm_ring_read。
 *     多生产者或多次消费者请在外层加锁，或改用带原子预留的变体。
 *   - 容量必须是 2 的幂（初始化时校验，不是断言崩溃）。
 *   - 缓冲区由调用者提供（静态数组或 PSRAM 块），本模块不分配内存。
 *
 * 无锁条件：读写下标用 acquire/release 配对，保证"先写数据、后发布下标"。
 * 不阻塞、不关中断，ISR 安全。
 */
#ifndef CM_RING_H
#define CM_RING_H

#include <string.h>

#include "cm_atomic.h"
#include "cm_err.h"
#include "cm_types.h"

typedef struct {
    uint8_t       *buf;
    uint32_t       mask;   /* capacity - 1 */
    cm_atomic_u32  head;   /* 写者推进：已写入的累计字节数 */
    cm_atomic_u32  tail;   /* 读者推进：已读出的累计字节数 */
} cm_ring_t;

/*
 * cap 必须是 2 的幂且 > 0。buf 必须至少 cap 字节。
 * 下标是 32 位自由计数，靠 mask 取模，因此永远不会因回绕出错
 * （只要未读数据量小于 2^32，即永远成立）。
 */
static inline cm_err_t cm_ring_init(cm_ring_t *r, void *buf, uint32_t cap)
{
    if (r == NULL || buf == NULL || cap == 0u || !CM_IS_POW2(cap)) {
        return CM_ERR_INVALID;
    }
    r->buf = (uint8_t *)buf;
    r->mask = cap - 1u;
    cm_atomic_u32_init(&r->head, 0u);
    cm_atomic_u32_init(&r->tail, 0u);
    return CM_OK;
}

/* 可读字节数（消费者视角） */
static inline uint32_t cm_ring_avail(const cm_ring_t *r)
{
    uint32_t head = cm_atomic_u32_load(&r->head, CM_MO_ACQUIRE);
    uint32_t tail = cm_atomic_u32_load(&r->tail, CM_MO_RELAXED);
    return head - tail;
}

/* 可写字节数（生产者视角） */
static inline uint32_t cm_ring_space(const cm_ring_t *r)
{
    return (r->mask + 1u) - cm_ring_avail(r);
}

/*
 * 写入至多 n 字节，返回实际写入数（0 表示环满）。
 * 分段 memcpy 处理回绕，不做逐字节拷贝。
 */
static inline uint32_t cm_ring_write(cm_ring_t *r, const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    uint32_t head = cm_atomic_u32_load(&r->head, CM_MO_RELAXED);
    uint32_t tail = cm_atomic_u32_load(&r->tail, CM_MO_ACQUIRE);
    uint32_t space = (r->mask + 1u) - (head - tail);

    if (n > space) {
        n = space;
    }
    if (n == 0u) {
        return 0u;
    }

    uint32_t idx = head & r->mask;
    uint32_t first = (r->mask + 1u) - idx;   /* 到缓冲区末尾的连续空间 */
    if (first > n) {
        first = n;
    }
    memcpy(&r->buf[idx], s, first);
    if (n > first) {
        memcpy(r->buf, s + first, n - first);
    }

    cm_atomic_u32_store(&r->head, head + n, CM_MO_RELEASE);
    return n;
}

/*
 * 读出至多 n 字节，返回实际读出数。
 * 全部写完数据后再推进 tail，避免消费者提前看到未写完的数据。
 */
static inline uint32_t cm_ring_read(cm_ring_t *r, void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint32_t tail = cm_atomic_u32_load(&r->tail, CM_MO_RELAXED);
    uint32_t head = cm_atomic_u32_load(&r->head, CM_MO_ACQUIRE);
    uint32_t avail = head - tail;

    if (n > avail) {
        n = avail;
    }
    if (n == 0u) {
        return 0u;
    }

    uint32_t idx = tail & r->mask;
    uint32_t first = (r->mask + 1u) - idx;
    if (first > n) {
        first = n;
    }
    memcpy(d, &r->buf[idx], first);
    if (n > first) {
        memcpy(d + first, r->buf, n - first);
    }

    cm_atomic_u32_store(&r->tail, tail + n, CM_MO_RELEASE);
    return n;
}

/* 只看不取：从环里复制 n 字节但不推进 tail。满不足 n 返回复制量。 */
static inline uint32_t cm_ring_peek(const cm_ring_t *r, void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint32_t tail = cm_atomic_u32_load(&r->tail, CM_MO_RELAXED);
    uint32_t head = cm_atomic_u32_load(&r->head, CM_MO_ACQUIRE);
    uint32_t avail = head - tail;

    if (n > avail) {
        n = avail;
    }
    for (uint32_t i = 0; i < n; i++) {
        d[i] = r->buf[(tail + i) & r->mask];
    }
    return n;
}

/* 丢弃 n 字节（消费者视角前进）。返回实际丢弃量。 */
static inline uint32_t cm_ring_discard(cm_ring_t *r, uint32_t n)
{
    uint32_t avail = cm_ring_avail(r);
    if (n > avail) {
        n = avail;
    }
    if (n > 0u) {
        uint32_t tail = cm_atomic_u32_load(&r->tail, CM_MO_RELAXED);
        cm_atomic_u32_store(&r->tail, tail + n, CM_MO_RELEASE);
    }
    return n;
}

/* 清空。只能在生产者与消费者都停住时调用（如 App 退出、错误恢复）。 */
static inline void cm_ring_reset(cm_ring_t *r)
{
    cm_atomic_u32_store(&r->head, 0u, CM_MO_RELEASE);
    cm_atomic_u32_store(&r->tail, 0u, CM_MO_RELEASE);
}

#endif /* CM_RING_H */
