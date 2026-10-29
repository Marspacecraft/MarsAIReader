/*
 * App 生命周期的全部实现。
 *
 * 状态机（只有 6 个状态，没有"暂停"——单 MCU 上的暂停等于"占着资源不干活"）：
 *
 *   EMPTY --start--> INIT --init成功--> RUNNING --request_stop--> STOPPING --> STOPPED
 *                      |                   |                                    |
 *                      +--init失败--> FAILED +--task返回（自愿退出）-------------+
 *                                                                               |
 *                                                     STOPPED --start--> INIT   (可重启)
 *
 * 三条不变式：
 *   1. exit() 一定被调用恰好一次（FAILED 也调用），所以 App 可以放心把清理放这里。
 *   2. cancel 位置 1 只由观测者（0 号）做；清零只由任务自己在收尾时做。
 *      顺序反了（先清后停 / 先停后清）会导致"重启后立刻又退出"或"永远起不来"。
 *   3. STOPPED 是"任务即将结束"的宣告，不是"已经结束"。平台没有 join 语义，
 *      所以这里留着一个微秒级窗口：观测者看到 STOPPED 后立刻 start，可能短暂
 *      出现两个任务。当前用"观测轮询间隔"把它压在毫秒级；要彻底消除需要平台
 *      提供任务 join，已记入待定项。
 */
#include <string.h>

#include "app.h"
#include "cm_atomic.h"
#include "fw_app.h"
#include "fw_internal.h"
#include "plat_ops.h"

/* 取消检查的粒度：一次不可打断的等待最多这么久。
 * 这个值是"停止延迟"的上界，也是音频/触摸这类实时路径的容忍上限。 */
#define FW_CANCEL_SLICE_MS 50u

struct fw_app {
    app_ctx_t ctx;
};

static fw_app_t s_apps[FW_APP_MAX];
static uint32_t s_count;
static const plat_ops_t *s_plat;

const plat_ops_t *fw_plat(void)
{
    return s_plat;
}

static int hard_required_ok(const plat_ops_t *p)
{
    return p != NULL && p->mem_alloc != NULL && p->mem_free != NULL &&
           p->clock_ms != NULL && p->sleep_ms != NULL && p->mutex_create != NULL &&
           p->mutex_take != NULL && p->mutex_give != NULL &&
           p->task_create != NULL && p->task_exit_current != NULL;
}

cm_err_t fw_init(const app_desc_t *const *manifest, uint32_t count)
{
    if (manifest == NULL || count == 0u || count > FW_APP_MAX) {
        return CM_ERR_INVALID;
    }
    if (!hard_required_ok(plat_get_ops())) {
        /* 平台缺硬必需项。这里不打印：日志通道本身可能就是缺的那一项。 */
        return CM_ERR_NOT_SUPPORTED;
    }

    s_plat = plat_get_ops();
    s_count = count;

    for (uint32_t i = 0; i < count; i++) {
        const app_desc_t *d = manifest[i];
        if (d == NULL || d->id == NULL || d->task == NULL) {
            return CM_ERR_INVALID;
        }
        memset(&s_apps[i], 0, sizeof(s_apps[i]));
        s_apps[i].ctx.desc = d;
        cm_atomic_u32_init(&s_apps[i].ctx.cancel, 0u);
        cm_atomic_u32_init(&s_apps[i].ctx.state, (uint32_t)APP_STATE_EMPTY);
    }
    return CM_OK;
}

cm_err_t fw_boot(const app_desc_t *const *manifest, uint32_t count)
{
    cm_err_t r = fw_init(manifest, count);
    if (r != CM_OK) {
        return r;
    }

    /* 只拉起 SYSTEM App（0 号）。其余由 0 号按自己的策略启动——
     * 这样"谁先起来"这件事只有一个决定点。 */
    for (uint32_t i = 0; i < s_count; i++) {
        if ((s_apps[i].ctx.desc->flags & APP_FLAG_SYSTEM) == 0u) {
            continue;
        }
        r = fw_app_start(&s_apps[i]);
        if (r != CM_OK) {
            return r;
        }
    }
    return CM_OK;
}

uint32_t fw_app_count(void)
{
    return s_count;
}

fw_app_t *fw_app_at(uint32_t index)
{
    return index < s_count ? &s_apps[index] : NULL;
}

fw_app_t *fw_app_find(const char *id)
{
    if (id == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < s_count; i++) {
        if (strcmp(s_apps[i].ctx.desc->id, id) == 0) {
            return &s_apps[i];
        }
    }
    return NULL;
}

const app_desc_t *fw_app_desc(const fw_app_t *a)
{
    return a != NULL ? a->ctx.desc : NULL;
}

app_state_t fw_app_state_of(const fw_app_t *a)
{
    if (a == NULL) {
        return APP_STATE_EMPTY;
    }
    return (app_state_t)cm_atomic_u32_load(&a->ctx.state, CM_MO_ACQUIRE);
}

