# 代码结构（v0.3）

> 目的：先定"哪里放什么"，再定接口细节。
> 相关：[architecture.md](architecture.md) 三层与解耦规则、[platform-port.md](platform-port.md) 平台契约

---

## 1. 顶层

```
MarsAIReader/
├─ docs/
│   ├─ README.md             文档纪律（八条规则）
│   ├─ architecture.md       三层、解耦、伪阻塞总纲
│   ├─ app-model.md          App 结构体、生命周期、谁启动谁
│   ├─ platform-port.md      平台契约与移植顺序
│   ├─ code-structure.md     本文件
│   └─ generated/            生成物（能力目录、依赖图），入库但由工具产出
├─ common/                   公共基础库 —— 任何层可 include，自身不依赖项目内任何头
│   ├─ include/              cm_port / cm_types / cm_err / cm_atomic / cm_list / cm_ring / cm_bits / cm_crc
│   └─ src/                  少数需要 .c 的实现（字符串表、CRC）
├─ api/                      应用层契约 —— 只有声明，没有实现
│   └─ include/              api.h（总入口）/ app.h / log.h
├─ framework/                框架层 —— 机制，不含业务
│   ├─ include/              控制面：fw_app.h（能启动/停止别人）
│   └─ src/                  fw_app.c（生命周期）/ fw_task.c（任务与 TLS）/ fw_log.c / fw_internal.h
├─ lib/                      应用层公共纯逻辑 —— 无硬件依赖，可 host 测
├─ apps/                     应用层实现
│   ├─ app_manifest.c/.h     唯一的 App 注册点
│   ├─ _system/              0/1/2 号系统 App + 服务宿主
│   │   └─ manager/          0 号：启动其他 App、观测状态变化
│   └─ demo/hello/           示例 App —— 给 AI 照抄的最小骨架
├─ platform/                 平台层
│   ├─ port/                 平台契约头（plat_ops.h）
│   ├─ drivers/              公共器件驱动（多平台共用）
│   ├─ boards/               按平台分目录（引脚、时序、板级组装）
│   ├─ host-fake/            host 参考平台（真线程、无硬件）
│   ├─ template/             新建平台包的脚手架
│   └─ README.md             如何接一块新板
├─ skills/                   给 AI agent 的工作流 + 硬约束
├─ tools/                    门禁脚本、生成器（纯 Python，三平台同一份）
└─ tests/                    host 单测（不经过 IDF）
    └─ sys/                  一个文件一个可执行文件；tinytest.h 提供用例表
```

### 1.1 落地状态

树里有些目录还是空的。写清楚，免得看文档的人以为已经有了：

| 状态 | 内容 |
|---|---|
| 已实现并跑通 host 测试 | `common/`、`api/`（app + log）、`framework/`（生命周期 + 任务 + 日志）、`apps/`（0 号 + hello）、`platform/port/`、`platform/host-fake/`、`tools/`、`tests/sys/` |
| 目录已建、内容待填 | `lib/`、`skills/`、`docs/generated/` |
| 还没建 | `platform/drivers/`、`platform/boards/`、`platform/template/` |
| 接口未定 | display / input / audio / stor / net / power / res / cap（见 architecture.md §9、§10） |

---

## 2. 依赖规则（这是本设计真正的约束）

| 目录 | 可以 include | 不得 include |
|---|---|---|
| `common/` | 标准 C | **任何**项目内目录 |
| `api/` | 标准 C、`common/`、`api/` | framework / platform / apps / lib |
| `lib/` | `api/`、`common/`、`lib/`、标准 C | framework / platform / apps |
| `framework/` | `api/`、`common/`、`framework/`、`platform/port/` | `apps/`、`platform/` 的实现 |
| `apps/`（AI 写） | `api/`、`lib/`、`common/`、自己 | framework / platform / 别的 app |
| `apps/_system/`（系统） | 上面 + `framework/include/fw_*.h` | 别的 app 的内部头 |
| `platform/` | `platform/port/`、`common/`、平台自有 SDK | api / framework / apps / lib |
| `tests/` | 全部（白盒） | — |

三条由此推出的铁律：

