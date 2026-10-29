#!/usr/bin/env python3
"""
头文件自包含检查。

用法：
  python tools/check_headers.py                     检查全部对外头文件目录
  python tools/check_headers.py api/include         只检查指定目录

做什么：对每个 .h 生成一个"只 include 这一个头"的空 TU，用 -Werror 编译。

为什么需要：单元测试往往先 include 了别的头，会把"这个头少 include 了自己的依赖"
掩盖掉。这类缺陷只在别人按不同顺序 include 时才炸，属于最难查的一类。

退出码：0 全部自包含 / 1 有失败 / 2 环境问题（没有编译器）
"""

import argparse
import sys
from pathlib import Path

import hostenv


def main():
    ap = argparse.ArgumentParser(description="头文件自包含检查")
    ap.add_argument("dirs", nargs="*", help="要检查的 include 目录")
    ap.add_argument("--cc", help="C 编译器路径")
    ap.add_argument("-v", "--verbose", action="store_true")
    opts = ap.parse_args()

    # 顺序即 -I 顺序：先放被依赖最多的，再放依赖别人的。
    dirs = opts.dirs or [
        "platform/port",
        "common/include",
        "api/include",
        "framework/include",
        "api/include/sys",
    ]

    cc = hostenv.find_cc(opts.cc)
    if cc is None:
        return hostenv.fail_env(hostenv.no_cc_message("headers"))

    inc = hostenv.include_flags(dirs)
    tu_dir = hostenv.BUILD_DIR / "headers"
    tu_dir.mkdir(parents=True, exist_ok=True)

    total = 0
    failed = 0

    for d in dirs:
        path = Path(d) if Path(d).is_absolute() else hostenv.ROOT / d
        if not path.is_dir():
            print("[headers] 跳过不存在的目录：{}".format(d))
            continue

        for header in sorted(path.glob("*.h")):
            total += 1
            tu = tu_dir / ("tu_" + header.name + ".c")
            tu.write_text('#include "{}"\nint main(void) {{ return 0; }}\n'.format(header.name),
                          encoding="utf-8", newline="\n")

            cmd = [cc] + hostenv.CFLAGS + ["-fsyntax-only"] + inc + [str(tu)]
            if opts.verbose:
                print("$ " + " ".join(cmd))
            rc, output = hostenv.run_capture(cmd)

            rel = "{}/{}".format(d, header.name)
            if rc == 0:
                print("OK   " + rel)
            else:
                failed += 1
                print("FAIL " + rel)
                for line in output.splitlines():
                    print("       " + line)

    print("----")
    if failed:
        print("FAILED: {} / {} 个头文件不自包含".format(failed, total))
        return 1
    print("PASS: {} 个头文件自包含".format(total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
