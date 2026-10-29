/*
 * 框架层内部控制面（私有）：App 作者看不到这一层。
 *
 * 这里放的是"框架自己需要、但服务型 App 不该碰"的东西：
 * App 上下文的真实布局、当前线程上下文、任务收尾。
 */
#ifndef FW_INTERNAL_H
#define FW_INTERNAL_H

#include "cm_types.h"
#include "cm_atomic.h"
#include "app.h"
#include "plat_ops.h"

struct app_ctx {
    const app_desc_t *desc;
    /* 取消请求。manager 置 1，任务在阻塞点发现后自行收尾并清零。 */
    cm_atomic_u32 cancel;
    /* app_state_t。放在原子变量里，因为观测者（0 号）与执行者（App 任务）
     * 分属不同任务，普通 int 的读写是数据竞争。 */
    cm_atomic_u32 state;
};

/* App 任务的默认栈（字节）。由 manifest 的 desc 覆盖是下一步的事：
 * 现在所有 App 一样大，避免"某个 App 栈溢出"变成随机崩溃。 */
#define FW_APP_STACK_DEFAULT 4096u

const plat_ops_t *fw_plat(void);

/* 当前线程所属的 App 上下文；非 App 任务返回 NULL。
 * 用线程局部存储实现——这样 api 层不必把 ctx 一路透传。 */
app_ctx_t *fw_current_ctx(void);
void fw_ctx_set_current(app_ctx_t *ctx);

/* 启动一个 App 任务。 */
cm_err_t fw_task_spawn(app_ctx_t *ctx);

/* App 任务的统一收尾：先禁用取消，再跑 exit()，最后置终态并结束任务。
 * 不返回。final_state 通常是 STOPPED 或 FAILED。 */
void fw_ctx_finish(app_ctx_t *ctx, app_state_t final_state) CM_NORETURN;

#endif /* FW_INTERNAL_H */
