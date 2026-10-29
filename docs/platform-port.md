# 平台层接口要求（Platform Port Contract）

> 本文件是**框架仓库**向**硬件平台实现方**提出的契约。框架不认识任何具体板子；平台包只需要实现这里的接口，不需要认识任何 App。
>
> 相关：[architecture.md](architecture.md)（三层与解耦规则）、§9 屏幕专项（待讨论）

---

## 1. 边界

| 属于平台包 | 不属于平台包 |
|---|---|
| 引脚、总线、时钟、DMA、中断 | 业务语义 |
| 上电时序、器件初始化、面板传输 | 刷新策略、页面内容 |
| 总线仲裁（哪些器件共享总线、能否并发） | 谁拥有某个功能（那在应用层） |
| 器件驱动（寄存器级） | App 生命周期 |

**`platform/` 的 include 权限是分层的**：

| 子目录 | 可以 include | 不得 include |
|---|---|---|
| `port/` | 标准 C | 任何项目内头 |
| `drivers/` | `port/` | `api/`、`framework/`、`boards/` |
| `boards/` | `port/`、`drivers/`、平台 SDK | `api/`、`framework/`、`apps/` |

反向一律禁止：`api/`、`framework/`、`apps/`、`lib/` 都不得被平台层看见。

---

## 2. 返回码

**全项目只有一个错误码枚举**：`cm_err_t`（`common/include/cm_err.h`）。平台层保留 `plat_err_t` 这个名字纯为了代码可读，它是 `cm_err_t` 的别名，不是一套独立的值：

```c
typedef cm_err_t plat_err_t;

#define PLAT_OK                 CM_OK
#define PLAT_ERR_NOT_SUPPORTED  CM_ERR_NOT_SUPPORTED
#define PLAT_ERR_INVALID_ARG    CM_ERR_INVALID
#define PLAT_ERR_NO_MEM         CM_ERR_NOMEM
#define PLAT_ERR_BUSY           CM_ERR_BUSY
#define PLAT_ERR_IO             CM_ERR_IO
#define PLAT_ERR_TIMEOUT        CM_ERR_TIMEOUT
```

为什么不分层各定义一套：

- 每跨一层就要写一个转换函数，MCU 上这些是纯开销，而转换函数的 `default:` 分支是最典型的"错误被静默吞掉"的位置。
- 错误语义本来就是全局的：`NOT_SUPPORTED` 在三个层里的含义完全一致，分别为它造三个名字只是自找映射麻烦。
- 前端（日志、上报、UI）拿到的是同一个值，不需要知道它从哪层冒上来。

`NOT_SUPPORTED` 是**一等公民**，不是错误路径的兜底。所有调用方都必须处理它。

平台层**不应**返回 `CM_ERR_UNAVAILABLE` / `CM_ERR_CANCELLED`：前者是"有能力但服务没起来"（框架层语义），后者是"被 App 停止"（取消机制语义）。平台层只管"这个操作成功没成功"。同理，平台层不认识"取消位"，取消由框架层在调用返回后统一检查。

---

## 3. ops 表：只定义"平台必须提供什么"

权威定义在 `platform/port/plat_ops.h`。本节不复制签名（复制会静默漂移，见
`docs/README.md` 规则 8），只说明每一项为什么存在、缺失时上层看到什么。

当前实现（M1：能启动、能跑通完整生命周期）：

| 项 | 必需性 | 为什么需要 |
|---|---|---|
| `mem_alloc` / `mem_free` | 硬必需 | App 与框架的所有堆分配都走这里，将来才能做配额统计 |
| `clock_ms` | 硬必需 | 超时、看门狗、日志时间戳的唯一时间源（单调即可，不必对齐墙上时间） |
| `sleep_ms` | 硬必需 | 伪阻塞延时的底座；没有它 `app_delay()` 无法实现 |
| `mutex_create` / `mutex_take` / `mutex_give` | 硬必需 | 框架的互斥机制；host 上用 pthread / Win32 mutex 实现 |
| `task_create` | 硬必需 | 每个 App 一个任务。不传优先级（架构 §3.5） |
| `task_exit_current` | 硬必需 | `app_run_exit()` 不能返回，否则 App 会继续执行退出点之后的直线代码 |
| `log_write` | 可选 | NULL 时日志静默丢弃；"没接串口"是正常状态，不该让 App 分支处理 |

