# AGENTS.md —— 唯一必读入口

你是这个仓库里的 AI 协作者。**读完这份文件先别动手**：按下面的路由表只打开你需要的那一份，其余不要预加载。

## 这个项目是什么

- 一个能跑在多种硬件平台上的 AI 墨水屏阅读器**固件框架**。三层：应用层（App）/ 框架层（机制）/ 平台层（硬件）。
- 框架只提供机制、不拥有任务；平台层由各硬件平台自行实现，**缺的能力返回 `NOT_SUPPORTED`**。
- 本仓库只做框架与应用层公共逻辑，**不含具体板级实现**。

## 第一件事：确认你的身份

| 你要做的事 | 身份 | 读这一份 |
|---|---|---|
| 新建/修改一个 App | AI 开发者（tag 2） | [skills/ai-app-developer/SKILL.md](skills/ai-app-developer/SKILL.md) |
| 改框架机制、加系统 App、新增一个能力 | 项目开发者（tag 3/1） | [skills/project-developer/SKILL.md](skills/project-developer/SKILL.md) |
| 移植一块新板、调平台层 | 硬件平台开发者（tag 4/5/6） | [skills/platform-developer/SKILL.md](skills/platform-developer/SKILL.md) |

身份不明时先问，不要猜。**"以某身份工作"不等于"拥有该身份的全部权限"**——写权限只看 `policy.txt`。

## 所有人都必须遵守

1. **只写 `policy.txt` 允许你写的路径。** 核对方式：`python tools/policy.py role <路径>`。
2. **`skills/`、`policy.txt`、构建文件、`.github/` 只有人能改。** AI 能改考卷，所有约束就失效了。
3. **不跨层**：依赖方向 `apps → framework → platform`，`api/` 是唯一横向通道；两个 App 不直接链接符号。
4. **可阻塞的接口一律伪阻塞**：调用 `app_delay()` 之后的代码可能永远不执行，这是正常路径。清理只能放 `exit()`。
5. **交付固定四行**，缺一行视为没交付：

```
Build:        PASS / FAIL / NOT RUN
Host tests:   PASS / FAIL / NOT RUN（贴数字）
Device tests: PASS / FAIL / NOT RUN（没做就写 NOT RUN）
Unverified:   还有什么没验证、为什么
```

**不得把"编译通过"说成"硬件验证通过"。** 设备连着 ≠ 获得烧录授权，要烧录先问。

## 必须停下来问人的事

- 改分层、依赖方向、四条不变量中的任何一条
- 改 `policy.txt`（包括新增一个角色 tag）
- 改 App 的回调集合、伪阻塞语义、错误码归属
- 改 `api/` 的公开签名
- 需要某个能力但 `api/` 里没有——**报告，不要绕过**
- 烧录真机、改 CI、动 `.github/`

上面是全局清单。**各身份还有自己更具体的停止条件**（带自检命令与实测依据要求），在你那份 skill 的「触发边界：命中任一条，立刻停」一节——那份比你记着的更全，先看它。

停下时的交付不是"我遇到问题了"，而是：我在做什么 / 撞到哪条边界 / 为什么不能自己解决 / 选项与代价 / 我建议哪个 / 在你回复前我不动哪些文件。

## 事实来源优先级

遇到冲突时按这个顺序采信：

1. **`policy.txt`** —— 谁不能碰什么
2. **头文件**（`api/`、`platform/port/`）—— 接口的权威定义
3. **`docs/`** —— 为什么这样设计、失败模式
4. **`skills/`** —— 具体怎么做
5. **本文件**

文档不复制函数签名（会漂移）；签名只在头文件里。**信息不足时直接问，不要猜。**

## 现在能调用什么

`api/include/api.h` 是 App 的唯一入口，目前全部符号：`app_id` / `app_delay` / `app_alloc` / `app_free` / `app_log` / `app_logp`。

显示、输入、音频、存储、网络、电源、资源配额、能力查询**都还没有接口**。

## 常用命令

```bash
python tools/validate.py                        # 全部 host 门禁（测试 + 头文件 + 纯 C + 权限判定）
python tools/validate.py --as 2                 # 同上，并以 AI 开发者身份判定"你改了哪些文件"
python tools/run_tests.py test_common           # 只跑一个测试程序
python tools/run_tests.py test_common list_ops  # 只跑一个用例
python tools/policy.py --check                  # 策略表自检
python tools/policy.py role framework/src/fw_app.c   # 这个文件属于哪个角色
python tools/check_policy.py --all --as 2       # 只跑三条权限判定
```

退出码：`0` 通过 / `1` 代码有问题 / `2` 环境有问题。**`2` 不是通过。**
写入判定不声明 `--as` 会打印 `SKIP`——**那也不叫通过**。

## 当前落地状态（重要：别把规划当现实）

| 已跑通 | 还没做 |
|---|---|
| `common/` 全套工具 | 资源登记 `res_track`（伪阻塞的资源回收依赖它） |
| App 生命周期**代码**（启动/停止/重启/FAILED） | 生命周期**测试**——`apps/` 删除后无覆盖，见 app-model.md §8 |
| `platform/host-fake` 虚拟平台 | 服务注册表（`fw_svc_*`） |
| host 门禁 + CI | 显示 / 输入 / 音频 / 存储 / 网络 / 电源能力 |
| 三条权限判定（依赖 / 符号 / 写入） | 注册表的"只能追加"细则（改既有行不会红，见 access-control.md §8） |

**`apps/` 不在本仓库**：所有 App 实现与清单由产品仓库提供。

**看到 `docs/` 里描述、但代码里找不到的东西，以代码为准**，并在 `Unverified:` 里写清楚。文档里标了"待实现""不是强制"的段落，就是还没生效的部分。

## 文档地图

| 想知道 | 读 |
|---|---|
| 三层各是什么、怎么解耦 | [docs/architecture.md](docs/architecture.md) |
| 一个 App 由什么构成、谁让它跑/停 | [docs/app-model.md](docs/app-model.md) |
| 代码放哪、谁能 include 谁 | [docs/code-structure.md](docs/code-structure.md) |
| 谁能读/写哪个目录、凭什么判定 | [docs/access-control.md](docs/access-control.md) |
| 平台实现方要满足什么 | [docs/platform-port.md](docs/platform-port.md) |
| 文档本身怎么维护 | [docs/README.md](docs/README.md) |
