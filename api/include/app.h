/*
 * App 的形态：一个描述体 + 三个函数 + 一个上下文。
 *
 * 这是**契约**，不是实现。App 作者（人和 AI）只需要看这个头。
 *
 * 核心约定（必须理解，否则写出来的 App 会被强制终止）：
 *
 *   1. 框架提供的每个"会阻塞"的接口内部都会响应停止请求。App 写直线代码：
 *      while (1) { do_work(); app_delay(100); }
 *      停止请求到达时，app_delay() 里的等待会提前结束，任务直接进入退出流程。
 *      所以 App 不需要检查返回值、不需要写状态机、不需要轮询。
 *
 *   2. 因此 app_delay() 之后的代码不一定会执行。清理逻辑只能写在 exit() 回调里，
 *      不能写在 task() 的循环后面。
 *
 *   3. exit() 在清理期间不会再被打断（框架自己保证），所以可以放心做
 *      关文件、释放内存、恢复外设这类必须完成的事。
 *
 *   4. App 不得自行创建任务、不得直接访问平台层、不得 include 除 api/lib/common
 *      以外的东西。门禁会拦。
 *
 * "退出是怎么发生"这一层机制（谁能查取消位、谁能立刻结束任务）不在这个头里。
 * 那是框架与系统 App 之间的事，见 sys/app_sys.h；AI 写的 App 看不到也不该用。
 */
#ifndef API_APP_H
#define API_APP_H

#include "cm_types.h"
#include "cm_err.h"

/* App 的机器标识。
 *
 * 为什么不是字符串：id 要出现在日志前缀、注册表查找、将来的按 App 配额/位图里，
 * 每次比较都走 strcmp 是白花时间；而且 u8 让"一共有哪些 App"可以是一个位图。
 *
 * 分配规则：1..254，全固件唯一，0 保留给"无效"。人读的名字用 name 字段，
 * 日志与 UI 都打 name，不要打 id。
 */
typedef cm_u8 app_id_t;

#define APP_ID_NONE 0u
#define APP_ID_MAX 254u

/* 不透明：App 只能通过 api 操作自己的上下文，不能读它的字段。
 * 这样"取消位怎么放、资源怎么登记"是框架的自由，不必变成 ABI。 */
typedef struct app_ctx app_ctx_t;

typedef struct app_desc {
    app_id_t id;      /* 机器标识，见上面的分配规则 */
    const char *name; /* 给人看的名字（日志、UI）。长度由作者决定，不参与比较 */

    /* 在 App 自己的任务里执行。返回非 OK 则 App 进入 FAILED，不再启动。 */
    cm_err_t (*init)(app_ctx_t *ctx);

    /* 任务结束前**一定**调用一次：正常返回、被停止打断、init 失败都走这里。
     * 期间不会被再次打断，可以安全做多步清理。 */
    void (*exit)(app_ctx_t *ctx);

    /* 主任务体。正常情况不返回（靠阻塞接口退出）。
     * 如果返回了，框架按"自愿退出"处理：调用 exit() 后进入 STOPPED。 */
    void (*task)(app_ctx_t *ctx);

    uint32_t flags;
} app_desc_t;

#define APP_FLAG_NONE 0u
/* 由 fw_boot() 直接拉起，不经过 0 号 App 的配额与调度。0 号自己用这个标志。 */
#define APP_FLAG_SYSTEM (1u << 0)

/* ---- 身份 ---- */
app_id_t app_id(const app_ctx_t *ctx);

/* ---- 阻塞 ----

 * 伪阻塞延时。内部按切片等待，每片都能响应停止；被停止时直接进入退出流程，
 * 也就是"这个函数可能不返回"。不要写成 `if (app_delay(x) != CM_OK) {...}`——
 * 走到退出流程时那行代码根本不会执行。 */

cm_err_t app_delay(uint32_t ms);

/* ---- 内存 ----
 * 统一走框架，便于将来做配额与统计。App 不得直接调用 malloc（门禁可加规则）。 */
void *app_alloc(cm_size n);
void app_free(void *p);

#include "log.h"

#endif /* API_APP_H */
