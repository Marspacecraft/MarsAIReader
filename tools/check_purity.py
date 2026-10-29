#!/usr/bin/env python3
"""
纯 C 边界门禁。

用法：
  python tools/check_purity.py
  python tools/check_purity.py --rules tools/purity_rules.txt

检查范围：common/ api/ lib/ framework/ apps/
这些目录必须做到「标准 C11 + common 的兼容宏」，与任何目标 SDK、芯片、图形库、
OS 头文件无关。等价判据：和 platform/host-fake/ 一起编到 PC 上能跑。

platform/ 不在检查范围内 —— 它就是用来吸住这些依赖的地方。

规则维护在 tools/purity_rules.txt，格式 kind:正则，'#' 开头为注释。
为什么规则要单独成文件：它是"这道门禁到底在防什么"的唯一说明书，
写在代码里会被淹没，写成数据就便于逐条 review。

退出码：0 无违规 / 1 有违规 / 2 环境问题（缺规则文件、没有可检查的目录）
"""

import argparse
import re
import sys
from pathlib import Path

import hostenv

SCAN_DIRS = ["common", "api", "lib", "framework", "apps"]
SOURCE_SUFFIXES = {".c", ".h"}
DEFAULT_RULES = hostenv.ROOT / "tools" / "purity_rules.txt"

# 注意：这里**不再**检查"普通 App 不得 include framework/ 与 sys/"。
# 那条规则已经由 tools/check_policy.py 的依赖判定接管——它用 clang -MM 的真实
# 解析结果，比正则扫 #include 文本可靠（`../`、-I 顺序、#include_next 都能绕过文本）。
# 同一个约束只留一处实现，否则两处会慢慢不一致，而先失效的那处没人知道。



def load_rules(path):
    """解析 kind:pattern 规则。格式不对就抛 ValueError，不静默跳过。"""
    rules = []
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if ":" not in stripped:
            raise ValueError("第 {} 行缺少 ':'：{}".format(lineno, stripped))
        kind, pattern = stripped.split(":", 1)
        rules.append((kind.strip(), re.compile(pattern)))
    return rules


def iter_sources(dirs):
    for d in dirs:
        base = hostenv.ROOT / d
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                yield path


def main():
    ap = argparse.ArgumentParser(description="纯 C 边界门禁")
    ap.add_argument("--rules", help="规则文件路径（默认 tools/purity_rules.txt）")
    opts = ap.parse_args()

    rules_path = Path(opts.rules) if opts.rules else DEFAULT_RULES
    if not rules_path.is_file():
        return hostenv.fail_env("[purity] 缺少规则文件：{}".format(rules_path))

    try:
        rules = load_rules(rules_path)
    except ValueError as exc:
        return hostenv.fail_env("[purity] 规则格式错误：{}".format(exc))

    dirs = [d for d in SCAN_DIRS if (hostenv.ROOT / d).is_dir()]
    if not dirs:
        return hostenv.fail_env("[purity] 没有可检查的目录（{}）".format(" ".join(SCAN_DIRS)))

    sources = list(iter_sources(dirs))
    if not sources:
        return hostenv.fail_env("[purity] 范围内没有 .c/.h 文件")

    print("[purity] 范围: {}  ({} 个文件)".format(" ".join(dirs), len(sources)))
    print("[purity] 规则: {}".format(rules_path.name))

    violations = 0
    for kind, pattern in rules:
        for path in sources:
            rel = path.relative_to(hostenv.ROOT).as_posix()
            # 跨层 include 只放行 platform/port/（契约头，抽象接口，不含实现）
            allow_port = kind == "cross"
            text = path.read_text(encoding="utf-8", errors="replace")
            for lineno, line in enumerate(text.splitlines(), 1):
                if not pattern.search(line):
                    continue
                if allow_port and "platform/port/" in line:
                    continue
                print("  {:<8} {}:{}  {}".format(kind, rel, lineno, line.strip()))
                violations += 1

    print("----")
    if violations:
        print("FAIL: 纯 C 边界违规 {} 处".format(violations))
        print("      这些目录必须与目标 SDK 无关。需要平台能力时：")
        print("      在 api/ 定义接口 → framework/ 转发到 platform/port/。")
        return 1

    print("PASS: 纯 C 边界（0 处违规）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
