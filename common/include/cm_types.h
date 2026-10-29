/*
 * 公共基础类型与通用宏。
 *
 * 规则：
 *   - 本文件只依赖 C11 标准库与 cm_port.h（编译器差异全部在 cm_port.h 收敛）。
 *   - 任何层（common / api / lib / framework / apps / platform / tests）都可以 include。
 *   - 不放业务逻辑，不放需要初始化的状态。
 */
#ifndef CM_TYPES_H
#define CM_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cm_port.h"

/* ---------------- 定宽别名 ---------------- */

typedef uint8_t  cm_u8;
typedef uint16_t cm_u16;
typedef uint32_t cm_u32;
typedef uint64_t cm_u64;
typedef int8_t   cm_i8;
typedef int16_t  cm_i16;
typedef int32_t  cm_i32;
typedef int64_t  cm_i64;
typedef size_t   cm_size;
typedef ptrdiff_t cm_ssize;
typedef uintptr_t cm_uintptr;

/* ---------------- 常用宏 ---------------- */

#define CM_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define CM_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CM_MAX(a, b) ((a) > (b) ? (a) : (b))
#define CM_CLAMP(v, lo, hi) (CM_MIN(CM_MAX(v, lo), hi))

/* 从成员指针反推宿主结构体指针 */
#define CM_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* 抑制未使用告警（写清理钩子时常需要） */
#define CM_UNUSED(x) ((void)(x))

#define CM_IS_POW2(x) ((x) != 0u && (((x) & ((x) - 1u)) == 0u))
#define CM_ALIGN_UP(x, a) (((x) + ((a) - 1u)) & ~((a) - 1u))
#define CM_BIT(n) (1u << (n))

/* 句柄：不透明指针。不要假设它指向什么，也不要解引用。 */
typedef struct cm_opaque *cm_handle_t;

#define CM_HANDLE_NULL ((cm_handle_t)0)

#endif /* CM_TYPES_H */
