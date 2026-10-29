/*
 * 系统 App 可见的 App 机制面。
 *
 * 只有系统 App 可以 include 这个头（0 号 manager 之类）。普通 App 通过
 * api.h 拿不到这里的任何符号——门禁会拦。
 *
 * 为什么这些不放在 api/app.h 里：
 *   - app_run_exit() 能让当前 App 立刻结束，这是"管理"能力，不是"被管理"能力。
 *     一旦对 AI 可见，它会变成"我处理不了的错误就让整个 App 重启"的逃生门。
 *   - app_cancelled() 一旦可见，App 就会开始写
 *       if (app_cancelled()) break;
 *     于是每个 App 各自维护一份退出逻辑，伪阻塞的保证（框架一处保证）
 *     退化成"N 个作者各自记得做"。
 *
 * 这些函数的实现在 framework/src/fw_app.c —— 声明放 api/ 是因为"契约由 api 定义"，
 * 不是因为实现属应用层。若把声明放进 apps/_system/，framework 就得反向 include
 * 应用层目录，依赖方向会反过来。
 */
#ifndef API_SYS_APP_SYS_H
#define API_SYS_APP_SYS_H

#include "cm_types.h"
#include "app.h"

/* App 的生命周期状态。EMPTY 表示"注册了但没启动过"。
 *
 * 注意 STOPPED 的含义是"已经决定结束、任务即将退出"，不是"已完全退出"——
 * 确认任务真的不在了需要平台支持，当前平台契约没有这个能力（见 app-model 已知缺口）。 */
typedef enum app_state {
    APP_STATE_EMPTY = 0,
    APP_STATE_INIT,
    APP_STATE_RUNNING,
    APP_STATE_STOPPING,
    APP_STATE_STOPPED,
    APP_STATE_FAILED,
} app_state_t;

app_state_t app_state(const app_ctx_t *ctx);
const char *app_state_str(app_state_t s);

/* 是否收到了停止请求。
 *
 * 只给两类代码用：① 框架自己的阻塞点；② 系统 App 在启动他人的间隙做检查。
 * 普通 App 不该用——它不需要知道，阻塞接口会代替它处理。 */
int app_cancelled(void);

/* 立刻走当前 App 的退出流程（调 exit() → 置终态 → 结束任务），**不返回**。
 *
 * 用在：框架的阻塞点发现取消位之后。系统 App 也可以在自己的清理流程里用它。
 * 调用后不要写任何代码，编译器会因为 noreturn 报错，这是故意的。 */
void app_run_exit(void);

#endif /* API_SYS_APP_SYS_H */
