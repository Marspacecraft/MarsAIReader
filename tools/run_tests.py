#!/usr/bin/env python3
"""
编译并运行 tests/sys/ 下的 host 单元测试。

用法：
  python tools/run_tests.py                           跑全部测试程序的全部用例
  python tools/run_tests.py --list                    列出测试程序名
  python tools/run_tests.py test_common               只跑 test_common 的全部用例
  python tools/run_tests.py test_common list_ops      只跑 test_common 里的 list_ops
  python tools/run_tests.py test_common --list        列出 test_common 的用例名
  python tools/run_tests.py --cc C:/LLVM/bin/clang.exe 指定编译器

退出码：0 通过 / 1 有用例失败或编译失败 / 2 环境问题（没有编译器、找不到测试）

为什么支持"只跑一个用例"：改一行代码就要等全部用例跑完，会让人少跑测试。
按用例名跑要快得多，成本低到没有借口跳过。
"""

import argparse
import subprocess
import sys
from pathlib import Path

import hostenv

SYS_DIR = hostenv.ROOT / "tests" / "sys"

# 测试是白盒的：能看到所有层的头。被测代码自己受门禁约束（见 check_purity.py），
# 测试文件不受限——它本来就要能看见 framework/ 的控制面。
INCLUDE_DIRS = [
    "common/include",
    "api/include",
    "framework/include",
    "platform/port",
    "apps",
    "apps/_system",
    "tests/sys",
]

# 所有实现文件一起编：这不是最优做法（改一行会重编很多），但它是"零配置"——
# 不需要维护源文件列表，加一个 .c 就自动进入构建。对这么大的项目，值。
_IMPL_GLOBS = [
    "common/src/*.c",
    "framework/src/*.c",
    "platform/host-fake/*.c",
    "apps/*.c",
    "apps/**/*.c",
]


def impl_sources():
    """所有非测试的 .c，去重并保持顺序稳定。"""
    seen = set()
    out = []
    for pat in _IMPL_GLOBS:
        for p in sorted(hostenv.ROOT.glob(pat)):
            if p in seen:
                continue
            seen.add(p)
            out.append(p)
    return out


def discover():
    """tests/sys/test_*.c → [(名字, 路径)]，按名字排序保证输出稳定。"""
    if not SYS_DIR.is_dir():
        return []
    return sorted((p.stem, p) for p in SYS_DIR.glob("test_*.c"))


def resolve(selector):
    """把用户输入解析成测试程序名。

    接受 'test_common'、'common'、'test_common.c'，以及唯一子串。
    """
    if selector.endswith(".c"):
        selector = selector[:-3]
    names = [n for n, _ in discover()]

    if selector in names:
        return selector
    if ("test_" + selector) in names:
        return "test_" + selector

    hits = [n for n in names if selector in n]
    if len(hits) == 1:
        return hits[0]
    return None


def build(cc, name, src, verbose):
    hostenv.BUILD_DIR.mkdir(parents=True, exist_ok=True)
    out = hostenv.BUILD_DIR / (name + hostenv.EXE)
    # 被测实现要和测试一起编：测试测的是它们，不是头文件里的宏
    impl = impl_sources()
    cmd = ([cc] + hostenv.CFLAGS + hostenv.include_flags(INCLUDE_DIRS)
           + [str(src)] + [str(p) for p in impl] + ["-o", str(out)])
    if verbose:
        print("$ " + " ".join(cmd))
    rc, output = hostenv.run_capture(cmd)
    if rc != 0:
        print("FAIL: {} 编译失败".format(name))
        sys.stdout.write(output)
        return None
    return out


def run_program(exe, args):
    cmd = [str(exe)] + args
    proc = subprocess.run(cmd)
    return proc.returncode


def main():
    # 行缓冲：否则自己的输出会被缓冲，和子进程的输出交错成乱序
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    ap = argparse.ArgumentParser(add_help=True, description="host 单元测试")
    ap.add_argument("file", nargs="?", help="测试程序名，如 test_common")
    ap.add_argument("case", nargs="?", help="用例名，如 list_ops")
    ap.add_argument("--list", action="store_true", help="列出测试程序或某程序的用例")
    ap.add_argument("--cc", help="C 编译器路径")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印编译命令")
    opts = ap.parse_args()

    tests = discover()
    if not tests:
        return hostenv.fail_env("[tests] {} 下没有 test_*.c".format(SYS_DIR))

    if opts.file is None:
        if opts.list:
            for name, _ in tests:
                print(name)
            return 0
        selected = tests
    else:
        name = resolve(opts.file)
        if name is None:
            print("[tests] 找不到测试程序：{}".format(opts.file), file=sys.stderr)
            print("[tests] 可用：", file=sys.stderr)
            for n, _ in tests:
                print("  " + n, file=sys.stderr)
            return 2
        selected = [(n, p) for n, p in tests if n == name]

    cc = hostenv.find_cc(opts.cc)
    if cc is None:
        return hostenv.fail_env(hostenv.no_cc_message("tests"))

    if opts.verbose:
        print("[tests] 编译器: {}".format(cc))

    # 指定了具体目标（某个用例、或某个程序的用例清单）时不再套自己的框架输出：
    # 用户要的就是这一条用例的信息和结果，多一行装饰都是噪声。
    direct = bool(opts.case) or bool(opts.list)

    failed = 0
    env_error = 0

    for name, src in selected:
        if not direct:
            print("==== {} ====".format(name))

        exe = build(cc, name, src, opts.verbose)
        if exe is None:
            if direct:
                return 1
            failed += 1
            continue

        args = []
        if opts.list:
            args = ["--list"]
        elif opts.case:
            args = [opts.case]

        rc = run_program(exe, args)

        if direct:
            # 直接模式：结果就是子进程的退出码，它自己已经把信息与结果打出来了
            return rc

        if rc == 1:
            failed += 1
        elif rc == 2:
            env_error += 1

    print("----")
    if env_error:
        print("ERROR: {} 个测试程序参数无效（用例名不存在？）".format(env_error))
        return 2
    if failed:
        print("FAIL: {} 个测试程序未通过".format(failed))
        return 1

    print("PASS: host 单元测试")
    return 0


if __name__ == "__main__":
    sys.exit(main())
