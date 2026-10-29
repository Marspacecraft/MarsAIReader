"""
host 门禁脚本共用的环境探测（不是门禁本身）。

为什么单独一个模块：编译器探测、警告开关、构建目录这三件事被四个脚本用到，
复制四份的话，某天调整 `-Werror` 就会出现"一个门禁严、三个门禁松"。
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

# 仓库根目录：本文件在 tools/ 下
ROOT = Path(__file__).resolve().parent.parent

BUILD_DIR = ROOT / "build" / "host"

# 与 CMakeLists / 交叉编译保持一致的 C 标准与警告档位。
# -Werror 是有意的：这些警告在嵌入式里几乎全是真 bug（隐式转换丢精度、未初始化、
# 声明了没定义）。
CFLAGS = ["-std=c11", "-Wall", "-Wextra", "-Werror"]

# Windows 上 clang/gcc 常常不在 PATH 里（尤其 LLVM 的默认安装位置）。
_WINDOWS_GUESSES = [
    r"C:\Program Files\LLVM\bin\clang.exe",
    r"C:\Program Files (x86)\LLVM\bin\clang.exe",
    r"C:\msys64\ucrt64\bin\gcc.exe",
    r"C:\msys64\mingw64\bin\gcc.exe",
]

EXE = ".exe" if os.name == "nt" else ""


def find_cc(explicit=None):
    """返回编译器路径；找不到返回 None。

    优先级：命令行 > 环境变量 CC > PATH > Windows 常见安装位置。
    """
    if explicit:
        return explicit if shutil.which(explicit) or Path(explicit).exists() else None

    env = os.environ.get("CC")
    if env:
        if shutil.which(env) or Path(env).exists():
            return env

    for name in ("cc", "gcc", "clang"):
        found = shutil.which(name)
        if found:
            return found

    if os.name == "nt":
        for guess in _WINDOWS_GUESSES:
            if Path(guess).exists():
                return guess

    return None


def no_cc_message(script):
    return (
        "[{}] 找不到 C 编译器。\n"
        "  装 cc/gcc/clang 之一，或设环境变量 CC=/path/to/clang，\n"
        "  或加参数 --cc /path/to/clang。".format(script)
    )


def include_flags(dirs):
    """把若干目录转成 -I 参数，不存在的目录直接跳过。"""
    flags = []
    for d in dirs:
        p = Path(d)
        if not p.is_absolute():
            p = ROOT / p
        if p.is_dir():
            flags.append("-I" + str(p))
    return flags


def fail_env(msg, code=2):
    print(msg, file=sys.stderr)
    return code


def run_capture(cmd, cwd=None):
    """执行命令并收集输出，返回 (退出码, 合并输出文本)。

    为什么不用 subprocess 的管道（capture_output=True）：
    本机的沙箱禁止进程间命名管道，管道路径会直接 PermissionError。
    写临时文件没有这个限制，且编译输出本来就是整体查看的，不需要流式。
    """
    log_dir = BUILD_DIR / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / "cmd.log"

    with open(log_path, "wb") as sink:
        proc = subprocess.run(cmd, cwd=cwd, stdout=sink, stderr=subprocess.STDOUT)

    text = log_path.read_text(encoding="utf-8", errors="replace")
    try:
        log_path.unlink()
    except OSError:
        pass
    return proc.returncode, text