将来会加（M2+，各自单独评审）：`storage_*`、`i2c_transfer`、`gpio_*`、
`power_battery` / `power_off` / `power_sleep`、`audio_in_*` / `audio_out_*`、
以及 display 相关（等 architecture.md §9 定稿）。

### 3.1 硬必需 vs 可选

- **硬必需（9 项）**：内存、时钟、延时、互斥、任务创建、任务退出。缺任何一项
  `fw_init()` 返回 `CM_ERR_NOT_SUPPORTED`，整机不启动 —— 因为框架自身跑不起来。
- **可选（其余）**：指针为 NULL 即"本平台没有这个能力"，框架继续运行，
  相关 API 返回 `CM_ERR_NOT_SUPPORTED`。

> **为什么硬必需从 6 项变成 9 项**：设计文档最初只列了"内存 / 时钟 / 互斥 / task 创建"。
> 真正写实现时才暴露出两件事躲不开：没有 `sleep_ms` 就没有伪阻塞延时，而伪阻塞是
> 取消机制的地基；没有 `task_exit_current` 就无法让 `app_run_exit()` 真正"不返回"。
> 这两项都是结构性的依赖，不能算可选。
>
> 这个变化本身要保留：设计阶段的清单是假设，实现是验算。清单改了就要改文档，
> 否则下一个平台作者会照着旧清单写，然后在 `fw_init()` 失败时找不到原因。

---

## 4. 默认实现：NULL 表示不支持

`PLAT_OPS_STUB` 展开后每个字段都是 NULL，平台包在上面覆盖自己支持的部分：

```c
/* platform/host-fake/plat_host.c 或 boards/<id>/plat_ops_impl.c */
static const plat_ops_t s_ops = {
    .mem_alloc         = host_mem_alloc,
    .mem_free          = host_mem_free,
    .clock_ms          = host_clock_ms,
    .sleep_ms          = host_sleep_ms,
    .mutex_create      = host_mutex_create,
    .mutex_take        = host_mutex_take,
    .mutex_give        = host_mutex_give,
    .task_create       = host_task_create,
    .task_exit_current = host_task_exit_current,
    .log_write         = host_log_write,   /* 可选 */
};
```

收益：**一个只实现这 9 项的最小平台就能把整机启动起来**（很多功能返回"不支持"），
移植的每一步都可验证，而不是"全部写完才能跑"。

### 4.1 为什么放弃"stub 函数"方案（已否决）

早期设计是 `PLAT_OPS_STUB` 里每项指向 `plat_stub_*`（返回 NOT_SUPPORTED），
并靠"函数指针是否等于 stub"来自检能力位。实现时否决了：

1. `static inline` 的 stub 函数在每个 TU 各有一份副本，**跨 TU 比较函数指针地址恒为不等**，
   自检形同虚设。写代码时真的踩到了这个坑。
2. 更根本的是：stub 函数"看起来像实现了"，正是"声明支持、实现为空"这类漂移的起点。

改成 NULL 之后：`PLAT_OPS_STUB` 显式列出每个字段（漏填能看出来），框架每次调用前判 NULL
并返回 `CM_ERR_NOT_SUPPORTED`，语义只剩一种，没有第二处需要解释。

### 4.2 能力位 caps：未实现（M2）

设计里 `plat_ops_t` 还应有 `uint64_t caps` 与 `cap_available()`，用于"声明支持什么"。
M1 阶段先不做：能力位要等第一个真实平台落地才有内容，现在加上就是一组空转的常量。

加的时候一并实现"caps 一致性自检"，且**必须用 NULL 语义**（`caps` 置位的能力，
其函数指针不得为 NULL），不要回到 stub 指针比较那条路。

---

## 5. 能力位

