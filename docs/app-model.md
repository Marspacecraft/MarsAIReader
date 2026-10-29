# App 模型

**它回答什么问题**：一个 App 由什么构成？谁让它运行、谁让它停止？它凭什么能写直线代码？

对应实现：`api/include/app.h`（App 可见）、`framework/include/fw_app.h`（控制面）、
`framework/src/fw_app.c` / `fw_task.c`（实现）、`apps/app_manifest.c`（注册点）。

> 规则 8：接口的权威定义是头文件。本文只说约束、理由和失败模式。

---

## 1. 一个 App = 一个描述体 + 三个函数

```c
typedef struct app_desc {
    const char *id;                    /* 稳定标识 */
    const char *name;                  /* 给人看的名字 */
    cm_err_t (*init)(app_ctx_t *ctx);  /* 在 App 自己的任务里跑 */
    void     (*exit)(app_ctx_t *ctx);  /* 任务结束前一定调用一次 */
    void     (*task)(app_ctx_t *ctx);  /* 主任务体 */
    uint32_t flags;
} app_desc_t;
```

三条设计取舍：

1. **没有"创建"和"销毁"回调，只有 init/exit**。区别在于：exit 在**取消路径上也会被调用**，
   所以它必须能处理"init 只做了一半"的状态。多一个 destroy 只会让作者以为可以只在正常路径清理。
2. **没有 `start` 参数、没有返回值传递**。App 不给别人返回值——它通过服务注册表暴露能力，
   通过状态位暴露死活。这样"启动失败"只有一种表现：`FAILED`。
3. **`ctx` 是不透明的**。App 不能读它的字段，因此框架可以自由改内部布局（现在里面只有
   cancel 与 state 两个原子量，将来会加资源登记表）。

## 2. 生命周期

```
EMPTY --start--> INIT --init成功--> RUNNING --request_stop--> STOPPING --> STOPPED
                   |                   |                                    |
                   +--init失败--> FAILED +--task返回（自愿退出）------------+
                                                                          |
                                                STOPPED --start--> INIT   （可重启）
```

规则：

- `EMPTY` / `STOPPED` 是**唯一**允许启动的两个状态，其他状态 `fw_app_start()` 返回 `CM_ERR_STATE`。
- `exit()` 在**任何**结束路径上都会被调用恰好一次，包括 `FAILED`。
- `FAILED` 不自动重启。单 MCU 上"重启 App"最多是"重建它的任务"，失败循环的代价
  （电量、日志、可能的硬件半初始化状态）比失败本身更大。重启策略属于 0 号的策略层，
  现在没有实现，也不打算在这一版实现。
- `SYSTEM` 标志只决定"谁先被启动"（`fw_boot()` 直接拉起），**不是特权等级**：
  0 号自己也能被停止，测试里就是这么验的。

## 3. 伪阻塞契约（整个模型的地基）

App 写直线代码：

```c
static void hello_task(app_ctx_t *ctx)
{
    for (;;) {
        do_work();
        app_delay(200);   /* 被停止时这里直接退出任务，不返回 */
    }
}
```

- 框架提供的**每一个**会阻塞的接口都是伪阻塞的：内部按切片等待，每片检查取消位，
  发现取消就调用 `app_run_exit()` —— **不返回**。
- 因此 App **不需要**检查返回值、不需要写状态机、不需要轮询 `app_cancelled()`。
- 因此 `app_delay()` **之后**的代码不一定会执行 → 清理只能写在 `exit()` 里，
  不能写在循环后面。

`app_run_exit()` 做四件事，顺序不能变：

1. 清零取消位（= 在 `exit()` 期间禁用取消，否则清理被反复打断，永远结束不了）；
2. 调用 `desc->exit(ctx)`；
3. 置终态（`STOPPED` / `FAILED`）；
4. 结束当前任务，不返回。

**为什么不是"检查返回值"模型**：那样每个调用点都要写 `if (r == CM_ERR_CANCELLED) return;`，
漏一个就变成"停止请求发了但 App 还在跑"。伪阻塞把这件事收敛到框架一处，
App 作者想漏也漏不掉。

**面积代价**：App 一旦进入框架接口就再也回不到"我要自己收尾"的位置，所以
`exit()` 必须能看到并清理它需要的一切。当前的做法是"App 自己记住自己开了什么"；
资源自动登记（`res_track`）还没做，见 §7。

## 4. 谁启动谁

```
fw_boot(manifest)
  └── 对每个 APP_FLAG_SYSTEM 的 App 调 fw_app_start()   ← 只有 0 号
        └── 0 号 manager 的任务体：遍历注册表，启动所有非 SYSTEM App
              └── app_delay(100) + 观测状态变化
```

