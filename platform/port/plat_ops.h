/*
 * 平台层契约：一个平台包要提供什么，框架才能在它上面跑起来。
 *
 * 这里只有函数指针，没有实现。不同芯片/RTOS 的差异全部落在这个结构体背后，
 * 上层（framework/ api/ apps/）看不到任何 SDK 类型。
 *
 * 硬必需 vs 可选：
 *   硬必需 —— 留空则 fw_init() 直接失败。没有它们，连"启动一个 App"都做不到，
 *            允许缺失只会把失败推迟到某个更难查的地方。
 *   可选   —— 允许为 NULL。上层每次调用前必须自己处理 NULL（框架的封装函数
 *            统一返回 CM_ERR_NOT_SUPPORTED），这就是"平台层默认实现返回失败"
 *            那条约束的落点。
 *
 * 为什么用 NULL 表示"不支持"而不是给一堆返回失败的 stub：
 *   平台包里 PLAT_OPS_STUB 是 {0}，作者漏填一目了然；而 stub 函数"看着像
 *   实现了"正是漂移的起点（能力位说支持、函数体什么都不做）。
 */
#ifndef PLAT_OPS_H
#define PLAT_OPS_H

#include "cm_types.h"
#include "cm_err.h"

/* 互斥的"永久等待"。不允许用 0 表示永久——那是最容易写错的约定。 */
#define PLAT_MUTEX_FOREVER 0xFFFFFFFFu

typedef struct plat_ops {
    /* ---- 硬必需：内存 ---- */
    void *(*mem_alloc)(cm_size n);
    void (*mem_free)(void *p);

    /* ---- 硬必需：时间 ----
     * clock_ms 只需单调递增，不需要与墙上时间对齐（日志时间戳另说）。 */
    uint32_t (*clock_ms)(void);
    void (*sleep_ms)(uint32_t ms);

    /* ---- 硬必需：互斥 ----
     * 只提供"创建/获取/释放"。不抽象优先级继承、递归锁、条件变量：
     * 这些在四家 RTOS 上语义不一致，抽象它们的代价大于收益。 */
    void *(*mutex_create)(void);
    cm_err_t (*mutex_take)(void *m, uint32_t timeout_ms);
    void (*mutex_give)(void *m);

    /* ---- 硬必需：任务 ----
     * 不传优先级：本框架所有 App 任务同一个优先级（架构 §3.5）。
     * 不提供"改优先级"的入口，这样就没有人能误用它。
     * stack_bytes 是建议值，平台可以按自己的对齐/最小栈调整。 */
    cm_err_t (*task_create)(const char *name, void (*fn)(void *), void *arg,
                            uint32_t stack_bytes);

    /* 结束当前任务，不返回。
     * 为什么必须有：app_run_exit() 被设计成"从任意阻塞点直接退出"，它不能返回，
     * 否则 App 会继续执行 app_run_exit() 之后的直线代码——正是伪阻塞要避免的。 */
    void (*task_exit_current)(void);

    /* ---- 可选 ---- */
    /* 输出一行日志（不含换行）。NULL 时框架丢弃日志，不报错。 */
    cm_err_t (*log_write)(const char *line);
} plat_ops_t;

/* 平台包从这个初始值开始覆盖：显式列出每个字段，漏填一个就编译报错，
 * 而不是悄悄留 NULL。 */
#define PLAT_OPS_STUB                                                          \
    {                                                                          \
        NULL, /* mem_alloc */                                                  \
            NULL, /* mem_free */                                               \
            NULL, /* clock_ms */                                               \
            NULL, /* sleep_ms */                                               \
            NULL, /* mutex_create */                                           \
            NULL, /* mutex_take */                                             \
            NULL, /* mutex_give */                                             \
            NULL, /* task_create */                                            \
            NULL, /* task_exit_current */                                      \
            NULL  /* log_write */                                              \
    }

/* 平台包实现它。返回的指针必须在本进程生命周期内一直有效（通常是静态常量）。 */
const plat_ops_t *plat_get_ops(void);

#endif /* PLAT_OPS_H */
