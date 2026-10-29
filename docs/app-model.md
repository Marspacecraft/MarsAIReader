# App 模型（v0.4）

**它回答什么问题**：一个 App 由什么构成？谁让它运行、谁让它停止？它凭什么能写直线代码？

对应实现：`api/include/app.h`（App 可见）、`api/include/sys/app_sys.h`（机制面，只给系统 App）、
`framework/include/fw_app.h`（控制面）、`framework/src/fw_app.c` / `fw_task.c`（实现）。

**本仓库不含任何 App 实现**：`apps/` 目录由产品仓库提供，其中包含各 App 自己的清单
（`fw_manifest_t`）。框架只负责合并清单、拉起 `APP_FLAG_SYSTEM` 的 App。

> 规则 8：接口的权威定义是头文件。本文只说约束、理由和失败模式。

---

## 1. 一个 App = 一个描述体 + 三个函数

```c
typedef struct app_desc {
    app_id_t id;                       /* 机器标识：1..254，全固件唯一 */
    const char *name;                  /* 给人看的名字：日志、UI */
    cm_err_t (*init)(app_ctx_t *ctx);  /* 在 App 自己的任务里跑 */
    void     (*exit)(app_ctx_t *ctx);  /* 任务结束前一定调用一次 */
    void     (*task)(app_ctx_t *ctx);  /* 主任务体 */
    uint32_t flags;
} app_desc_t;
```

**为什么 id 是 `cm_u8` 而不是字符串**：它要出现在注册表查找、将来的按 App 配额与位图里，
每次比较走 `strcmp` 是白花时间；位图又让"一共有哪些 App"变成一个 32 位整数。
代价是 id 必须手工分配且唯一——`fw_init()` 会检查重复并拒绝启动，因为重复 id 的表现是
"有时启动的是另一个 App"，属于最难查的一类 bug。人读的名字留在 `name`，日志一律打 name。

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
  带这个标志的 App 自己也能被停止。

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
  发现取消就调用内部的退出函数（`app_run_exit()`，见 §6 的机制面）——**不返回**。
- 因此 App **不需要**检查返回值、不需要写状态机、不需要轮询取消位。
- 因此 `app_delay()` **之后**的代码不一定会执行 → 清理只能写在 `exit()` 里，
  不能写在循环后面。

退出流程做四件事，顺序不能变：

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
fw_boot(manifests, count)
  └── 框架遍历所有清单，对每个带 APP_FLAG_SYSTEM 的 App 直接 fw_app_start()
        └── 系统 App 的任务体（通常就是 0 号 manager）
              └── 自己决定启动哪些业务 App：fw_app_count() / fw_app_at()
                    └── app_delay(100) + 观测状态变化
```

**框架只做一件启动决定**：`APP_FLAG_SYSTEM` 的 App 由 `fw_boot()` 直接拉起。
其余的由谁、按什么顺序拉起，是**系统 App 自己的策略**——框架不规定，也不认识
具体是谁。0 号 manager 就是"实现了这个策略的那个 App"，它不在本仓库里。

启动顺序因此是**可读的**，不需要猜：框架按清单数组顺序拉起 `APP_FLAG_SYSTEM` 的
App；业务 App 的顺序由系统 App 自己决定，写在它的代码里，同样不是散落各处。

## 5. 清单由调用方提供，可以有多份

```c
typedef struct {
    const app_desc_t *const *apps;
    uint32_t count;
} fw_manifest_t;

