# tools/ —— host 门禁

四个脚本，全部用 Python 写。**同一份代码在 Linux / macOS / Windows 上跑**，没有第二份实现。

| 脚本 | 命令 | 检查什么 |
|---|---|---|
| 单元测试 | `python tools/run_tests.py` | 编译并运行 `tests/sys/test_*.c`，支持单文件 / 单用例 |
| 头文件自包含 | `python tools/check_headers.py` | 每个头文件能否被单独 `#include` |
| 纯 C 边界 | `python tools/check_purity.py` | `common/ api/ lib/ framework/ apps/` 是否碰了目标 SDK |
| 全部门禁 | `python tools/validate.py` | 上面三项一起跑，不中断 |

VS Code 里按 `Ctrl+Shift+B` 走「host: 全部门禁」。四个任务见 [.vscode/tasks.json](../.vscode/tasks.json)。

## 为什么最后只剩 Python

中间有过一版 `sh` + `ps1` 双实现，结论是**不划算**：

- 规则能同步，**行为细节同步不了**：退出码传播、路径分隔符、找不到编译器时的处置，每一处都要两遍验证。
- 验证成本是 ×2，于是实际只跑一个平台，另一个"改了但没验"——比只有一份更危险。
- `.ps1` 还有 BOM 坑（见下），等于给门禁本身加了一个失败模式。

Python 是唯一在三家平台上语义一致的选项，而且构建机基本都有。

### 顺带留个记录：PowerShell 5.1 必须带 UTF-8 BOM

Windows PowerShell 5.1 读**无 BOM** 的 `.ps1` 时按系统 ANSI 代码页解码。中文系统上是 GBK：

```
无 BOM：Chinese test: 涓枃杈撳嚭
有 BOM：Chinese test: 中文输出
```

同一个坑也影响 `Get-Content`：读无 BOM 的 UTF-8 文件（如 `.vscode/tasks.json`）会被当成 GBK，
弹出莫名其妙的 JSON 语法错误——文件没坏，是读取方解码错。现在没有 `.ps1` 了，但这个坑值得记着。

## 退出码约定

| 码 | 含义 | 处置 |
|---|---|---|
| 0 | 通过 | — |
| 1 | 检查发现问题（违规、测试失败、编译失败） | 挡合并，修代码 |
| 2 | 环境问题（找不到编译器、缺规则文件、没有测试、用例名不存在） | 修环境或改参数，不是修代码 |

区分 1 和 2 是刻意的：CI 里"代码违规"和"环境坏了"要分开。把两者都当 1，
环境坏掉时会表现成"代码全错"，最浪费时间。

## CI（[.github/workflows/ci.yml](../.github/workflows/ci.yml)）

CI 里跑的就是 `python tools/validate.py --cc <编译器>`，和本地 `Ctrl+Shift+B` 同一条命令，
矩阵是 `{linux gcc, linux clang, macos clang, windows clang}`。三个平台都上，是因为这四个脚本的
价值就在"一份实现、三家语义一致"——只在一个平台跑，这句话没人验证。

**加门禁只改 `validate.py` 的 `STEPS`，CI 自动跟上。** 在 workflow 里再列一遍脚本名就是第二份清单。
取舍与限制见 [.github/README.md](../.github/README.md)。

## 只跑一个用例

```sh
python tools/run_tests.py                            # 全部
python tools/run_tests.py --list                     # 有哪些测试程序
python tools/run_tests.py test_common                # 一个测试程序的全部用例
python tools/run_tests.py test_common --list         # 这个程序里有哪些用例
python tools/run_tests.py test_common list_ops       # 一个用例
```

测试程序名可以写 `test_common`、`common` 或唯一子串。用例名必须精确。

**指定了具体目标时是"直接模式"**：不打印 `==== 程序名 ====`、`----`、`PASS: host 单元测试`
这些外层装饰，屏幕上只有这个用例自己的信息与结果——这正是你此刻要看的那一行。

```
$ python tools/run_tests.py test_common list_ops
PASS: list_ops (22 checks)                      # 通过，退出码 0

$ python tools/run_tests.py test_common crc_vectors
  FAIL tests/sys/test_common.c:272  cm_crc16(0xFFFFu, v, 9) == 0x29B1, expected 0x29B2
FAIL: crc_vectors (7 checks, 1 failures)        # 退出码 1

$ python tools/run_tests.py test_common --list
types_macros
err_codes
list_ops
...
```

跑全部时才有汇总（多个程序需要边界），这是唯一保留 `====` 与 `----` 的场合。

## 指定编译器

脚本按 `--cc` → 环境变量 `CC` → `PATH` 里的 `cc`/`gcc`/`clang` → Windows 常见安装位置
（`C:\Program Files\LLVM\bin\clang.exe` 等）的顺序找。都没有就以退出码 2 失败，并说明怎么装。

```sh
python tools/validate.py --cc "C:/Program Files/LLVM/bin/clang.exe"
```

## 一个本机限制：不要用管道收集子进程输出

`tools/hostenv.py` 里的 `run_capture()` 用**临时文件**而不是管道接子进程输出。
原因是本机沙箱禁止进程间命名管道，`subprocess.run(..., capture_output=True)` 会直接
`PermissionError: [WinError 5]`。新加脚本时请复用 `hostenv.run_capture()`，别自己写管道。

## purity_rules.txt 的格式

```
kind:pattern
```

- `kind` 只用于输出分类标签（`include` / `ident` / `cross`）。
- 第一个 `:` 之后全部属于正则；因此**正则里不能出现 `:`**。
- 正则是 Python `re` 语法（现在只有一份实现，不必再迁就 POSIX ERE 与 .NET 的交集）。
- `#` 开头的行是注释。

规则单独成文件而不是写进脚本：它是"这道门禁到底在防什么"的唯一说明书，
写成数据便于逐条 review，也便于改规则时不碰代码。
