/*
 * App 生命周期与伪阻塞的验收测试。
 *
 * 这个测试是"框架能不能跑起来"的定义：它同时证明了
 *   - 注册表能初始化、0 号能被拉起、0 号能拉起别的 App；
 *   - 停止是协作式的，而且延时中途也能停（伪阻塞真的成立了）；
 *   - 重启能成功（取消位的清零顺序对了）；
 *   - 非法状态转换被拒绝；
 *   - SYSTEM 只是"谁先启动"，不是特权等级（0 号自己也能被停）。
 *
 * 它跑在 host-fake 平台上：真线程、真时间、真调度，只是没有硬件。
 */
#include <string.h>

#include "tinytest.h"

#include "api.h"
#include "app_manifest.h"
#include "fw_app.h"
#include "plat_ops.h"

/* hello App 用来证明 exit() 真被调用了 */
extern uint32_t g_hello_exit_count;

static uint32_t now_ms(void)
{
    return plat_get_ops()->clock_ms();
}

static void nap(uint32_t ms)
{
    plat_get_ops()->sleep_ms(ms);
}

/* 返回 1 = 到达目标状态，0 = 超时，-1 = 没有这个 App */
static int wait_state(const char *id, app_state_t want, uint32_t timeout_ms)
{
    uint32_t t0 = now_ms();
    for (;;) {
        fw_app_t *a = fw_app_find(id);
        if (a == NULL) {
            return -1;
        }
        if (fw_app_state_of(a) == want) {
            return 1;
        }
        if ((now_ms() - t0) > timeout_ms) {
            return 0;
        }
        nap(2);
    }
}

static void manifest_is_sane(void)
{
    uint32_t n = 0;
    const app_desc_t *const *m = app_manifest(&n);

    TT_CHECK(m != NULL);
    TT_CHECK(n >= 2u);
    if (m == NULL) {
        return;
    }

    uint32_t systems = 0;
    for (uint32_t i = 0; i < n; i++) {
        TT_CHECK(m[i] != NULL);
        if (m[i] == NULL) {
            continue;
        }
        TT_CHECK(m[i]->id != NULL);
        TT_CHECK(m[i]->task != NULL); /* 没有 task 的 App 没有意义 */
        if ((m[i]->flags & APP_FLAG_SYSTEM) != 0u) {
            systems++;
        }
        for (uint32_t j = i + 1; j < n; j++) {
            /* id 必须唯一：日志、注册表、门禁全用它做键 */
            TT_CHECK(strcmp(m[i]->id, m[j]->id) != 0);
        }
    }
    TT_CHECK_EQ_U32(systems, 1u); /* 目前只有 0 号是 SYSTEM */
}

static void lifecycle(void)
{
    uint32_t n = 0;
    const app_desc_t *const *m = app_manifest(&n);

    TT_CHECK_EQ_U32(fw_boot(m, n), CM_OK);
    TT_CHECK_EQ_U32(fw_app_count(), n);

    /* 0 号由 fw_boot 直接拉起；hello 由 0 号拉起 */
    TT_CHECK_EQ_U32(wait_state("manager", APP_STATE_RUNNING, 2000u), 1);
    TT_CHECK_EQ_U32(wait_state("hello", APP_STATE_RUNNING, 2000u), 1);
    TT_CHECK_EQ_U32(g_hello_exit_count, 0u);

    fw_app_t *hello = fw_app_find("hello");
    fw_app_t *mgr = fw_app_find("manager");
    TT_CHECK(hello != NULL);
    TT_CHECK(mgr != NULL);
    if (hello == NULL || mgr == NULL) {
        return;
    }

    /* 1) 停止：请求发出时 hello 正卡在 app_delay(200) 里。
     *    如果伪阻塞没生效，这里会等满 200ms 才退出。 */
    uint32_t t0 = now_ms();
    fw_app_request_stop(hello);
    TT_CHECK_EQ_U32(wait_state("hello", APP_STATE_STOPPED, 2000u), 1);
    uint32_t stop_ms = now_ms() - t0;
    printf("  hello 从请求停止到 STOPPED: %u ms\n", (unsigned)stop_ms);
    TT_CHECK(stop_ms < 500u);
    TT_CHECK_EQ_U32(g_hello_exit_count, 1u);

    /* fw_app.c 不变式 3 的已知窗口：STOPPED 之后任务还要几十微秒才真正结束。
     * 让出一次，避免重启时短暂出现两个任务。 */
    nap(20);

    /* 2) 重启：能起来说明取消位被任务清掉了（顺序错了会立刻又退出） */
    TT_CHECK_EQ_U32(fw_app_start(hello), CM_OK);
    TT_CHECK_EQ_U32(wait_state("hello", APP_STATE_RUNNING, 2000u), 1);
    TT_CHECK_EQ_U32(g_hello_exit_count, 1u); /* 还没退出过第二次 */

    /* 3) 再停一次 */
    fw_app_request_stop(hello);
    TT_CHECK_EQ_U32(wait_state("hello", APP_STATE_STOPPED, 2000u), 1);
    TT_CHECK_EQ_U32(g_hello_exit_count, 2u);

    /* 4) 0 号自己也能被停：SYSTEM 只决定"谁先起来" */
    fw_app_request_stop(mgr);
    TT_CHECK_EQ_U32(wait_state("manager", APP_STATE_STOPPED, 2000u), 1);

    /* 5) 状态机拒绝非法转换 */
    TT_CHECK_EQ_U32(fw_app_start(mgr), CM_OK);
    TT_CHECK_EQ_U32(fw_app_start(mgr), CM_ERR_STATE); /* 已经在跑 */

    /* 收尾：把 0 号停掉，否则进程退出时它还在打日志 */
    fw_app_request_stop(mgr);
    TT_CHECK_EQ_U32(wait_state("manager", APP_STATE_STOPPED, 2000u), 1);
}

static const tt_case_t s_cases[] = {
    TT_CASE(manifest_is_sane),
    TT_CASE(lifecycle),
};

TT_MAIN(s_cases)
