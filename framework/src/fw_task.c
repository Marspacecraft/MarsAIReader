/*
 * App 任务的创建与收尾。
 *
 * 这里只有两件事：把 App 放进一个任务，以及任务结束时怎么干净地退出。
 *
 * "我是哪个 App"放在 common 的任务局部变量里（cm_tls，0 号槽）：
 *   任意深度的阻塞接口（app_delay / 将来的 stor_read / disp_commit）都能拿到
 *   ctx，不需要把 ctx 一路透传。代价是"非 App 任务里这些接口不可用"，
 *   而那本来就该被禁止。
 *
 * 为什么用 cm_tls 而不是文件内 static _Thread_local：
 *   服务型 App（如 display）也需要回答"当前是哪个 App 在调我"，那属于 api/ 实现，
 *   在别的 TU 里；把这块状态放在 common 才能被双方读到而不用互相 include。
 */
#include "fw_internal.h"

#include "cm_tls.h"
#include "plat_ops.h"

app_ctx_t *fw_current_ctx(void)
{
    return (app_ctx_t *)cm_tls_get(FW_TLS_SLOT_CTX);
}

void fw_ctx_set_current(app_ctx_t *ctx)
{
    cm_tls_set(FW_TLS_SLOT_CTX, ctx);
}

static void fw_task_entry(void *arg)
{
    app_ctx_t *ctx = (app_ctx_t *)arg;

    fw_ctx_set_current(ctx);

    if (ctx->desc->init) {
        cm_err_t r = ctx->desc->init(ctx);
        if (r != CM_OK) {
            app_logp(ctx->desc->name, "init 失败: %s", cm_err_str(r));
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
    /* 任务名用 name 不用 id：调试器/RTOS 任务列表里看到的是人读的名字。
     * id 是给程序和位图用的，不该出现在这里。 */
    return p->task_create(ctx->desc->name, fw_task_entry, ctx, FW_APP_STACK_DEFAULT);
}
