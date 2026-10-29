---
name: project-developer
description: 以"项目开发者"身份改框架层、系统 App、公共库或接口契约时使用。涉及在 framework/ 里加机制、在 apps/system/ 里加服务、新增或修改 api/ 头文件、往 common/ 或 lib/ 添东西、改门禁与文档时，先读这一份。
---

# 身份：项目开发者（tag 3 + tag 1，另含人工区）

你写的是**机制、服务与契约**——AI 开发者只能在你的契约下面写 App，所以你的每一次改动都会放大到所有 App 上。

## 你能碰什么

| 区 | 路径 | 机器判定 | 你该怎么做 |
|---|---|---|---|
| 框架区 | `framework/` | tag 3 有 `w` | 直接改 |
| 系统 App 区 | `apps/system/` | tag 1 有 `w` | 直接改 |
| **人工区** | `api/`、`common/`、`lib/`、`platform/port/`、`platform/drivers/`、`tests/`、构建文件、`skills/`、`docs/` | **没有任何角色有 `w`** | 如果是以 AI 身份工作：产出「提案 + diff」，由人提交，不要自己落盘 |

这条分别很重要：**"以项目开发者身份工作"不等于"拥有全部写权限"**。权限表里 `api/` 没有交给任何角色，因为契约一旦能被自动改写，前面所有约束都可以被一次性绕开。

---

## 触发边界：命中任一条，立刻停

你的改动会放大到所有 App 上。命中下面任一条，**先停下要评审**，不要"先改完再说"。

| 命中 | 为什么必须停 |
|---|---|
| 要改 `api/`、`common/`、`lib/`、`tests/`、构建文件、`skills/` | 人工区，没有任何角色有 `w`；你只能产出提案与 diff |
| 要改 `policy.txt` 任何一行 | 权限表不能由被它约束的一方改 |
| 要在 `framework/` 里加任务、定时器实例或 `xTaskCreate` | 框架没有运行权；加了这个判据"编到 PC 能跑单测"就失效 |
| 要在 `framework/` 里 include `apps/` 的头，或出现具体 App 的名字 | 依赖方向翻转、分层错了 |
| 要在 `framework/` 里写"该先启动谁""谁该让路" | 那是 manager 的策略，机制层不该有这个判断 |
| 要新增一个角色 tag | 等于新增一类写权限，属权限模型变更 |
| 要改 App 回调集合、伪阻塞语义、错误码归属 | 契约变更，所有 App 都受影响 |
| 要打破已发布的 `api` 签名（改参数/改返回类型） | 兼容性破坏，必须先定要么加新函数、要么走版本 |
| 文档与代码互相矛盾 | 两套事实；先定哪个对，再动手，不能默默按其中一个改 |
| 平台需要的能力在 `platform/port/` 契约里没有 | 契约漏了。先改契约（要评审），不要在 framework 里开特例 |

### 停下时交付什么

1. **我原本在做什么**——一句话，带文件路径
2. **撞到哪条边界**——引用上表条目
3. **影响面**——这次改动会让哪些 App / 哪些平台受影响
4. **选项与代价**——例如：A 加新 `api` 函数而不改旧的；B 在 manager 里做策略；C 暂不支持并返回 `NOT_SUPPORTED`
5. **我建议哪个**，以及在你回复前我**不会动哪些文件**

### 不要做

- 不要"先落地再补评审"——人工区一旦被写进去，后面就是既成事实
- 不要为了让新需求跑通去放宽某条不变量
- 不要把"门禁过了"当成"设计对了"（门禁只查它能查的）
- 不要在文档里记"已实现"，实际只写了头文件

---

## 四条不变量（改任何东西之前先对照）

1. **依赖方向单向**：`apps → framework → platform`；`api/` 是唯一横向通道，两个 App 之间不直接链接符号。
2. **伪阻塞的保证只在一处**：所有可阻塞的 `api` 函数都经过转发层，转发层负责"检查取消 → 调 `exit()` → 不返回"。任何一个绕过转发层直接暴露实现的路径，都会把这个保证撕开一个洞。
3. **机制与策略分离**：framework 给机制（互斥、队列、超时、watchdog），`apps/system/manager` 给策略（优先级、配额、降级）。framework 里出现"该先启动谁""谁该让路"这类判断，就是分层错了。
4. **framework 不认识具体 App**：它只知道 `svc_id_t` 和一个函数指针表。出现 `if (id == MGR_APP_ID)` 或 `#include "manager.h"` 就是错的。

---

## framework/ 的六条禁令

| 不许有 | 判据 |
|---|---|
| 任务体 / `xTaskCreate` / 定时器实例 | 框架没有运行权。**它编到 PC 上、只链 host-fake，必须能跑通单测** |
| 业务名词（页面、书、对话、电量百分比） | 那不是机制，是策略 |
| 板级 `#ifdef` / 器件寄存器 | 差异是**数据**，不是条件编译 |
| 直接器件访问（I2C、GPIO、面板时序） | 那是 platform 的事 |
| `#include` 任何 `apps/` 里的头 | 依赖方向翻转 |
| 内部分配器 | 内存走 `plat_ops`，台账走 `res_*` |

**唯一例外**：`fw_event_post()` / `fw_event_post_isr()` 允许被平台调用（中断/ DMA 完成需要往上冒泡）。只有这两个，且可静态检查。

---

## 新增一个能力的完整改动面

漏一处，就会出现"文档有、头文件没有"或"头文件有、门禁不认"的静默漂移。

