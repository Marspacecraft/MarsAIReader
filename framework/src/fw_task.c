/*
 * App 任务的创建与收尾。
 *
 * 这里只有两件事：把 App 放进一个任务，以及任务结束时怎么干净地退出。
 *
 * 线程局部存储（s_cur）是伪阻塞的关键：
 *   任意深度的阻塞接口（app_delay / 将来的 stor_read / disp_commit）都能拿到
 *   "我是哪个 App"，不需要把 ctx 一路透传。代价是"非 App 任务里这些接口不可用"，
 *   而那本来就该被禁止。
 */
#include "fw_internal.h"
#include "plat_ops.h"

/* C11 标准线程局部存储。只在执行单元有独立栈的平台上成立（FreeRTOS 任务 / pthread
 * 都成立）。unsupported 的编译器在 cm_port.h 就被拦掉了，所以这里不用做降级。 */
static _Thread_local app_ctx_t *s_cur;

app_ctx_t *fw_current_ctx(void)
{
    return s_cur;
}

void fw_ctx_set_current(app_ctx_t *ctx)
{
    s_cur = ctx;
}

static void fw_task_entry(void *arg)
{
    app_ctx_t *ctx = (app_ctx_t *)arg;

    fw_ctx_set_current(ctx);

    if (ctx->desc->init) {
        cm_err_t r = ctx->desc->init(ctx);
        if (r != CM_OK) {
            app_logp(ctx->desc->id, "init 失败: %s", cm_err_str(r));
            fw_ctx_finish(ctx, APP_STATE_FAILED);
        }
    }

    /* 启动过程中就被要求停止：不要再进入 RUNNING，否则观测者会看到一个
     * 刚起来就已经该停的 App。 */
    if (cm_atomic_u32_load(&ctx->cancel, CM_MO_ACQUIRE) != 0u) {
        fw_ctx_finish(ctx, APP_STATE_STOPPED);
    }

    cm_atomic_u32_store(&ctx->state, APP_STATE_RUNNING, CM_MO_RELEASE);

    if (ctx->desc->task) {
        ctx->desc->task(ctx);
    }

    /* task() 正常返回 = 自愿退出。 */
    fw_ctx_finish(ctx, APP_STATE_STOPPED);
}

cm_err_t fw_task_spawn(app_ctx_t *ctx)
{
    const plat_ops_t *p = fw_plat();

    if (p->task_create == NULL) {
        return CM_ERR_NOT_SUPPORTED;
    }
    return p->task_create(ctx->desc->id, fw_task_entry, ctx, FW_APP_STACK_DEFAULT);
}
