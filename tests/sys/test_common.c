/*
 * common/ 的 host 单元测试。
 *
 * 运行全部：      python tools/run_tests.py
 * 只跑本文件：    python tools/run_tests.py test_common
 * 只跑一个用例：  python tools/run_tests.py test_common list_ops
 * 列出用例：      python tools/run_tests.py test_common --list
 *
 * 不依赖目标 SDK，只用标准 C 编译器。
 */
#include <stdio.h>
#include <string.h>

#include "cm_atomic.h"
#include "cm_bits.h"
#include "cm_crc.h"
#include "cm_err.h"
#include "cm_list.h"
#include "cm_ring.h"
#include "cm_types.h"

#include "tinytest.h"

/* ---------------- 类型与宏 ---------------- */

static void types_macros(void)
{
    TT_CHECK(CM_ARRAY_SIZE(((int[7]){0})) == 7u);
    TT_CHECK(CM_MIN(3, 5) == 3);
    TT_CHECK(CM_MAX(3, 5) == 5);
    TT_CHECK(CM_CLAMP(10, 0, 8) == 8);
    TT_CHECK(CM_CLAMP(-3, 0, 8) == 0);
    TT_CHECK(CM_IS_POW2(1u));
    TT_CHECK(CM_IS_POW2(64u));
    TT_CHECK(!CM_IS_POW2(0u));
    TT_CHECK(!CM_IS_POW2(63u));
    TT_CHECK_EQ_U32(CM_ALIGN_UP(5u, 4u), 8u);
    TT_CHECK_EQ_U32(CM_ALIGN_UP(8u, 4u), 8u);

    /* CONTAINER_OF 往返 */
    struct host { int a; int b; };
    struct host h = { 1, 2 };
    int *pb = &h.b;
    struct host *back = CM_CONTAINER_OF(pb, struct host, b);
    TT_CHECK(back == &h);
    TT_CHECK(back->a == 1);
}

/* ---------------- 错误码 ---------------- */

static void err_codes(void)
{
    TT_CHECK(strcmp(cm_err_str(CM_OK), "OK") == 0);
    TT_CHECK(strcmp(cm_err_str(CM_ERR_NOT_SUPPORTED), "NOT_SUPPORTED") == 0);
    TT_CHECK(strcmp(cm_err_str(CM_ERR_UNAVAILABLE), "UNAVAILABLE") == 0);
    TT_CHECK(strcmp(cm_err_str((cm_err_t)999), "UNKNOWN") == 0);

    /* 每个合法值都必须有名字，不能有一个是 NULL 导致返回 UNKNOWN */
    for (int i = 0; i < CM_ERR_MAX; i++) {
        TT_CHECK(strcmp(cm_err_str((cm_err_t)i), "UNKNOWN") != 0);
    }

    TT_CHECK(cm_err_is_capability(CM_ERR_NOT_SUPPORTED));
    TT_CHECK(cm_err_is_capability(CM_ERR_UNAVAILABLE));
    TT_CHECK(!cm_err_is_capability(CM_ERR_TIMEOUT));

    TT_CHECK(cm_err_is_retryable(CM_ERR_BUSY));
    TT_CHECK(cm_err_is_retryable(CM_ERR_UNAVAILABLE));
    TT_CHECK(!cm_err_is_retryable(CM_ERR_INVALID));
}

/* ---------------- 侵入式链表 ---------------- */

struct item {
    int id;
    cm_list_node_t node;
};

static void list_ops(void)
{
    cm_list_t l;
    struct item items[4];

    cm_list_init(&l);
    TT_CHECK(cm_list_empty(&l));
    TT_CHECK_EQ_U32(cm_list_count(&l), 0u);
    TT_CHECK(cm_list_pop_front(&l) == NULL);

    for (int i = 0; i < 4; i++) {
        items[i].id = i;
        cm_list_node_init(&items[i].node);
        cm_list_push_back(&l, &items[i].node);
    }
    TT_CHECK_EQ_U32(cm_list_count(&l), 4u);
    TT_CHECK(!cm_list_empty(&l));

    /* 顺序应为 0,1,2,3 */
    int expect = 0;
    struct item *it;
    cm_list_for_each_entry(it, &l, node) {
        TT_CHECK(it->id == expect);
        expect++;
    }
    TT_CHECK(expect == 4);

    /* 头插 */
    struct item head_item = { .id = 99 };
    cm_list_node_init(&head_item.node);
    cm_list_push_front(&l, &head_item.node);
    TT_CHECK_EQ_U32(cm_list_count(&l), 5u);
    TT_CHECK(CM_LIST_ENTRY(cm_list_front(&l), struct item, node)->id == 99);
    TT_CHECK(CM_LIST_ENTRY(cm_list_back(&l), struct item, node)->id == 3);

    /* 遍历中删除 id 为偶数的节点 */
    struct item *tmp;
    cm_list_for_each_entry_safe(it, tmp, &l, node) {
        if ((it->id % 2) == 0) {
            cm_list_remove(&l, &it->node);
        }
    }
    TT_CHECK_EQ_U32(cm_list_count(&l), 3u);   /* 剩下 99, 1, 3 */

    /* 删除后节点应处于 unlinking 状态，重复删除不破坏链表 */
    TT_CHECK(!cm_list_node_linked(&items[0].node));
    cm_list_remove(&l, &items[0].node);
    TT_CHECK_EQ_U32(cm_list_count(&l), 3u);

    /* 全部弹出，顺序 99 → 1 → 3 */
    TT_CHECK(CM_LIST_ENTRY(cm_list_pop_front(&l), struct item, node)->id == 99);
    TT_CHECK(CM_LIST_ENTRY(cm_list_pop_front(&l), struct item, node)->id == 1);
    TT_CHECK(CM_LIST_ENTRY(cm_list_pop_front(&l), struct item, node)->id == 3);
    TT_CHECK(cm_list_empty(&l));
    TT_CHECK(cm_list_front(&l) == NULL);
    TT_CHECK(cm_list_back(&l) == NULL);
}

