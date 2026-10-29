/*
 * 示例 App：能被 AI 直接照抄的最小骨架。
 *
 * 它演示了三件事，也是 App 的全部要求：
 *   1. 一个无限循环（不写退出条件，退出由框架负责）；
 *   2. 循环里只调用带伪阻塞的接口，不自己 sleep、不自己创建任务；
 *   3. 清理写在 exit()，不写在循环后面（因为循环后面的代码永远到不了）。
 */
#include "api.h"

/* 测试用：证明 exit() 真的被调用了，且重启后能再次被调用。 */
uint32_t g_hello_exit_count;

static cm_err_t hello_init(app_ctx_t *ctx)
{
    (void)ctx;
    app_log("hello: init");
    return CM_OK;
}

static void hello_exit(app_ctx_t *ctx)
{
    (void)ctx;
    g_hello_exit_count++;
    app_log("hello: exit（第 %u 次）", (unsigned)g_hello_exit_count);
}

static void hello_task(app_ctx_t *ctx)
{
    (void)ctx;

    uint32_t tick = 0;
    for (;;) {
        app_log("hello: tick %u", (unsigned)tick);
        tick++;
        /* 这就是"伪阻塞"：如果有停止请求，这里直接退出任务，不会返回。 */
        app_delay(200);
    }
}

const app_desc_t g_app_hello = {
    "hello",
    "Hello App",
    hello_init,
    hello_exit,
    hello_task,
    APP_FLAG_NONE,
};