1. **`api/` 是唯一横向通道。** 两个 App 之间不直接链接符号，只能通过 `api/` 的 C 函数。
2. **`platform/` 不认识业务。** 它只 include `platform/port/`，连 `api/` 都看不见。
3. **`framework/` 不认识具体 App。** 它通过服务注册表拿到函数指针，不 include `apps/`。

> 权限差异是刻意的：AI 写的 App 连 `framework/include` 都拿不到，只能看 `api/`。这与"AI 不得修改应用层以外的代码"是同一件事的两面。

### 2.1 `common/` —— 唯一的"人人可用"目录

`common/` 是依赖图里唯一向所有层开放的目录，因为它**不含任何策略**，只有原语：

| 头文件 | 内容 | 为什么必须有 |
|---|---|---|
| `cm_port.h` | **标准 C 兼容层**：C11 版本闸门、`CM_INLINE`/`CM_NORETURN`/`CM_RESTRICT`/`CM_STATIC_ASSERT` 等宏 | 工具链差异只在这一处收敛，上层永远只写标准 C |
| `cm_types.h` | 定宽别名、`CM_ARRAY_SIZE`/`CM_MIN`/`CM_CONTAINER_OF`/`CM_ALIGN_UP` | 这些宏每个文件都在用，各写一份就会各写错一次 |
| `cm_err.h` | **全项目唯一的错误码枚举** | 分层各自定义错误码 → 每次跨层调用都要转换，转换函数默认值会吞掉错误 |
| `cm_atomic.h` | `cm_atomic_i32/u32/ptr` + 内存序 | 不用 `<stdatomic.h>`：交叉工具链支持不一致；用 `__atomic_*` 内建保证各目标行为一致 |
| `cm_list.h` | 侵入式双向链表 | MCU 上不能为节点再 malloc 一层；节点本来就嵌在结构体里 |
| `cm_ring.h` | 单生产者单消费者无锁字节环 | ISR→任务、DMA→任务、AT 响应流全靠它，是最容易写出竞态的地方 |
| `cm_bits.h` | popcount/clz/ctz/next_pow2/位图 | 1bpp 帧缓冲和位图分配反复用到，手写是 off-by-one 高发区 |
| `cm_crc.h` | CRC-16/CCITT-FALSE、CRC-32/ISO-HDLC（增量式） | 书库文件、字体包、NVS 记录、AT 帧都需要，算法口径必须统一 |

`common/` 的四条禁令：

1. **不分配内存、不阻塞、不关中断、不打日志**（打日志要依赖 `api/log.h`，那就成环了）。
2. **没有需要初始化的全局状态**（没有 `cm_init()`）。有状态的库不是原语，是服务。
3. **不出现业务名词**（没有 `page`、`book`、`asr`）。
4. **不 include 任何项目内头文件**——这是它同时向所有层开放的前提。

命名一律 `cm_` 前缀，避免与 ESP-IDF / FreeRTOS 的 `List_t`、`esp_err_t` 等撞名。

原子操作刻意用结构体包装而不是裸 `int32_t`：让"这是原子变量"在代码里一眼可见，防止有人对普通 `int` 做 `++` 然后以为它是原子的。

验收方式：`python tools/run_tests.py`，只依赖 `cc/gcc/clang`，不经过 IDF。

---

### 2.2 纯 C 边界 —— 五个目录不认识目标 SDK

`common/` `api/` `lib/` `framework/` `apps/` 这五个目录**只能是标准 C11**：

- 不 include 任何目标 SDK / RTOS / 图形库头文件；
- 不出现它们的标识符（任务创建、延时、日志、中断属性等）；
- 不写 `#ifdef 某芯片` / `CONFIG_*_TARGET` 之类的条件编译。

等价判据（这是最容易执行的一条）：**把它们和 `platform/host-fake/` 一起编到 PC 上，必须能编译并跑通单测。** 编不过，说明依赖漏进来了。

编译器与标准库之间的差异全部收敛在 `cm_port.h`：