/* ---------------- 无锁环 ---------------- */

static void ring_ops(void)
{
    uint8_t storage[8];
    cm_ring_t r;

    /* 非法参数 */
    TT_CHECK(cm_ring_init(&r, storage, 0) == CM_ERR_INVALID);
    TT_CHECK(cm_ring_init(&r, storage, 6) == CM_ERR_INVALID);   /* 非 2 的幂 */
    TT_CHECK(cm_ring_init(&r, NULL, 8) == CM_ERR_INVALID);
    TT_CHECK(cm_ring_init(&r, storage, 8) == CM_OK);

    TT_CHECK_EQ_U32(cm_ring_avail(&r), 0u);
    TT_CHECK_EQ_U32(cm_ring_space(&r), 8u);

    TT_CHECK_EQ_U32(cm_ring_write(&r, "abcdef", 6), 6u);
    TT_CHECK_EQ_U32(cm_ring_avail(&r), 6u);
    TT_CHECK_EQ_U32(cm_ring_space(&r), 2u);

    /* 满时只写进部分 */
    TT_CHECK_EQ_U32(cm_ring_write(&r, "XYZ", 3), 2u);
    TT_CHECK_EQ_U32(cm_ring_space(&r), 0u);
    TT_CHECK_EQ_U32(cm_ring_write(&r, "Q", 1), 0u);

    char out[16] = {0};
    TT_CHECK_EQ_U32(cm_ring_read(&r, out, 4), 4u);
    TT_CHECK(memcmp(out, "abcd", 4) == 0);

    /* 读走 4 后腾出 4 字节，再写入跨越缓冲区末尾的数据 */
    TT_CHECK_EQ_U32(cm_ring_write(&r, "1234", 4), 4u);

    memset(out, 0, sizeof(out));
    TT_CHECK_EQ_U32(cm_ring_read(&r, out, 8), 8u);
    TT_CHECK(memcmp(out, "efXY1234", 8) == 0);
    TT_CHECK(cm_ring_avail(&r) == 0u);

    /* peek 不推进读指针 */
    TT_CHECK_EQ_U32(cm_ring_write(&r, "hello", 5), 5u);
    memset(out, 0, sizeof(out));
    TT_CHECK_EQ_U32(cm_ring_peek(&r, out, 3), 3u);
    TT_CHECK(memcmp(out, "hel", 3) == 0);
    TT_CHECK_EQ_U32(cm_ring_avail(&r), 5u);

    /* discard 推进读指针 */
    TT_CHECK_EQ_U32(cm_ring_discard(&r, 2), 2u);
    TT_CHECK_EQ_U32(cm_ring_avail(&r), 3u);
    memset(out, 0, sizeof(out));
    TT_CHECK_EQ_U32(cm_ring_read(&r, out, 8), 3u);
    TT_CHECK(memcmp(out, "llo", 3) == 0);

    cm_ring_reset(&r);
    TT_CHECK_EQ_U32(cm_ring_avail(&r), 0u);
    TT_CHECK_EQ_U32(cm_ring_space(&r), 8u);
}

/* ---------------- 原子 ---------------- */