```c
#define PLAT_CAP_STORAGE   (1ull << 0)
#define PLAT_CAP_I2C       (1ull << 1)
#define PLAT_CAP_GPIO      (1ull << 2)
#define PLAT_CAP_BATTERY   (1ull << 3)
#define PLAT_CAP_SLEEP     (1ull << 4)
#define PLAT_CAP_AUDIO_OUT (1ull << 5)
#define PLAT_CAP_AUDIO_IN  (1ull << 6)
#define PLAT_CAP_DISPLAY   (1ull << 7)
```

### 5.1 能力如何传到应用层

```
app_main() → 0 号 manager → fw_init()
                                 ├─ 绑定 g_plat_ops
                                 ├─ 一致性自检（§4.1）
                                 └─ 读 caps 生成运行时能力表
                            → manager 启动各 App
app → cap_available(PLAT_CAP_AUDIO_IN) → bool
```

**为什么必须有这一步**：如果只能"调了失败再说"，用户点了语音按钮才发现没有麦克风 —— 这是 UI 灾难。能力必须**在渲染之前**可查。

约束：

- App 使用任何可选能力前**必须**先查 `cap_available()`；
- 能力缺失时，App 必须**隐藏对应入口**，不是弹错误；
- 同一能力在运行期不得变化（不做热插拔能力）。

---

## 6. 平台包交付要求

一个平台包必须提供：

| 项目 | 形式 | 说明 |
|---|---|---|
| ops 实例 | `const plat_ops_t g_plat_ops` | 唯一入口符号，由平台包定义 |
| port 版本 | `uint32_t plat_port_version(void)` | 与框架声明的主版本一致，否则 `fw_init()` 失败 |
| 板级描述 | 平台包内部的 manifest / 头文件 | 引脚、总线拓扑、器件、时序参数；**不进入框架仓库** |
| 初始化 | 可放在 `g_plat_ops` 首次使用前完成，或提供 `plat_boot()` 由 `fw_init()` 调用 | 顺序：总线 → 器件 → 中断 |

### 6.1 平台包**可以**做的

- 建自己的 task（如 DMA 泵、总线仲裁者）—— 这是平台内部事务；
- 在 ISR / 回调里调 `fw_event_post_isr()` 上报事件；
- 内部实现总线仲裁、器件会话锁。

### 6.2 平台包**不可以**做的

- 调用任何 App 的函数，或 include `apps/*`、`api/*`；
- 调 framework 除 `fw_event_post` / `fw_event_post_isr` 以外的函数；
- 在 port 函数里做业务判断（"这一页该不该重刷"不是平台的事）；
- 用 `#ifdef BOARD_XXX` 在共享代码里分支 —— 差异进板级描述，不进公共代码。

---

## 7. 任务数量的判据（给平台实现者）

> **这个外设有没有「排他性的长时序状态」？有才建 task。**

- 有（并行刷屏、音频播放、连接状态机、需要时间窗的手势识别）→ owner task + 提交队列；
- 没有（纯 I2C 寄存器读写、RTC 读数、PWM 占空比）→ 库函数 + 内部锁，**不要建 task**。

反例：给每个 I2C 器件建一个 task，会得到十几个 3~8KB 栈的任务，内部 RAM 迅速耗尽，而它们全都在等同一个总线。

---

## 8. 参考平台：host-fake

框架仓库自带 `tests/host-fake/`，实现 `g_plat_ops`：

- 硬必需项：真的用 `malloc` / 单调时钟 / `pthread` mutex；
- 可选能力：默认全部缺失（正好用来测"能力不存在时 App 是否优雅降级"）；
- 可按测试需要打开某一项，验证"有这项能力时"的路径。

这使**框架层与应用层的公共逻辑都能在 PC 上测**，不必烧板子。

---

## 9. 平台移植的验收顺序

按这个顺序做，每一步都可独立验证，不会出现"全部写完才知道行不行"：

1. 9 项硬必需 → 整机能否启动、manager 能否拉起 App；
2. storage → 能否读文件；
3. i2c / gpio → 能否读到一个器件 ID；
4. battery / sleep → 电量与休眠；
5. audio out / in → 播放与采集；
6. display → **单独讨论，见 architecture.md §9**。

每一步失败都只应影响该能力，不应导致整机不可启动 —— 这就是"默认实现返回失败"的价值。