启动顺序只有一个决定点（`apps/app_manifest.c` 的数组顺序），不分散在各处。
"谁先起来"因此可以被读出来，而不是靠猜。

## 5. 注册点只有一个

加一个 App：

1. 在 `apps/<分类>/<id>/` 下实现 `init` / `exit` / `task`；
2. 定义 `const app_desc_t g_app_<id>`；
3. `apps/app_manifest.c` 里加一行 `extern` + 数组里加一项。

三处都在 `apps/` 内。**如果加一个 App 必须动 `framework/`，说明框架漏了能力**——
这是门禁之外的一条人工判断，但方向很明确。

## 6. 两个头，两种受众

| 头 | 谁能用 | 能给什么 |
|---|---|---|
| `api/app.h` | 所有 App | 自己的状态、伪阻塞延时、退出、内存 |
| `framework/include/fw_app.h` | `apps/_system/` 与测试 | 看别人的状态、启动/停止别人、注册表 |

「启动/停止别人」是特权，所以它不在 `api/` 里。普通 App 的 include 白名单里没有
`framework/`，`tools/check_purity.py` 会拦。

同一个 `api/` 函数有两个实现家（机制型在 `framework/src/`，服务型在
`apps/_system/*` + 服务注册表），但**调用方看不出区别**——见 code-structure.md §4.1。

## 7. 已知缺口（都记着，不假装没有）

1. **`STOPPED` 是"即将结束"而不是"已经结束"**。平台没有 join 语义，所以观测到 `STOPPED`
   后立刻 `start` 理论上能出现两个任务。当前用"轮询间隔 + 重启前让出一次"压在毫秒级。
   要彻底消除需要 port 增加 `task_join`，代价是四家 RTOS 语义不一致。
2. **没有任务级隔离**。"强制回收"最多是"标记 → 停止投递 → 重建任务 → 不行就重启整机"。
   这一句必须原样进 skill，不能让 AI 以为有 Linux 级隔离。
3. **`res_track` 未实现**：App 必须自己记住开了哪些资源并在 `exit()` 里关掉。漏了的后果是
   句柄泄漏，而不是"框架帮你收"。
4. **栈大小固定 4096**，所有 App 一样。按 App 配置栈是下一步，先统一是为了避免
   "某个 App 栈溢出"变成随机崩溃。
5. **没有配额、没有看门狗**：0 号现在只观测，不管制。

## 8. 验收

`tests/sys/test_manager.c` 在 host-fake 平台上真开线程跑：

- 注册表可初始化；0 号被 `fw_boot` 拉起；0 号拉起 hello；
- 停止 hello 时它正卡在 `app_delay(200)` 里，实测停止耗时 ~60ms（< 500ms 断言）；
- `exit()` 计数从 0→1，重启后再停 → 2（证明取消位清零顺序对）；
- 0 号自己也能被停（SYSTEM 不是特权）；
- 运行中再 `start` 返回 `CM_ERR_STATE`。

运行：`python tools/run_tests.py test_manager`。

## 9. 决定记录

| # | 决定 | 理由 | 状态 |
|---|---|---|---|
| 1 | App = 模块 + 主任务 + 描述体，不是独立进程 | 单 MCU 没有地址空间隔离，进程模型是假的 | 定稿 |
| 2 | 只留 init / exit / task 三个回调 | exit 覆盖所有结束路径，避免作者只在正常路径清理 | 定稿 |
| 3 | 阻塞接口全部伪阻塞，App 不检查返回值 | 把"停不下来"的失败收敛到框架一处 | 定稿（用户 m00121） |
| 4 | `EXIT` 顺序：清零取消位 → exit() → 置终态 → 结束任务 | 顺序错会导致"清理被打断"或"重启立刻又退出" | 定稿 |
| 5 | `FAILED` 不自动重启 | 失败循环的代价大于失败本身；策略属 0 号 | 暂定 |
| 6 | 只允许从 `EMPTY`/`STOPPED` 启动 | 防止重入与双任务；其余状态返回 `CM_ERR_STATE` | 定稿 |
| 7 | `SYSTEM` = 启动顺序，不是特权 | 0 号也会被停；权限不该靠一个 flag 隐式表达 | 定稿 |
| 8 | 注册点集中在 `apps/app_manifest.c` | 一个决定点，可读、可门禁 | 定稿 |
| 9 | 不用 stub 函数表示"不支持"，用 NULL | stub 指针跨 TU 比较恒不等；stub 还制造"看起来实现了"的假象 | 定稿（platform-port.md §4.1） |
| 10 | 栈统一 4096，暂不按 App 配置 | 先消除一类随机崩溃；配置化留到有真实需求 | 暂定 |