| 宏 | 作用 | 为什么需要它 |
|---|---|---|
| `CM_INLINE` | `static inline` | 头文件里的函数不产生外部符号，多个 TU 都 include 也不冲突 |
| `CM_NORETURN` | `__attribute__((noreturn))` | 标准 C11 的 `_Noreturn` 只能前置；`void f(void) CM_NORETURN;` 用关键字写会语法错误 |
| `CM_RESTRICT` / `CM_ALIGNOF` / `CM_ALIGNAS` | 标准关键字别名 | 上层不必记关键字拼写，换工具链也不用改代码 |
| `CM_LIKELY` / `CM_UNLIKELY` | 分支预测提示 | 热路径判空用得上；非 GCC 系工具链直接 `#error`，不假装支持 |
| `CM_STATIC_ASSERT` | `_Static_assert` | "假设 u32 是 4 字节"这类错误在编译期就挡住 |
| `CM_PACKED` / `CM_WEAK` / `CM_DEPRECATED` / `CM_PRINTF_FMT` | 属性 | 协议结构体、可替换钩子、格式化参数检查 |

**这条规则不靠自觉，靠门禁**：`python tools/check_purity.py`（见 §10）。它扫这五个目录，命中 SDK 头、SDK 标识符、或非法跨层 include 就返回非 0。

> 为什么必须做成门禁：规则只写在文档里，第一个赶工期的人就会写下 `#include "esp_log.h"`，从此整套代码永久绑死在一个 SDK 上。编译器不会因此报错——它只会在换平台那天一次性爆炸，而那时改动量已经不可收拾。

---

## 3. `api/` —— 应用层可见的全部接口

**是的：`api/` 就是 App 能看到的全部符号。** 没有第二处。

这条同时是个闭合性检查：如果一个 App 需要某个能力而 `api/` 里没有，只有两种可能——要么 `api/` 漏了，要么这个 App 在做不该它做的事。不存在"我自己绕过 api 直接调"这条路。

```
api/
├─ api.h         汇总 + 版本 + 编译期能力开关
├─ types.h       公共类型：句柄、错误码、取消语义
├─ app.h         App 骨架：ctx、on_exit、app_delay
├─ cap.h         cap_available()
├─ res.h         资源登记与申请（res_track / res_acquire / res_release）
├─ disp.h        屏幕
├─ input.h       按键 / 触摸 / 手势
├─ audio.h       播放 / TTS
├─ stor.h        文件
├─ net.h         连接与传输
├─ power.h       电量 / 休眠
└─ log.h
```

**只有声明**，没有实现，也没有 `static inline` 业务逻辑（纯类型辅助宏除外）。

### 3.1 使用面 vs 控制面

同一个能力会有两个头，分属两侧，**不能互相替代**：

| 能力 | `api/`（使用面，App 可见） | `framework/include/`（控制面，仅 `_system/` 可见） |
|---|---|---|
| App | `app_delay`、`ctx`、`on_exit` | `fw_app_register`、`fw_app_stop`、取消位置位 |
| 资源 | `res_track`、`res_acquire` | `fw_res_set_quota`、`fw_res_revoke`、用量查询 |
| 服务 | `disp_commit` | `fw_svc_register`、`fw_svc_pending` |

判据：**问"我能不能强制别人"——能，是控制面；只是"我想做某事"——是使用面。**

AI 写的 App 只拿到左列。右列能停别人的任务、改别人的配额，所以只给 0 号与服务宿主。

每个头里，每个函数一行注释必须写清 **实现者是 framework 还是哪个 service**（见 §4）。这一行是能力目录的生成源。


---

## 4. 一个 API，两个实现家

这是本设计最容易搞错的地方，必须先说清。

| 类别 | 例子 | 实现位置 | 原因 |
|---|---|---|---|
| **机制型** | `stor_open`、`res_acquire`、`app_delay`、`cap_available` | `framework/src/` | 需要在阻塞点检查取消位、需要记账 |
| **服务型** | `disp_commit`、`audio_play`、`input_poll` | `apps/_system/*` + 注册表 | 有独占硬件、有长时序状态、需要活对象 |

服务型的调用链：

```
app → disp_commit()                      [framework/src/dispatch/ 里的转发层]
        ├─ 查服务是否已注册；未注册 → NOT_SUPPORTED
        ├─ 配额/超时检查
        ├─ 调 svc->ops->commit(...)       [函数指针，指向 display App]
        └─ 返回前检查取消位 → 若已取消，走退出路径
```

