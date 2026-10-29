/*
 * 任务局部变量（task-local storage），对应 Linux 的线程变量。
 *
 * 用途：让"当前是哪个 App"这类信息可以被任意深度的函数读到，而不必一路透传参数。
 * 框架用它存 app_ctx_t*（见 fw_task.c）；服务型 App 也可以用它存自己的会话指针，
 * 但**不要**用它存需要跨任务共享的东西——那不是它的语义。
 *
 * 为什么需要它：伪阻塞要求"任意阻塞点都能判断是否该退出"，而阻塞点可能是
 * stor_read() 里的第 3 层调用。透传 ctx 会让每个签名都多一个参数，且容易漏。
 *
 * 槽位：0..CM_TLS_SLOT_MAX-1。槽位分配是全局约定，写在各自的头文件里
 * （例如框架用 0 号槽），不要临时找一个空槽——两个模块抢同一个槽是静默的。
 *
 * 实现：C11 `_Thread_local`。在"每个执行单元独立栈"的平台上成立
 * （FreeRTOS 任务、pthread 都成立）。若某个平台的编译器没有可用 TLS 运行时，
 * 由该平台在自己的包里实现同签名的 cm_tls_get/cm_tls_set 覆盖弱符号——
 * 这正是把它放进 common 而不是写死在 framework 里的原因。
 */
#ifndef CM_TLS_H
#define CM_TLS_H

#include "cm_types.h"

/* 槽位上限。用固定数组而不是链表：查表一次间接访问，没有分配失败路径。 */
#define CM_TLS_SLOT_MAX 8u

/* 写槽位。slot 越界是调用方的 bug，这里静默忽略（common 不打日志，见 code-structure §2.1）。
 * 传入 NULL 等价于"清空该槽"。 */
void cm_tls_set(uint32_t slot, void *value);

/* 读槽位。越界或未设置返回 NULL。 */
void *cm_tls_get(uint32_t slot);

#endif /* CM_TLS_H */
