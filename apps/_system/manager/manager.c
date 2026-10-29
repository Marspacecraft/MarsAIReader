/*
 * 0 号 App 的实现。
 *
 * 注意 task() 里没有任何取消检查：app_delay() 会在取消时替它退出。
 * 这是伪阻塞模型的验收样本——App 写直线代码，能不能停是框架的保证。
 */
#include "manager.h"

#include "app.h"
#include "app_manifest.h"
#include "fw_app.h"

#define MGR_TICK_MS 100u

/* 上一次上报过的状态。只在变化时打印，否则每 100ms 一行会淹掉真实日志。 */
static app_state_t s_reported[FW_APP_MAX];

static cm_err_t manager_init(app_ctx_t *ctx)
{
    (void)ctx;
    app_log("manager: init，注册表里 %u 个 App", (unsigned)fw_app_count());
    return CM_OK;
}

static void manager_exit(app_ctx_t *ctx)
{
    (void)ctx;
    app_log("manager: exit");
}

static void manager_start_all(void)
{
    uint32_t n = fw_app_count();
    for (uint32_t i = 0; i < n; i++) {
        fw_app_t *a = fw_app_at(i);
        const app_desc_t *d = fw_app_desc(a);

        s_reported[i] = fw_app_state_of(a);

        if ((d->flags & APP_FLAG_SYSTEM) != 0u) {
            continue;
        }
        cm_err_t r = fw_app_start(a);
        app_log("manager: 启动 %s -> %s", d->id, cm_err_str(r));
    }
}

static void manager_supervise(void)
{
    uint32_t n = fw_app_count();
    for (uint32_t i = 0; i < n; i++) {
        fw_app_t *a = fw_app_at(i);
        const app_desc_t *d = fw_app_desc(a);
        app_state_t prev = s_reported[i];
        app_state_t st = fw_app_state_of(a);

        if (st == prev) {
            continue;
        }
        s_reported[i] = st;
        app_log("manager: %s %s -> %s", d->id, app_state_str(prev), app_state_str(st));

        /* 单 MCU 上"重启 App"最多是"重新创建它的任务"，真正的隔离不存在。
         * 所以这里只报告，不自动重启：失败循环比失败本身更贵（电量、鬼影、日志）。 */
        if (st == APP_STATE_FAILED) {
            app_log("manager: %s 进入 FAILED，不再自动重启", d->id);
        }
    }
}

static void manager_task(app_ctx_t *ctx)
{
    (void)ctx;

    manager_start_all();

    for (;;) {
        app_delay(MGR_TICK_MS);
        manager_supervise();
    }
}

const app_desc_t g_app_manager = {
    "manager",
    "0 号管理器",
    manager_init,
    manager_exit,
    manager_task,
    APP_FLAG_SYSTEM,
};