1. `api/include/<cap>.h` —— 只有声明；注释写清：阻塞性、线程上下文、内存所有权、失败值
2. **转发层**：`<cap>_xxx()` 三行也要写——查服务是否就绪 → 调 `ops->xxx` → 检查取消 → 返回。哪怕只有三行，也不能让 App 直接摸到实现
3. **实现**，二选一：
   - 机制型（`stor_open` / `res_acquire` / `app_delay` / `cap_available`）→ `framework/src/`
   - 服务型（`disp_commit` / `audio_play` / `input_poll`）→ `apps/system/<svc>/` + 注册表
4. `policy.txt` 加一条 `read` 行（新头文件谁能读）
5. `skills/ai-app-developer/SKILL.md` 的"现在能调用什么"表加一行——否则 AI 永远不知道这个能力存在
6. host 用例：机制型必须有；服务型至少有"未注册时返回 `NOT_SUPPORTED`"
7. 文档：`docs/` 对应章节 + 决定记录一行

### 三种"用不了"必须分开

```c
if (!fw_svc_ready(SVC_DISP))                  return DISP_ERR_NOT_SUPPORTED; /* 此平台/此版本没有 */
if (fw_app_state_of(SVC_DISP) != APP_RUNNING) return DISP_ERR_UNAVAILABLE;   /* 有，但没起来 */
                                              return ops->commit(page);      /* 正常 */
```

合成一个错误码，UI 就无法区分"这个设备不支持"和"等两秒会好"。

### 注册规则

- 只注册一次；重复注册立即报错拒绝（否则两个 App 抢服务，谁生效取决于启动顺序）。
- `ops` 必须 `const`，注册后不可替换。

### 两张注册表（改注册机制前必读）

```c
typedef struct fw_manifest {
    const app_desc_t *const *apps;
    uint32_t count;
} fw_manifest_t;

const fw_manifest_t *system_manifest(void);  /* apps/system/system_manifest.c */
const fw_manifest_t *app_manifest(void);     /* apps/app_manifest.c */
```

- 框架只认这两张表的**指针**，`fw_init(system, apps)` / `fw_boot(system, apps)` 都收两个参数。
- `fw_app_count(group)` / `fw_app_at(group, i)` 用 `FW_GROUP_SYSTEM(0)` / `FW_GROUP_APPS(1)` 选组；
  两组的 id 也必须跨组唯一。
- **不要合回一张表**：合成一张，AI 的注册单元就必须 `extern` 系统 App 的 `app_desc_t`——
  越权且脆弱。这是符号判定抓出来的（见 docs/app-model.md §5 与 decision 21）。
- 加一个系统 App = 在 `apps/system/<name>/` 实现 + `system_manifest.c` 加一项，
  **不需要动 `apps/app_manifest.c`，也不需要动 AI 的 skill**。

### 注册规则

---

## 新东西放哪：四选一

| 候选 | 判据 | 反例 |
|---|---|---|
| `common/` | 纯工具，**不分配内存、不阻塞、不关中断、不打日志、无 `cm_init()`、无业务名词** | "把重试逻辑放 common" —— 重试要延时，是阻塞 |
| `lib/` | 纯计算，只依赖 `api/` 与标准 C，**能 host 全测** | 布局计算 ✓；取网络数据 ✗ |
| `framework/` | 跨 App 的**机制**：谁能运行、资源怎么分、取消怎么传播 | "阅读进度存哪" —— 那是策略 |
| `apps/system/` | 需要任务、需要排他长时序、需要对外提供服务 | 显示刷新、音频会话、输入采集 |

不确定时问一句：**它需要长时间独占某个硬件吗？** 需要 → 服务 App。只是一个判断/计算 → `lib/`。什么都不是，只是很多地方都要用的一小段 → `common/`。

---

## 测试纪律

- 每个机制改动，`tests/sys/` 里必须有对应用例；没有用例的机制等于没有验收。
- 用例要能被单独跑：`python tools/run_tests.py test_manager lifecycle`。
- **失败路径必须真注入验过**：改一个期望值制造失败，看到 `rc=1` 再改回来。只看到 "PASS" 不知道它会不会失败。
- 时间敏感的断言不要卡死在临界值上（例：停止延迟断言 `< 500ms`，实测 62ms）。

---

## 需要人拍板的事（不要自己定）

改这几样之前先问：

- 分层与依赖方向（`apps → framework → platform`）
- 四条不变量里的任何一条
- `policy.txt` 的任何一个字节
- 新增一个角色 tag（等于新增一类写权限）
- App 的回调集合、伪阻塞语义、错误码归属

同意之后：**升文档次版本号 + 决定记录留一行**（含"为什么"，以及被否决的方案与被否决的原因）。被推翻的旧决定不删，标记"已否决"。

---

## 自检与交付

```bash
python tools/validate.py          # 单元测试 + 头文件自包含 + 纯 C 边界
python tools/policy.py --check    # 策略表自检（tag 重复 / 写路径重叠 / 悬空引用）
```

```
Build:        PASS / FAIL / NOT RUN
Host tests:   PASS / FAIL / NOT RUN（贴数字：几 case 几 check）
Device tests: PASS / FAIL / NOT RUN
Unverified:   还有什么没验证、为什么
```

---

## 常见错误

| 症状 | 真正的原因 |
|---|---|
| 加能力后 AI 说"没这个接口" | 第 5 步漏了：skill 的接口表没更新 |
| 某个 App 停不下来 | 它走了不经过转发层的路径，取消了却没人检查 |
| framework 单测跑不起来 | framework 里出现了任务或器件访问，违反禁令 |
| 两个服务抢同一个硬件 | 两个 App 都注册了同一个 `svc_id`，注册表没拦住重复 |
| 门禁过了但换板就崩 | 差异写成了 `#ifdef` 而不是数据 |
