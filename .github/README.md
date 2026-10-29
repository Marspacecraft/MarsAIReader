# .github —— 自动 CI

> 回答一个问题：**提交之后，谁来把门禁跑一遍，跑在哪些平台上。**

工作流：

- [`workflows/ci.yml`](workflows/ci.yml) —— 默认，push / PR 触发，三格（linux gcc、linux clang、windows clang）。
- [`workflows/host-macos.yml`](workflows/host-macos.yml) —— macOS 一格，**只在手动触发时跑**，理由见 §2.1。

---

## 1. 它做的唯一一件事

```
python tools/validate.py --cc <编译器>
```

和本地 `Ctrl+Shift+B`「host: 全部门禁」**是同一条命令**，不是"照着抄一遍的 CI 版本"。

理由：CI 与本地一旦各写一份步骤，就会慢慢漂移，最后出现"本地全过、CI 挂在一个本地根本没有的检查上"。这个仓库对门禁的定位是**唯一裁决点**（[docs/code-structure.md](../docs/code-structure.md) §10），所以 CI 只能是它的调用者，不能是它的第二作者。

**加一条门禁 = 往 `tools/validate.py` 的 `STEPS` 里加一行，CI 自动跟上。** 任何在 workflow 里重新列出脚本名/参数的做法都是第二份清单，会腐烂。

## 2. 矩阵：跑哪几格、什么时候跑

| 格 | 何时跑 | 理由 |
|---|---|---|
| `linux / gcc` | 每次 | ubuntu runner 预装；也是 ESP-IDF 目标编译器所属的家族，最接近真实固件的行为 |
| `linux / clang` | 每次 | 预装；与 gcc 的 `-Werror` 判定并不总一致，一致性必须被看见 |
| `windows / llvm clang` | 每次 | 预装于 `C:\Program Files\LLVM\bin`；也是本仓库开发机上实际用的编译器 |
| `macos / apple clang` | **仅手动** | `-ffoo` 解析、`sizeof` 警告、`#include` 大小写敏感度与 Linux 不同。但它是 BSD 语义的**抽查**，不是目标平台（目标是 ESP32 + Windows/Linux 开发机） |

### 2.1 为什么 macOS 移出默认矩阵，而且是另开一个 workflow 文件

2026-10-29 第一次真实运行（run #1）：`windows` 25 秒通过，两格 Linux 各自失败，
而 `macos / apple clang` 排队 7 分钟仍没被 runner 接单。**只要矩阵里有一格没跑完，
整个 run 的状态就是 `queued`** —— 另外三格的结果在那之前没人看得到，界面看起来像
"CI 没启动"。

代价与收益不对等：macOS 计费权重是 Linux 的 10 倍，冷启动排队不可控，而它验证的
"BSD 环境下的脚本语义"对当前阶段价值有限。

做法是**拆成独立文件** [`workflows/host-macos.yml`](workflows/host-macos.yml)
（只 `workflow_dispatch`），而不是给矩阵加一个"`manual_only` 才跑"的 job 级条件。
原因是：job 级 `if` 能否读 `matrix` 上下文没有把握——赌错的表现是**静默全跑**或
**静默全不跑**，比多一个文件糟糕得多。拆文件的代价只是重复十几行 step，而两边的
真正内容都只有一句 `python tools/validate.py`。

**代价要认**：不再有周期性验证，macOS 格会随环境漂移而静默失效。发版前手动跑一次：

```
仓库页 → Actions → host gates (macOS, manual) → Run workflow
```

如果将来 `tools/` 开始依赖 GNU coreutils 之外的细节，或者真的要支持 macOS 开发机，
就应该把它挪回 `ci.yml` 的矩阵，而不是继续靠"记得手动跑"。

不上的格子及原因：

- **macOS gcc**：`/usr/bin/gcc` 是 clang 的别名，跑它只是把 clang 记两遍。
- **Windows gcc**：msys2 的 gcc 不在 runner 预装清单里，需要 `msys2/setup-msys2` 额外安装。要补的话：加那一步，再把 `cc` 指向 `ucrt64/bin/gcc.exe`。
- **MSVC `cl`**：不接受 `-std=c11` / `-Wextra` / `-fsyntax-only` 这套参数。本项目的门禁不是为它写的，硬塞进来等于给一个没人用的编译器维护第四套命令行。