**所有 `api/` 函数都经过转发层，哪怕只有三行。** 理由：取消语义、配额语义必须有唯一落点；否则每个 App 都要自己写检查，等于没有约束。

### 4.1 服务注册表的契约（已定）

```c
/* framework/include/fw_svc.h —— 控制面，只有服务 App 调 */
typedef struct {
    disp_err_t (*commit)(const page_t *page);
    void       (*cancel)(disp_token_t tok);
} disp_ops_t;

void fw_svc_register(svc_id_t id, const void *ops);   /* 在服务 App 的 init() 里 */
bool fw_svc_ready(svc_id_t id);
```

```c
/* framework/src/dispatch/disp.c —— 转发层，三行也写 */
disp_err_t disp_commit(const page_t *page) {
    if (!fw_svc_ready(SVC_DISP))            return DISP_ERR_NOT_SUPPORTED;
    if (fw_app_state_of(SVC_DISP) != APP_RUNNING) return DISP_ERR_UNAVAILABLE;
    disp_err_t r = disp_ops->commit(page);
    if (app_cancelled()) app_run_exit();    /* §3.1：不返回 */
    return r;
}
```

三种状态必须分开，**不能混成一个错误码**：

| 状态 | 返回 | 上层该做什么 |
|---|---|---|
| 服务未注册（板上没这能力 / 未启用） | `NOT_SUPPORTED` | 隐藏入口，不提示错误 |
| 已注册但 App `FAILED`（服务崩了） | `UNAVAILABLE` | 显示"暂不可用"，可重试 |
| `RUNNING` | 正常结果 | — |

注册规则：

1. **只注册一次**。重复注册立即报错并拒绝——否则两个 App 抢同一个服务，谁生效取决于启动顺序。
2. **`ops` 必须是 `const`，注册后不可替换**。运行期换实现会让"上一次调用和这一次调用不是同一个东西"。
3. **注册发生在服务 App 的 `init()` 里**，早于任何调用方启动（`requires` 拓扑序保证）。因此正常的 `NOT_SUPPORTED` 只代表"这块板没有这个能力"，不代表"还没起来"。


---

## 5. `framework/` —— 只有机制

```
framework/
├─ include/           fw_*.h：控制面，只给 _system/ 用
│   ├─ fw_app.h       App 注册、生命周期、取消位、退出路径
│   ├─ fw_svc.h       服务注册表
│   ├─ fw_res.h       记账与限额（对应 res_track / res_acquire）
│   ├─ fw_lock.h      逻辑层锁与租约
│   ├─ fw_event.h     事件总线
│   └─ fw_init.h      fw_init() / 版本校验 / caps 自检
├─ src/
│   ├─ app/           状态机、取消位、退出路径、重启
│   ├─ dispatch/      每个 api 函数一个转发（薄，但存在）
│   ├─ res/           记账、ctx 槽位、配额
│   ├─ lock/          逻辑锁、租约、三层冲突的可见层
│   ├─ event/         队列与事件（队列实例属于使用者，不属框架）
│   ├─ svc/           注册表
│   └─ cap/           能力表 + 声明/事实一致性自检
└─ port/              不在这里 —— 见 platform/port/
```

**框架里不许出现的东西**：

- `while(1)` 的任务体、`xTaskCreate`、定时器实例 → 框架没有运行权（§架构 2）
- 任何业务名词（阅读、页面、书架、对话）
- `#ifdef CONFIG_xxx_BOARD` 之类的板级分支
- 对具体器件的直接访问（只经 `plat_*`）

判据：**把 `framework/` 编译到 PC 上、只链接 `platform/host-fake`，必须能跑起来并跑通单测。** 跑不起来说明机制里混进了硬件或业务。

---

## 6. `lib/` —— 应用层公共纯逻辑

```
lib/
├─ layout/       分页、断行、禁滚动区排版
├─ text/         编码、字体度量、断词
└─ epub/         容器解析、章节切分
```

规则：只依赖 `api/` 与标准 C，**不依赖 framework、不开任务、不碰 IO**。因此可以在 PC 上跑全量单测。

