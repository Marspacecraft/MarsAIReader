/*
 * 侵入式双向链表。
 *
 * 为什么侵入式：MCU 上没有通用堆可用，且节点已经嵌在结构体里，
 * 再多一层 malloc 包装只增加碎片和失败路径。
 * 用法：把 cm_list_node_t 放进自己的结构体，用 CM_CONTAINER_OF 取回宿主。
 *
 * 不做任何加锁 —— 加锁策略属于调用者（框架层的逻辑锁）。
 * 本文件所有操作都不分配内存，都不失败（除了对已删除节点再删）。
 *
 * 迭代期间删除节点必须用 cm_list_for_each_safe。
 */
#ifndef CM_LIST_H
#define CM_LIST_H

#include "cm_types.h"

typedef struct cm_list_node {
    struct cm_list_node *next;
    struct cm_list_node *prev;
} cm_list_node_t;

typedef struct {
    cm_list_node_t head;   /* 哨兵节点，不承载数据 */
    unsigned       count;  /* O(1) 求长度；MCU 上比遍历划算 */
} cm_list_t;

/* ---------------- 初始化 ---------------- */

static inline void cm_list_init(cm_list_t *l)
{
    l->head.next = &l->head;
    l->head.prev = &l->head;
    l->count = 0u;
}

static inline void cm_list_node_init(cm_list_node_t *n)
{
    n->next = NULL;
    n->prev = NULL;
}

/* 节点是否在某个链表里。用于调试与"重复删除"防御。 */
static inline bool cm_list_node_linked(const cm_list_node_t *n)
{
    return n->next != NULL;
}

/* ---------------- 插入 ---------------- */

/* 在 pos 之前插入 n */
static inline void cm_list_insert_before(cm_list_node_t *pos, cm_list_node_t *n)
{
    n->prev = pos->prev;
    n->next = pos;
    pos->prev->next = n;
    pos->prev = n;
}

/* 追加到尾部（注意：单链表意义上的"尾"在这里是 head 之前） */
static inline void cm_list_push_back(cm_list_t *l, cm_list_node_t *n)
{
    cm_list_insert_before(&l->head, n);
    l->count++;
}

/* 插到头部 */
static inline void cm_list_push_front(cm_list_t *l, cm_list_node_t *n)
{
    cm_list_insert_before(l->head.next, n);
    l->count++;
}

/* ---------------- 删除 ---------------- */

static inline void cm_list_del(cm_list_node_t *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
    n->next = NULL;
    n->prev = NULL;
}

static inline void cm_list_remove(cm_list_t *l, cm_list_node_t *n)
{
    if (!cm_list_node_linked(n)) {
        return;                 /* 重复删除：静默忽略，不破坏链表 */
    }
    cm_list_del(n);
    if (l->count > 0u) {
        l->count--;
    }
}

/* 弹出头部节点，空链表返回 NULL。 */
static inline cm_list_node_t *cm_list_pop_front(cm_list_t *l)
{
    if (l->count == 0u) {
        return NULL;
    }
    cm_list_node_t *n = l->head.next;
    cm_list_remove(l, n);
    return n;
}

/* ---------------- 查询 ---------------- */

static inline bool cm_list_empty(const cm_list_t *l)
{
    return l->count == 0u;
}

static inline unsigned cm_list_count(const cm_list_t *l)
{
    return l->count;
}

static inline cm_list_node_t *cm_list_front(cm_list_t *l)
{
    return (l->count == 0u) ? NULL : l->head.next;
}

static inline cm_list_node_t *cm_list_back(cm_list_t *l)
{
    return (l->count == 0u) ? NULL : l->head.prev;
}

/* ---------------- 遍历 ---------------- */

/* 取宿主结构体指针 */
#define CM_LIST_ENTRY(node, type, member) \
    CM_CONTAINER_OF(node, type, member)

/* 正向遍历。遍历中不得删除当前节点，要删用 _safe。 */
#define cm_list_for_each(pos, list)                                    \
    for ((pos) = (list)->head.next; (pos) != &(list)->head;            \
         (pos) = (pos)->next)

/* 正向遍历，允许删除当前节点（先取 next 再删）。 */
#define cm_list_for_each_safe(pos, tmp, list)                          \
    for ((pos) = (list)->head.next, (tmp) = (pos)->next;               \
         (pos) != &(list)->head;                                       \
         (pos) = (tmp), (tmp) = (pos)->next)

/* 反向遍历，允许删除。 */
#define cm_list_for_each_prev_safe(pos, tmp, list)                     \
    for ((pos) = (list)->head.prev, (tmp) = (pos)->prev;               \
         (pos) != &(list)->head;                                       \
         (pos) = (tmp), (tmp) = (pos)->prev)

/*
 * 带宿主的遍历：
 *   cm_list_for_each_entry(item, &list, node) { ... }
 */
#define cm_list_for_each_entry(pos, list, member)                      \
    for ((pos) = CM_LIST_ENTRY((list)->head.next, __typeof__(*(pos)), member); \
         &(pos)->member != &(list)->head;                              \
         (pos) = CM_LIST_ENTRY((pos)->member.next, __typeof__(*(pos)), member))

#define cm_list_for_each_entry_safe(pos, tmp, list, member)            \
    for ((pos) = CM_LIST_ENTRY((list)->head.next, __typeof__(*(pos)), member), \
         (tmp) = CM_LIST_ENTRY((pos)->member.next, __typeof__(*(pos)), member); \
         &(pos)->member != &(list)->head;                              \
         (pos) = (tmp),                                                \
         (tmp) = CM_LIST_ENTRY((tmp)->member.next, __typeof__(*(tmp)), member))

#endif /* CM_LIST_H */