`fail-fast: false`：一格挂了，其它格的结果仍然有价值——"只有 gcc 报错"是编译器差异，不是噪音。

## 3. 两处看起来像配置、其实是必需项

| 项 | 为什么必需 |
|---|---|
| `PYTHONUTF8: "1"` | Windows runner 的控制台代码页不是 UTF-8，而四个脚本的输出是中文。不设这一项，CI 会死在 `UnicodeEncodeError`——**打印一行中文**上，而不是死在门禁上。这是修环境，三个平台仍然共用同一份 Python，不做平台分支。 |
| 显式 `--cc` | 不写就让 `hostenv.find_cc()` 自己猜。猜中什么写进日志，但"我到底用哪个编译器验证过"这件事不该靠猜；显式传入后，`记录工具链身份` 那一步打出的 `--version` 就是本次验证的编译器身份。 |

退出码 0/1/2 的语义（[tools/README.md](../tools/README.md)）在 CI 里被翻译成两条不同的注解：1 是"代码违规"，2 是"环境坏了"。两者都让 job 失败，但**不允许在 UI 上长得一样**——否则一次 runner 环境故障会表现成"代码全错"。

## 4. 刻意不做的事

| 不做 | 原因 |
|---|---|
| 缓存（`actions/cache`） | 没有依赖可缓存。缓存 `build/host` 反而会把陈旧的测试可执行文件带进来，产生"改完没重新编译"的假绿灯。 |
| 按 `paths` 过滤（文档改动跳过 CI） | 四道门禁合计不到半分钟。为省半分钟制造"这次没跑 CI"的歧义不划算；分支保护要求 check 存在时，跳过还会卡住合并。 |
| 拆成多个 step 分别跑脚本 | 见 §1。 |
| 构建固件（`idf.py build`） | 根 `CMakeLists.txt` 与 `platform/boards/` 尚未落地，现在加只会得到一条永远红的 job。等固件入口与 IDF 版本定案后，**新增一个 job**，用 esp-idf 官方容器并固定版本；不要塞进 `host-gates`。 |
| Dependabot 自动升 action | 现在是 `@v4`/`@v5` 大版本标签。需要自动跟进时加 `.github/dependabot.yml`（`package-ecosystem: github-actions`）；不需要就手动改，两个 action 的升级频率极低。 |

## 5. 本地复现 CI 的每一格

```sh
python tools/validate.py --cc gcc
python tools/validate.py --cc clang
python tools/validate.py --cc "C:/Program Files/LLVM/bin/clang.exe"   # Windows
```

想手动跑一次而不推提交：仓库页 → Actions → `host gates` → Run workflow（`workflow_dispatch`）。

## 6. 已知限制

- 工作流里 `macos / apple clang`、`linux / gcc`、`linux / clang` 三格**没有在本机验证过**（本机只有 Windows + LLVM clang）。首次推送运行时，它们才是第一次真正执行；若报出 GCC 独有的 `-Werror` 判定，那是门禁在正常工作，应改代码或改门禁规则，而不是关掉该格。
- 没有覆盖 `platform/` 与固件侧：这两块的门禁属于 §4 表格里"构建固件"那一行，尚未开始。

## 7. 失败了先看哪里

**先看注解，不要先要日志。** 完整日志要登录才能下载，注解是公开可读的。所以
`host 门禁` 这一步在失败时会把 `FAIL` / `ERROR` / `error:` / `错误` / `违规` 的前 10 行
提升成 `::error::` 注解（每 job 上限 10 条），在 Actions 页面和 PR 界面上直接可见。

三条判读规则：

| 注解标题 | 含义 | 该改什么 |
|---|---|---|
| 门禁判定代码违规（退出码 1） | 有测试/头文件/纯 C 边界没通过 | 改代码；**若日志是 Python traceback，则是脚本自身故障** |
| 门禁无法执行（退出码 2） | 环境问题：没有编译器、找不到测试 | 改 CI 环境，不要改代码 |
| 意外退出码 N | `validate.py` 崩溃或返回了未约定的码 | 先看是不是脚本 bug |

第一次真实运行（#1）暴露的就是这一类：`linux / gcc` 与 `linux / clang` 均以退出码 1
失败，`windows / llvm clang` 通过 —— 平台差异，不是编译器差异。根因是
`platform/host-fake/plat_host.c` 在 `-std=c11` 下拿不到 POSIX 声明（详见
[platform-port.md §8](../docs/platform-port.md)）。