> 与 `framework/` 的区别：framework 是"谁能运行、资源怎么分"；lib 是"算出来的结果对不对"。前者要烧板子验证，后者不需要。

---

## 7. `apps/`

```
apps/
├─ _system/
│   ├─ manager/     0 号：引导、依赖排序、配额预检、监控、停止
│   ├─ display/     显示服务：唯一持有面板句柄 + 刷新计划（屏幕专项另议）
│   ├─ input/       输入服务：按键 / 触摸 / 手势
│   ├─ audio/       音频服务：跨 I2S/UART/GPIO 的会话状态机
│   └─ net/         网络服务：连接、重连、背压
└─ reader/          AI 可写的第一个 App（同时是样板）
```

每个 App 目录固定形态：

```
apps/reader/
├─ app.yaml       manifest：id、栈、psram、requires、services
├─ app.c          入口 + 主任务 + on_exit
├─ ctx.h          本 App 的资源槽位（伪阻塞退出靠它回收）
└─ ui/            页面（纯布局，调用 lib/layout）
```

`_system/` 与普通 App 的差别**只在两处**：能否 include `framework/include`、能否被 `tools/` 的路径门禁排除。**没有第三处**。

---

## 8. `platform/` —— 契约 + 共享驱动 + 按板组装

```
platform/
├─ port/                     plat_*.h，契约，只有声明
│
├─ drivers/                  公共器件驱动：多平台共用
│   ├─ ft6336u/              触摸
│   ├─ bq27220/              电量计
│   ├─ tps65185/             墨水屏 PMIC
│   ├─ pcf8563/              RTC
│   ├─ tca9555/              IO 扩展
│   └─ other/
│
├─ boards/                   按平台分目录：一份完整平台实现
│   ├─ esp32s31-metalio/
│   │   ├─ board.yaml        引脚、总线拓扑、器件地址、上电时序参数
│   │   ├─ plat_ops_impl.c   g_plat_ops 实例
│   │   ├─ board_init.c      总线 → 器件 → 中断的初始化顺序
│   │   └─ panel_ed047tc2.c  面板专用传输（并行 8bit）
│   └─ <next-board>/
│
├─ host-fake/                参考实现：硬必需用真 malloc/pthread，可选能力全缺
├─ template/                 新板脚手架：g_plat_ops 全填 STUB + TODO 清单
└─ README.md                 移植步骤与验收顺序
```

### 8.1 公共驱动 vs 板级目录

| | `drivers/<器件>/` | `boards/<平台>/` |
|---|---|---|
| 放什么 | 寄存器级读写、内部锁、错误处理 | 引脚、地址、时序参数、初始化顺序 |
| 依赖 | 只依赖 `port/` 的总线函数（`i2c_transfer`、`gpio_*`） | `port/` + `drivers/` |
| 可复用 | 多平台共用 | 不共用 |

**驱动不写死引脚和地址**：全部经配置结构体注入。

```c
/* drivers/ft6336u/ft6336u.h */
typedef struct {
    int      bus;        /* 哪条 I2C */
    uint8_t  addr;       /* 0x38 */
    int      pin_int;    /* -1 表示轮询 */
    int      pin_rst;
} ft6336u_cfg_t;

plat_err_t ft6336u_init(const ft6336u_cfg_t *cfg);
plat_err_t ft6336u_read(const ft6336u_cfg_t *cfg, touch_point_t *out);
```

板级目录只负责"填这张配置表"。这是公共驱动能跨平台复用的唯一办法，也是避免 `#ifdef BOARD_XXX` 蔓延的唯一办法——**差异是数据，不是条件编译**。

### 8.2 某块板没有的驱动

两种处理，按情况选：

- **能力不存在**（板上就没有这个器件）→ `g_plat_ops` 对应项保持 STUB，`caps` 不置位。上层看到 `NOT_SUPPORTED`，自己降级。
- **器件相同但接法特殊** → 在 `boards/<平台>/` 里放该板的实现，覆盖公共驱动（同名符号由构建顺序选择）。不去改 `drivers/` 里的公共代码。

### 8.3 平台层的三条禁令

