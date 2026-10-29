#!/usr/bin/env python3
"""
一次跑完全部门禁。

用法：
  python tools/validate.py
  python tools/validate.py --cc C:/LLVM/bin/clang.exe

三道门禁都会跑，不因为前一个失败就中断 —— 这样一次就能看到所有问题，
而不是"修一个、跑一次、再发现下一个"。

退出码：0 全过 / 1 有门禁未通过 / 2 环境问题
"""

import argparse
import subprocess
import sys

import hostenv

# (标题, 脚本, 该脚本支持的额外参数)
STEPS = [
    ("host 单元测试", "tools/run_tests.py", ["--cc"]),
    ("头文件自包含", "tools/check_headers.py", ["--cc"]),
    ("纯 C 边界", "tools/check_purity.py", []),
]


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    ap = argparse.ArgumentParser(description="全部 host 门禁")
    ap.add_argument("--cc", help="C 编译器路径")
    opts = ap.parse_args()

    failed = []
    env_error = False

    for title, script, accepts in STEPS:
        print()
        print("==== {} ====".format(title))
        cmd = [sys.executable, str(hostenv.ROOT / script)]
        if opts.cc and "--cc" in accepts:
            cmd += ["--cc", opts.cc]

        proc = subprocess.run(cmd)
        if proc.returncode == 1:
            failed.append(title)
        elif proc.returncode != 0:
            env_error = True

    print()
    print("================")
    if failed:
        print("FAIL: {} 项门禁未通过：{}".format(len(failed), " ".join(failed)))
        return 1
    if env_error:
        print("ERROR: 有门禁因环境问题未能执行")
        return 2

    print("PASS: 全部 host 门禁通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
