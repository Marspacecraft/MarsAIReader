/*
 * host-fake 平台：让整个框架 + 应用层在 PC 上跑起来。
 *
 * 它不是"模拟器"，不模拟任何硬件。它只实现硬必需项，让调度、生命周期、
 * 伪阻塞这些东西能在没有板子的情况下被验证——这些恰好是最容易写错、
 * 又最难在板子上调试的部分。
 *
 * 可选能力全部留空（NULL）：上层的降级路径因此每次 host 测试都会被走到。
 */

/*
 * 必须在任何 #include 之前。
 *
 * 门禁用 -std=c11（严格 ISO），glibc 见到 __STRICT_ANSI__ 就不定义 _DEFAULT_SOURCE，
 * 于是 POSIX 的 clock_gettime / nanosleep / pthread_mutex_timedlock 全部没有声明，
 * 编译直接失败。Windows 走 _WIN32 分支看不到这个坑，所以它只在 CI 上暴露。
 *
 * 只在这一个 TU 里要 POSIX，不放宽全局 CFLAGS：纯 C 边界（tools/check_purity.py）
 * 管的是 common / api / framework / apps，host-fake 是主机参考实现，不在其中。
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "plat_ops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif

/* ---------------------------------------------------------------------------
 * 内存
 * ------------------------------------------------------------------------- */
static void *host_mem_alloc(cm_size n)
{
    return malloc((size_t)n);
}

static void host_mem_free(void *p)
{
    free(p);
}

/* ---------------------------------------------------------------------------
 * 时间
 * ------------------------------------------------------------------------- */
static uint32_t host_clock_ms(void)
{
#if defined(_WIN32)
    return (uint32_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000));
#endif
}

static void host_sleep_ms(uint32_t ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/* ---------------------------------------------------------------------------
 * 互斥
 * ------------------------------------------------------------------------- */
#if defined(_WIN32)
static void *host_mutex_create(void)
{
    return (void *)CreateMutexW(NULL, FALSE, NULL);
}

static cm_err_t host_mutex_take(void *m, uint32_t timeout_ms)
{
    if (m == NULL) {
        return CM_ERR_INVALID;
    }
    DWORD wait = (timeout_ms == PLAT_MUTEX_FOREVER) ? INFINITE : (DWORD)timeout_ms;
    return WaitForSingleObject((HANDLE)m, wait) == WAIT_OBJECT_0 ? CM_OK
                                                                 : CM_ERR_TIMEOUT;
}

static void host_mutex_give(void *m)
{
    if (m != NULL) {
        ReleaseMutex((HANDLE)m);
    }
}
#else
static void *host_mutex_create(void)
{
    pthread_mutex_t *m = malloc(sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    if (pthread_mutex_init(m, NULL) != 0) {
        free(m);
        return NULL;
    }
    return m;
}

static cm_err_t host_mutex_take(void *m, uint32_t timeout_ms)
{
    if (m == NULL) {
        return CM_ERR_INVALID;
    }
    if (timeout_ms == PLAT_MUTEX_FOREVER) {
        return pthread_mutex_lock((pthread_mutex_t *)m) == 0 ? CM_OK : CM_ERR_IO;
    }

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000u);
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    int rc = pthread_mutex_timedlock((pthread_mutex_t *)m, &deadline);
    if (rc == 0) {
        return CM_OK;
    }
    return rc == ETIMEDOUT ? CM_ERR_TIMEOUT : CM_ERR_IO;
}

static void host_mutex_give(void *m)
{
    if (m != NULL) {
        (void)pthread_mutex_unlock((pthread_mutex_t *)m);
    }
}
#endif

/* ---------------------------------------------------------------------------
 * 任务
 *
 * 框架只传 (fn, arg)，不传优先级。这里用默认调度策略——框架层也从不依赖
 * 某个任务"更优先"（架构 §3.5）。
 * ------------------------------------------------------------------------- */
typedef struct {
    void (*fn)(void *);
    void *arg;
} host_task_boot_t;

#if defined(_WIN32)
static DWORD WINAPI host_task_tramp(LPVOID p)
{
    host_task_boot_t *b = (host_task_boot_t *)p;
    void (*fn)(void *) = b->fn;
    void *arg = b->arg;
    free(b);
    fn(arg);
    return 0;
}

static cm_err_t host_task_create(const char *name, void (*fn)(void *), void *arg,
                                 uint32_t stack_bytes)
{
    (void)name;
    (void)stack_bytes; /* 栈大小由链接器的 /STACK 决定，这里无法按任务调整 */

    host_task_boot_t *b = malloc(sizeof(*b));
    if (b == NULL) {
        return CM_ERR_NOMEM;
    }
    b->fn = fn;
    b->arg = arg;

    HANDLE h = CreateThread(NULL, 0, host_task_tramp, b, 0, NULL);
    if (h == NULL) {
        free(b);
        return CM_ERR_NOMEM;
    }
    CloseHandle(h); /* 不 join：关闭句柄不影响线程继续跑 */
    return CM_OK;
}

static void host_task_exit_current(void)
{
    ExitThread(0);
}
#else
static void *host_task_tramp(void *p)
{
    host_task_boot_t *b = (host_task_boot_t *)p;
    void (*fn)(void *) = b->fn;
    void *arg = b->arg;
    free(b);
    fn(arg);
    return NULL;
}

static cm_err_t host_task_create(const char *name, void (*fn)(void *), void *arg,
                                 uint32_t stack_bytes)
{
    (void)name;
    (void)stack_bytes;

    host_task_boot_t *b = malloc(sizeof(*b));
    if (b == NULL) {
        return CM_ERR_NOMEM;
    }
    b->fn = fn;
    b->arg = arg;

    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&t, &attr, host_task_tramp, b);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(b);
        return CM_ERR_NOMEM;
    }
    return CM_OK;
}

static void host_task_exit_current(void)
{
    pthread_exit(NULL);
}
#endif

/* ---------------------------------------------------------------------------
 * 日志（可选能力）
 * ------------------------------------------------------------------------- */
static cm_err_t host_log_write(const char *line)
{
    if (line == NULL) {
        return CM_ERR_INVALID;
    }
    fputs(line, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    return CM_OK;
}

/* ---------------------------------------------------------------------------
 * 平台描述
 * ------------------------------------------------------------------------- */
static const plat_ops_t s_ops = {
    .mem_alloc = host_mem_alloc,
    .mem_free = host_mem_free,
    .clock_ms = host_clock_ms,
    .sleep_ms = host_sleep_ms,
    .mutex_create = host_mutex_create,
    .mutex_take = host_mutex_take,
    .mutex_give = host_mutex_give,
    .task_create = host_task_create,
    .task_exit_current = host_task_exit_current,
    .log_write = host_log_write,
};

const plat_ops_t *plat_get_ops(void)
{
    return &s_ops;
}