1. `drivers/` 与 `boards/` **不得 include `api/` 或 `framework/`**——否则驱动开始认识业务，复用立刻失效。
2. **框架不依赖任何具体驱动或板**。删掉整个 `drivers/` 和 `boards/`，框架仍应能编译、能链 `host-fake` 跑通单测。
3. `boards/` 里不得出现对 App 的引用，也不得知道"这一页该不该重刷"——策略不是平台的事。

> 这三条把"仓库里放了一堆硬件代码"和"框架不针对硬件"这两件事分开了：硬件代码是**可选资产**，框架的编译与测试**不经过它们**。


---

## 9. 构建

| 目标 | 入口 | 用途 |
|---|---|---|
| 固件 | 根 `CMakeLists.txt`（IDF 工程） | `idf.py build` |
| host 单测 | `python tools/run_tests.py` | 逐个编译 `tests/sys/test_*.c` 并运行，**不经过 IDF** |
| 单个用例 | `python tools/run_tests.py test_common list_ops` | 只跑一个用例；`--list` 列出用例名 |

host 构建必须能覆盖 `framework/` 与 `lib/` 的全部逻辑。覆盖不到的代码 = 只能在板子上调 = 不该存在（架构 §6 同源判据）。

**`tests/sys/` 的约定：每个 `test_*.c` 编成一个独立可执行文件，`main()` 由 `tinytest.h` 的 `TT_MAIN` 展开。**

理由：C 的一个可执行文件只能有一个 `main`。若把所有测试堆进一个程序，一个测试崩溃（段错误 / 死循环）就会让后面的测试全部跑不到，失败清单也只看到第一个。拆开后：加测试 = 加一个文件，不改脚本；一个挂了不影响其他。

**用例表而不是顺序调用**：每个文件末尾用 `TT_CASE(...)` 声明用例，`tinytest.h` 按命令行参数分派"全部 / 某个用例 / `--list`"。这样"只跑一个用例"是运行器能力，不需要为每个测试写第二份开关代码。`--list` 通过**运行**编译产物获得，不解析源码文本——解析会漂移。

`apps/` 的编译：所有 App 编进同一个 component，由 manifest 在运行时决定启动谁 —— 不做动态加载（架构 §1）。

---

## 10. 门禁（`tools/`）

| 脚本 | 检查什么 | 现状 |
|---|---|---|
| `check_purity.py` | §2.2 纯 C 边界（五个目录不碰目标 SDK），规则在 `tools/purity_rules.txt` | **已实现** |
| `check_headers.py` | 每个头文件能单独 include（自包含） | **已实现** |
| `run_tests.py` | host 单测（`tests/sys/` 逐个编译运行，支持单用例） | **已实现** |
| `validate.py` | 一次跑完上面三项，不因前一项失败而中断 | **已实现** |
| `check_layers.py` | §2 的 include 矩阵（比 purity 更细的分层检查） | 新增 |
| `check_app_scope.sh` | `git diff --name-only` ⊆ `apps/`（AI 提交） | 新增 |
| `gen_catalog.py` | 从 `api/` + 服务注册生成能力目录 | 新增 |
| `check_catalog.py` | 生成物 ↔ 实际注册双向比对，防静默漂移 | 新增 |
| `check_blocking.py` | 任务体内必须有伪阻塞调用（防死循环） | 后置，先靠看门狗 |

**门禁只有一份实现：Python。** 曾经有过 `.sh` + `.ps1` 双份，结论是不划算——规则能同步，行为细节（退出码传播、路径分隔符、找不到编译器时的处置）同步不了，验证成本 ×2 的结果通常只验一个平台。约定见 [tools/README.md](../tools/README.md)：退出码 0 通过 / 1 代码问题 / 2 环境问题；规则数据仍单独放在 `tools/purity_rules.txt`，便于逐条 review。

其中 `check_headers.py` 是踩过坑才加的：`cm_ring.h` 曾漏 include `cm_err.h`，而单元测试因为先 include 了 `cm_err.h` 反而看不出来。**只有"单独 include 本头文件"的编译才有机会暴露这类问题**，所以它必须是一项独立检查，不能靠测试顺带覆盖。

---

## 11. 放错地方的信号（自查表）

出现以下情况，说明代码放错了层：