cm_err_t fw_app_start(fw_app_t *a)
{
    if (a == NULL) {
        return CM_ERR_INVALID;
    }

    app_state_t st = fw_app_state_of(a);
    if (st != APP_STATE_EMPTY && st != APP_STATE_STOPPED) {
        return CM_ERR_STATE;
    }

    cm_atomic_u32_store(&a->ctx.cancel, 0u, CM_MO_RELEASE);
    cm_atomic_u32_store(&a->ctx.state, (uint32_t)APP_STATE_INIT, CM_MO_RELEASE);

    cm_err_t r = fw_task_spawn(&a->ctx);
    if (r != CM_OK) {
        cm_atomic_u32_store(&a->ctx.state, (uint32_t)APP_STATE_EMPTY, CM_MO_RELEASE);
    }
    return r;
}

void fw_app_request_stop(fw_app_t *a)
{
    if (a == NULL) {
        return;
    }

    app_state_t st = fw_app_state_of(a);
    if (st != APP_STATE_INIT && st != APP_STATE_RUNNING) {
        return;
    }

    /* 先转状态再置位：观测者看到 STOPPING 时取消位一定已经生效。 */
    cm_atomic_u32_store(&a->ctx.state, (uint32_t)APP_STATE_STOPPING, CM_MO_RELEASE);
    cm_atomic_u32_store(&a->ctx.cancel, 1u, CM_MO_RELEASE);
}

/* ---------------------------------------------------------------------------
 * 任务收尾。不返回。
 * ------------------------------------------------------------------------- */
void fw_ctx_finish(app_ctx_t *ctx, app_state_t final_state)
{
    /* 清零取消位 = 禁用取消。exit() 里即便调到伪阻塞接口也不会再次触发退出，
     * 否则清理被反复打断，永远结束不了。 */
    cm_atomic_u32_store(&ctx->cancel, 0u, CM_MO_RELEASE);

    if (ctx->desc->exit != NULL) {
        ctx->desc->exit(ctx);
    }

    /* 最后一步才置终态：观测者看到 STOPPED 时，取消位已清、清理已完成。 */
    cm_atomic_u32_store(&ctx->state, (uint32_t)final_state, CM_MO_RELEASE);

    fw_plat()->task_exit_current();

    /* 平台违约返回时的兜底：绝不能让控制流跑回调用者的栈。 */
    for (;;) {
    }
}

/* ---------------------------------------------------------------------------
 * api/app.h 的实现
 * ------------------------------------------------------------------------- */

const char *app_id(const app_ctx_t *ctx)
{
    return (ctx != NULL && ctx->desc != NULL) ? ctx->desc->id : "?";
}

app_state_t app_state(const app_ctx_t *ctx)
{
    if (ctx == NULL) {
        return APP_STATE_EMPTY;
    }
    return (app_state_t)cm_atomic_u32_load(&ctx->state, CM_MO_ACQUIRE);
}

const char *app_state_str(app_state_t s)
{
    switch (s) {
    case APP_STATE_EMPTY:
        return "EMPTY";
    case APP_STATE_INIT:
        return "INIT";
    case APP_STATE_RUNNING:
        return "RUNNING";
    case APP_STATE_STOPPING:
        return "STOPPING";
    case APP_STATE_STOPPED:
        return "STOPPED";
    case APP_STATE_FAILED:
        return "FAILED";
    default:
        return "?";
    }
}

int app_cancelled(void)
{
    app_ctx_t *c = fw_current_ctx();
    if (c == NULL) {
        return 0;
    }
    return cm_atomic_u32_load(&c->cancel, CM_MO_ACQUIRE) != 0u;
}

void app_run_exit(void)
{
    app_ctx_t *c = fw_current_ctx();
    if (c == NULL) {
        /* 非 App 任务。没有可退出的 App，直接返回（不崩，但这是设计错误）。 */
        return;
    }
    app_logp(c->desc->id, "收到停止请求，退出");
    fw_ctx_finish(c, APP_STATE_STOPPED);
}

cm_err_t app_delay(uint32_t ms)
{
    const plat_ops_t *p = fw_plat();
    if (p == NULL || p->sleep_ms == NULL) {
        return CM_ERR_NOT_SUPPORTED;
    }

    app_ctx_t *c = fw_current_ctx();
    uint32_t left = ms;

    while (left > 0u) {
        uint32_t step = left > FW_CANCEL_SLICE_MS ? FW_CANCEL_SLICE_MS : left;
        p->sleep_ms(step);
        left -= step;

        /* 伪阻塞：延时中途也会响应停止。这就是 App 不需要轮询的原因。 */
        if (c != NULL && cm_atomic_u32_load(&c->cancel, CM_MO_ACQUIRE) != 0u) {
            app_run_exit();
        }
    }
    return CM_OK;
}

void *app_alloc(cm_size n)
{
    const plat_ops_t *p = fw_plat();
    if (p == NULL || p->mem_alloc == NULL || n == 0u) {
        return NULL;
    }
    return p->mem_alloc(n);
}

void app_free(void *ptr)
{
    const plat_ops_t *p = fw_plat();
    if (ptr != NULL && p != NULL && p->mem_free != NULL) {
        p->mem_free(ptr);
    }
}