cm_err_t fw_init(const fw_manifest_t *manifests, uint32_t count);
```

**为什么是"清单数组 + 数量"，而不是一个全局数组**：不同 App 的作者维护各自的
清单，彼此读不到对方的目录。框架把 N 份清单合并进内部的注册表（`FW_APP_MAX`
上界），合并只发生在框架里。

这个形状是**权限判定的符号判定逼出来的**。最初的实现只有一张
`apps/app_manifest.c`，AI 加自己的 App 时必须写
`extern const app_desc_t g_app_manager;` 才能把它放进启动列表——于是 AI 的
编译单元引用了系统 App 的符号。判定器第一次跑就报：

```
SYM  apps/app_manifest.c (tag 2) 用了 g_app_manager，但没有任何实际 include 的头声明它
```

改成"每人一份清单"以后，AI 的清单里只出现自己的 App，系统 App 的清单由别人维护，
框架负责合并。**这是判定器发现的第三个结构缺陷，前两个分别导致了 id 从字符串改成
整数、以及声明从 `apps/_system/` 挪到 `api/include/sys/`。**

### 本仓库不含任何 App 实现

`apps/` 目录不在这里。产品仓库提供：每个 App 的 `init` / `exit` / `task`、它自己的
`<名字>.h`（声明 `extern const app_desc_t g_app_<名字>;`）、以及一份或多份清单。
本仓库只提供 `fw_init` / `fw_boot` 和生命周期机制。

加一个 App 的完整步骤见 `skills/ai-app-developer/SKILL.md`——那是产品仓库里的事，
不是在这里加文件。

## 6. 三个头，三种受众

| 头 | 谁能用 | 能给什么 |
|---|---|---|
| `api/app.h` | 所有 App | id、名字、伪阻塞延时、内存 |
| `api/sys/app_sys.h` | 系统 App 与测试 | App 状态、`app_cancelled()`、`app_run_exit()` |
| `framework/include/fw_app.h` | 系统 App 与测试 | 注册表、启动/停止别人 |

为什么把"机制面"单独拆出来，而不是全塞进 `api/app.h`：

- `app_run_exit()` 能让当前 App 立刻结束。对 AI 可见，它就会变成**逃生门**——
  "这个错误我处理不了，让整个 App 重启吧"。这类代码在墨水瓶上表现为反复重启、反复刷屏。
- `app_cancelled()` 一旦可见，每个 App 都会开始写 `if (app_cancelled()) break;`，
  于是"伪阻塞由框架一处保证"退化成"N 个作者各自记得做"。这正是取消机制不该暴露的原因。

**注意"伪阻塞"这件事本身仍然是公开契约**（在 `api/app.h` 里写了）：AI 必须知道
"`app_delay()` 之后的代码不一定执行"、"清理只能写在 `exit()` 里"。看不见的是**怎么实现的**，
不是**行为是什么**。

`app_sys.h` 的声明放在 `api/include/sys/` 而不是 `apps/_system/`：实现在 `framework/src/`，
声明若放进应用层目录，框架就得反向 include 应用层——依赖方向会翻过来。

同一个 `api/` 函数有两个实现家（机制型在 `framework/src/`，服务型在服务 App + 服务注册表），
但**调用方看不出区别**——见 code-structure.md §4.1。

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
5. **没有配额、没有看门狗**：这两者是 0 号的策略，而 0 号不在本仓库。框架只保证
   机制（启动、停止、状态查询），不代替策略。

## 8. 验收

本仓库里**没有生命周期测试**：删掉 `apps/` 之后，`tests/sys/test_manager.c` 的被测
对象（manager 与示例 App）一起没了。当前 `tests/sys/` 只有 `test_common.c` 与
`test_port.c`。

这是已知的覆盖空洞，不是"已经验过"。补法有两种，都要在框架内完成、不依赖任何产品
App：用 `test` 本地的 `app_desc_t` 造两个假 App（一个 SYSTEM、一个普通），断言
停止时延、`exit()` 恰好一次、重启、非法转换返回 `CM_ERR_STATE`。

历史上那次实测（在 `apps/` 还存在时）记录的事实，可供补测试时对齐：
停止一个正卡在 `app_delay(200)` 的 App 实测 ~60ms（断言 < 500ms）；`exit()` 计数
0→1→重启→再停→2；`APP_FLAG_SYSTEM` 的 App 自己也能被停；运行中再 `start` 返回
`CM_ERR_STATE`。

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
| 8 | 注册点集中在 `apps/app_manifest.c` | 一个决定点，可读、可门禁 | **已被 11 取代** |
| 9 | 不用 stub 函数表示"不支持"，用 NULL | stub 指针跨 TU 比较恒不等；stub 还制造"看起来实现了"的假象 | 定稿（platform-port.md §4.1） |
| 10 | 栈统一 4096，暂不按 App 配置 | 先消除一类随机崩溃；配置化留到有真实需求 | 暂定 |
| 11 | 清单由调用方提供，可以有多份（`fw_manifest_t` + count） | 一张全局表会让 AI 的注册单元引用系统 App 的 desc 符号（越权 + 脆弱）。这条是权限判定的符号判定抓出来的，不是设计时想到的 | 定稿 |
| 12 | 本仓库不提供任何 App 实现，`apps/` 由产品仓库提供 | 框架仓库一旦塞进示例 App，"示例"就会变成事实标准；删除示例后生命周期测试同缺，已在 §8 记录为覆盖空洞 | 定稿（用户 m01960/m01994） |