- 某个 `api/` 函数体超过 5 行 → 它应该是一段机制（移到 framework）或一个服务（移到 app）
- `framework/` 里出现 `strstr("chapter")` 之类的业务字符串
- `apps/` 里出现 `plat_` 前缀的符号 → 越了整层
- 两个 App 互相 include → 应该走 `api/`
- `lib/` 里出现 `vTaskDelay` → 纯逻辑被污染
- `platform/` 里出现 `#include "disp.h"` → 平台开始认识业务
- 为一块新板改了 `framework/` → 抽象位置错了

---

## 12. 决定记录

| # | 决定 | 结论 |
|---|---|---|
| 1 | `api/` 按能力拆头 | **已定：拆** |
| 2 | `lib/` 独立顶层 | **已定：独立** |
| 3 | 服务型 API 的实现方式 | **已定：注册表（框架转发）**，见 §4.1 |
| 4 | 公共工具放哪 | **已定：顶层 `common/`，向所有层开放**，见 §2.1 |
| 5 | 错误码是否分层 | **已定：不分层，全项目一个 `cm_err_t`**，见 §2.1 与 platform-port.md §2 |
| 6 | 框架层/应用层的可移植边界 | **已定：必须是标准 C11，与目标 SDK 无关；编译器差异收敛在 `cm_port.h`；由 `tools/check_purity.py` 强制**，见 §2.2 |
| 7 | host 测试的组织方式 | **已定：`tests/sys/` 下一个文件一个 `main()`，各自独立可执行**，见 §9 |
| 8 | 门禁脚本的跨平台形态 | **已定：只用 Python 一份实现，三平台同一份代码；规则数据单独存 `tools/purity_rules.txt`**，见 tools/README.md。**已否决**双份（`.sh` + `.ps1`）方案：行为细节同步不了，验证成本 ×2 实际只验一个平台 |
| 9 | 测试用例的选择粒度 | **已定：`tinytest.h` 用例表 + `TT_MAIN`，支持全部 / 单用例 / `--list`**；`--list` 通过运行产物获得，不解析源码 |
| 10 | 子进程输出的收集方式 | **已定：写临时文件，不用管道**。本机沙箱禁止命名管道，`capture_output=True` 会 `PermissionError`；见 `hostenv.run_capture()` |
| 11 | App 的回调集合 | **已定：只有 `init` / `exit` / `task` 三个，没有 create/destroy**。exit 覆盖所有结束路径（含 FAILED、含取消），作者没法只在正常路径清理。见 app-model.md |
| 12 | 阻塞接口的形态 | **已定：一律伪阻塞，App 不检查返回值**。把"停不下来"收敛到框架一处；代价是资源必须登记（`res_track` 仍未实现） |
| 13 | 平台如何表达"不支持某能力" | **已定：函数指针为 NULL，不用 stub 函数**。`static inline` stub 在每个 TU 有独立副本，跨 TU 比较函数指针恒不等，能力自检形同虚设；且 stub 制造"看起来实现了"的假象。见 platform-port.md §4.1 |

### 12.1 为什么选注册表（裁决记录）

| | A 注册表（选中） | B App 直接导出符号（否决） |
|---|---|---|
| 调用链 | `disp_commit()` → 转发层 → 函数指针 | `disp_commit()` 就是 display App 的符号 |
| 取消检查 | 框架统一做，只有一处 | 每个服务自己记得做 |
| 配额 / 超时 | 框架统一做 | 无法统一 |
| 服务未启动 / 已崩溃 | 两种状态可区分 | 符号存在但内部未就绪 → 每个服务各判一次 |
| 额外开销 | 一次间接调用 | 无 |
| 复杂度 | 要注册表 + 明确的初始化顺序 | 简单 |

决定性理由：**伪阻塞（§3.1）的保证必须落在一处**。选 B 就等于把"每个阻塞点都可取消"这个承诺，分散成"每个服务作者各自记得做"——那它就不是保证，是愿望。

唯一的代价是一次间接调用（几十纳秒）。什么时候值得重新考虑 B：当某个服务的调用频率高到间接调用成本可测（例如每帧上千次），此时应优化该服务的接口形态（批量提交），而不是放弃注册表。
