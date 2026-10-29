# tests/sys —— host 测试入口

每个 `test_*.c` 编译成一个独立可执行文件，自带 `main()`（由 `tinytest.h` 的 `TT_MAIN` 展开）。

## 为什么按文件拆，而不是编成一个大程序

C 里一个可执行文件只能有一个 `main`。全堆在一起的话：

- 某个测试段错误或死循环，后面的测试全跑不到；
- 想只跑一个测试没有开关，改一行就要等全套。

按文件拆开后，加测试 = 加一个 `.c`，不用改脚本、不用改注册表。

## tinytest.h —— 三十行的运行器

不引第三方框架：这里只需要"断言 + 选跑哪个用例 + 退出码"。
引框架的代价是在三个平台、交叉工具链上各配一次，得不偿失。

```c
#include "tinytest.h"

static void list_push_back(void)
{
    TT_CHECK(list.count == 1);
    TT_CHECK_EQ_U32(node.value, 42);
}

static const tt_case_t s_cases[] = {
    TT_CASE(list_push_back),
};

TT_MAIN(s_cases)
```

| 宏 | 用途 |
|---|---|
| `TT_CHECK(cond)` | 失败时打印 `文件:行号` 与表达式原文 |
| `TT_CHECK_EQ_U32(actual, expected)` | 失败时同时打印两个值的十六进制 |
| `TT_CHECK_STR(actual, expected)` | 字符串比较，空指针也算失败 |
| `TT_CASE(fn)` | 用例表条目，名字自动取函数名 |
| `TT_MAIN(cases)` | 展开成 `main()`，分派全部 / 单个 / `--list` |

`--list` 直接编译后调用，而不是解析源码文本——解析会漂移，编译产物不会。

## 运行

```sh
python tools/run_tests.py                          # 全部程序、全部用例
python tools/run_tests.py test_common              # 一个程序
python tools/run_tests.py test_common list_ops     # 一个用例
python tools/run_tests.py test_common --list       # 列出用例名
```

退出码：0 通过 / 1 有失败 / 2 用例名不存在或环境问题。

指定用例（或 `--list`）时是**直接模式**：只输出这一条用例的信息与结果，没有外层装饰。

```
$ python tools/run_tests.py test_common list_ops
PASS: list_ops (22 checks)
```

## 约定

| 事项 | 规定 |
|---|---|
| 文件名 | `test_<被测对象>.c`，如 `test_common.c`、`test_stor.c` |
| 入口 | `TT_MAIN(s_cases)`；不要自己写 `main` |
| 用例粒度 | 一个用例 = 一件可独立判断对错的事，失败信息要能定位到函数 |
| 失败信息 | 打到 stdout，含 `文件:行号` 与被判定的表达式 |
| 依赖 | 只能标准 C + `common/` + `api/` + `lib/` + `framework/include/` + 本测试自己的桩 |
| 禁止 | 不得 include `platform/`；不得依赖执行顺序（用例之间不共享状态） |

平台相关逻辑的测试放在 `platform/` 侧，不在这个目录。

## 已注册的编译选项

`tools/run_tests.py` 固定传入：

```
-std=c11 -Wall -Wextra -Werror
-I common/include -I tests/sys
+ common/src/*.c          # 被测实现
```

`-Werror` 是刻意的：测试代码里的未使用变量、隐式转换，往往是真 bug 的前兆。

注意 `common/src/*.c` 是**自动加入**的：测试测的是实现，不是头文件里的宏。
将来把测试拆到 `api/`、`lib/` 时，需要给 `run_tests.py` 的 `impl` 补上对应实现目录。

## 当前测试

| 文件 | 用例 |
|---|---|
| `test_common.c` | `types_macros` `err_codes` `list_ops` `ring_ops` `atomic_ops` `bits_ops` `crc_vectors` |
| `test_port.c` | `noreturn_attr` `inline_restrict` `likely_unlikely` `common_macros` |

## 加一个测试

```sh
cp tests/sys/test_common.c tests/sys/test_stor.c   # 改内容
python tools/run_tests.py test_stor                # 只跑它
python tools/run_tests.py                          # 全跑
```