static void atomic_ops(void)
{
    cm_atomic_i32 a;
    cm_atomic_i32_init(&a, 10);
    TT_CHECK(cm_atomic_i32_load(&a, CM_MO_RELAXED) == 10);

    TT_CHECK(cm_atomic_i32_fetch_add(&a, 5) == 10);
    TT_CHECK(cm_atomic_i32_load(&a, CM_MO_RELAXED) == 15);
    TT_CHECK(cm_atomic_i32_fetch_sub(&a, 3) == 15);
    TT_CHECK(cm_atomic_i32_load(&a, CM_MO_RELAXED) == 12);

    int32_t expected = 12;
    TT_CHECK(cm_atomic_i32_cas(&a, &expected, 20));
    TT_CHECK(cm_atomic_i32_load(&a, CM_MO_RELAXED) == 20);

    /* 失败的 CAS 必须回写实际值 */
    expected = 999;
    TT_CHECK(!cm_atomic_i32_cas(&a, &expected, 7));
    TT_CHECK(expected == 20);
    TT_CHECK(cm_atomic_i32_load(&a, CM_MO_RELAXED) == 20);

    TT_CHECK(cm_atomic_i32_exchange(&a, 1) == 20);

    cm_atomic_u32 u;
    cm_atomic_u32_init(&u, 0xFFFFFFFEu);
    TT_CHECK(cm_atomic_u32_fetch_add(&u, 4) == 0xFFFFFFFEu);
    TT_CHECK_EQ_U32(cm_atomic_u32_load(&u, CM_MO_RELAXED), 2u);   /* 回绕正确 */

    /* set_if_null 语义：只有第一次成功（服务注册"只注册一次"依赖它） */
    cm_atomic_ptr p;
    cm_atomic_ptr_init(&p, NULL);
    int dummy1, dummy2;
    TT_CHECK(cm_atomic_ptr_set_if_null(&p, &dummy1));
    TT_CHECK(!cm_atomic_ptr_set_if_null(&p, &dummy2));
    TT_CHECK(cm_atomic_ptr_load(&p, CM_MO_ACQUIRE) == &dummy1);
}

/* ---------------- 位运算 ---------------- */

static void bits_ops(void)
{
    TT_CHECK_EQ_U32(cm_popcount32(0xF0F0u), 8u);
    TT_CHECK_EQ_U32(cm_popcount32(0u), 0u);
    TT_CHECK_EQ_U32(cm_clz32(0u), 32u);
    TT_CHECK_EQ_U32(cm_ctz32(0u), 32u);
    TT_CHECK_EQ_U32(cm_clz32(1u), 31u);
    TT_CHECK_EQ_U32(cm_ctz32(0x80000000u), 31u);
    TT_CHECK(cm_ffs32(0u) == -1);
    TT_CHECK(cm_ffs32(0x30u) == 4);

    TT_CHECK_EQ_U32(cm_next_pow2(0u), 1u);
    TT_CHECK_EQ_U32(cm_next_pow2(1u), 1u);
    TT_CHECK_EQ_U32(cm_next_pow2(5u), 8u);
    TT_CHECK_EQ_U32(cm_next_pow2(8u), 8u);
    TT_CHECK_EQ_U32(cm_next_pow2(0x40000000u), 0x40000000u);

    uint8_t bm[2] = {0};
    TT_CHECK(!cm_bitmap_get(bm, 0));
    cm_bitmap_set(bm, 0);
    cm_bitmap_set(bm, 9);
    TT_CHECK(cm_bitmap_get(bm, 0));
    TT_CHECK(cm_bitmap_get(bm, 9));
    TT_CHECK(!cm_bitmap_get(bm, 1));
    TT_CHECK_EQ_U32(cm_bitmap_find_first(bm, 16u), 0u);
    cm_bitmap_clear(bm, 0);
    TT_CHECK_EQ_U32(cm_bitmap_find_first(bm, 16u), 9u);
    cm_bitmap_clear(bm, 9);
    TT_CHECK_EQ_U32(cm_bitmap_find_first(bm, 16u), 16u);
}

/* ---------------- CRC 标准向量 ---------------- */

static void crc_vectors(void)
{
    const char *v = "123456789";

    /* CRC-16/CCITT-FALSE */
    TT_CHECK_EQ_U32(cm_crc16(0xFFFFu, v, 9), 0x29B1u);
    TT_CHECK_EQ_U32(cm_crc16(0xFFFFu, NULL, 0), 0xFFFFu);

    /* CRC-32/ISO-HDLC（与 zlib.crc32 一致） */
    TT_CHECK_EQ_U32(cm_crc32(0u, v, 9), 0xCBF43926u);
    TT_CHECK_EQ_U32(cm_crc32(0u, NULL, 0), 0u);

    /* 增量与一次性结果必须相同 —— 分块读文件的前提 */
    uint16_t c16 = cm_crc16(0xFFFFu, v, 4);
    c16 = cm_crc16(c16, v + 4, 5);
    TT_CHECK_EQ_U32(c16, cm_crc16(0xFFFFu, v, 9));

    uint32_t c32 = cm_crc32(0u, v, 4);
    c32 = cm_crc32(c32, v + 4, 5);
    TT_CHECK_EQ_U32(c32, cm_crc32(0u, v, 9));

    /* 已知文本的 CRC32（"hello"）*/
    TT_CHECK_EQ_U32(cm_crc32(0u, "hello", 5), 0x3610A686u);
}

static const tt_case_t s_cases[] = {
    TT_CASE(types_macros), TT_CASE(err_codes), TT_CASE(list_ops),
    TT_CASE(ring_ops),     TT_CASE(atomic_ops), TT_CASE(bits_ops),
    TT_CASE(crc_vectors),
};

TT_MAIN(s_cases)
