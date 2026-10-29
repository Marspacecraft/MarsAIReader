/*
 * App 的形态：一个描述体 + 三个函数 + 一个上下文。
 *
 * 这是**契约**，不是实现。App 作者（人和 AI）只需要看这个头。
 *
 * 核心约定（必须理解，否则写出来的 App 会被强制终止）：
 *
 *   1. 框架提供的每个"会阻塞"的接口都是**伪阻塞**的。App 写直线代码：
 *      while (1) { do_work(); app_delay(100); }
 *      取消请求到达时，app_delay() 内部会调用 app_run_exit()，**不返回**。
 *      所以 App 不需要检查返回值、不需要写状态机、不需要轮询取消位。
 *
 *   2. 因此 app_delay() 之后的代码不一定会执行。清理逻辑只能写在 exit() 回调里，
 *      不能写在 task() 的循环后面。
 *
 *   3. exit() 在清理期间不会再被取消打断（框架自己保证），所以可以放心做
 *      关文件、释放内存、恢复外设这类必须完成的事。
 *
 *   4. App 不得自行创建任务、不得直接访问平台层、不得 include 除 api/lib/common
 *      以外的东西。门禁会拦。
 */
#ifndef API_APP_H
#define API_APP_H

#include "cm_types.h"
#include "cm_err.h"

/* 不透明：App 只能通过 api 操作自己的上下文，不能读它的字段。
 * 这样"取消位怎么放、资源怎么登记"是框架的自由，不必变成 ABI。 */
typedef struct app_ctx app_ctx_t;

typedef enum {
    APP_STATE_EMPTY = 0, /* 还没启动过 */
    APP_STATE_INIT,      /* 在自己的任务里跑 init() */
    APP_STATE_RUNNING,   /* init 成功，task() 运行中 */
    APP_STATE_STOPPING,  /* 已请求停止，等任务退出（协作式，会等到下一个阻塞点） */
    APP_STATE_STOPPED,   /* 已退出，可以再次 start */
    APP_STATE_FAILED,    /* init 失败；不会自动重启 */
} app_state_t;

typedef struct app_desc {
    const char *id;   /* 稳定标识，如 "reader"。日志、注册表、门禁都用它 */
    const char *name; /* 给人看的名字 */

    /* 在 App 自己的任务里执行。返回非 OK 则 App 进入 FAILED，不再启动。 */
    cm_err_t (*init)(app_ctx_t *ctx);

    /* 任务结束前**一定**调用一次：正常返回、被取消、init 失败都走这里。
     * 期间取消被禁用，可以安全做多步清理。 */
    void (*exit)(app_ctx_t *ctx);

    /* 主任务体。正常情况不返回（靠伪阻塞接口退出）。
     * 如果返回了，框架按"自愿退出"处理：调用 exit() 后进入 STOPPED。 */
    void (*task)(app_ctx_t *ctx);

    uint32_t flags;
} app_desc_t;

#define APP_FLAG_NONE 0u
/* 由 fw_boot() 直接拉起，不经过 0 号 App 的配额与调度。0 号自己用这个标志。 */
#define APP_FLAG_SYSTEM (1u << 0)

/* ---- 身份与状态 ---- */
const char *app_id(const app_ctx_t *ctx);
app_state_t app_state(const app_ctx_t *ctx);
const char *app_state_str(app_state_t s);

/* ---- 伪阻塞 ---- */

/* 当前 App 是否已被请求停止。一般不需要调用：阻塞接口会替你处理。 */
int app_cancelled(void);

/* 立即结束当前 App 任务，不返回。
 * 由框架的阻塞接口调用；App 也可以主动调用（等价于"我现在要退出"）。
 * 在非 App 任务里调用是空操作（返回），但那种调用本身就是错误设计。 */
void app_run_exit(void);

/* 伪阻塞延时。内部按切片等待，每片检查一次取消位；被取消则走 app_run_exit()。 */
cm_err_t app_delay(uint32_t ms);

/* ---- 内存 ----
 * 统一走框架，便于将来做配额与统计。App 不得直接调用 malloc（门禁可加规则）。 */
void *app_alloc(cm_size n);
void app_free(void *p);

#include "log.h"

#endif /* API_APP_H */
