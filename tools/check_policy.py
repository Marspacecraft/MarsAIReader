#!/usr/bin/env python3
"""
权限判定（强制层）：依赖 / 符号 / 写入。

data 在 policy.txt，语义在 docs/access-control.md；本脚本只做判定，不改数据。

用法：
    python tools/check_policy.py --deps                 # 依赖判定（身份无关，永远跑）
    python tools/check_policy.py --symbols              # 符号判定（身份无关，永远跑）
    python tools/check_policy.py --write --as 2         # 写入判定（需要声明身份）
    python tools/check_policy.py --all --as 2           # 三条全跑

为什么依赖判定不看 `#include` 文本：
    `../` 相对路径、`-I` 顺序、`#include_next`、`__has_include` 都能让文本与真实解析结果不一致。
    这里用 `clang -MM` 的真实解析结果——它给出的就是编译器实际打开的文件。

为什么符号判定不看定义在哪、而看声明是否可见：
    `api/` 声明、`framework/` 实现。App 调 `app_delay()` 时，链接到的是
    framework/src 里的符号，但声明在 api/include/app.h。所以判据是
    "这个未定义符号，是否出现在该编译单元实际 include 过的头文件里"。
    这正好堵住 `extern void app_run_exit(void);` 这类不 include 任何头就能越权的写法。

退出码：0 通过 / 1 有违规 / 2 环境问题（缺编译器、缺 nm、不是 git 仓库）。
"""

import argparse
import os
import re
import shutil
import sys
from pathlib import Path

import hostenv
import policy as policymod

ROOT = hostenv.ROOT

# 参与判定的编译单元：与 host 门禁使用同一份 include 路径（见 hostenv.INCLUDE_DIRS）。
SCAN_DIRS = ["common/src", "framework/src", "platform", "apps", "lib", "tests/sys"]

# 注释与字符串要先去掉再找标识符，否则注释里提到一个符号会被当成"已声明"。
_STRIP = [
    re.compile(r"/\*.*?\*/", re.S),
    re.compile(r"//[^\n]*"),
    re.compile(r'"(?:[^"\\\n]|\\.)*"'),
    re.compile(r"'(?:[^'\\\n]|\\.)*'"),
]
_IDENT = re.compile(r"\b([A-Za-z_]\w*)\b")


def rel(path):
    """绝对或相对路径 → 仓库内 posix 相对路径；在仓库外返回 None。"""
    p = Path(path)
    if not p.is_absolute():
        p = ROOT / p
    try:
        return p.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return None


def find_nm(cc):
    """找一个 nm。优先 llvm-nm（clang 的搭档，能读 COFF 与 ELF）。"""
    for name in ("llvm-nm", "nm"):
        found = shutil.which(name)
        if found:
            return found
    # LLVM 装在 Program Files 且不在 PATH 时，按编译器所在目录猜
    if cc:
        guess = Path(cc).parent / ("llvm-nm" + hostenv.EXE)
        if guess.exists():
            return str(guess)
        guess = Path(cc).parent / ("nm" + hostenv.EXE)
        if guess.exists():
            return str(guess)
    return None


def iter_sources():
    seen, out = set(), []
    for d in SCAN_DIRS:
        base = ROOT / d
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*.c")):
            key = str(path.resolve())
            if key not in seen:
                seen.add(key)
                out.append(path)
    return out


def clang_deps(cc, src):
    """返回该编译单元实际打开的头文件（仓库内相对路径列表）。

    预处理失败 = 违规（不静默跳过）：一个编不过的文件，其依赖关系也是未知的。
    """
    cmd = [cc] + hostenv.CFLAGS + ["-MM"]
    cmd += hostenv.include_flags(hostenv.INCLUDE_DIRS)
    cmd.append(str(src))
    rc, text = hostenv.run_capture(cmd)
    if rc != 0:
        return None, text

    # make 规则：target: dep1 dep2 \ 换行续行
    joined = text.replace("\\\n", " ")
    joined = joined.split("\n", 1)[0]
    if ":" not in joined:
        return None, text
    deps = joined.split(":", 1)[1].split()

    out = []
    for d in deps:
        r = rel(d.strip().replace("\\", "/"))
        if r is not None:
            out.append(r)
    return out, None


def compile_obj(cc, src, objdir):
    """编译到 .o，返回 (obj路径, 错误文本)。"""
    objdir.mkdir(parents=True, exist_ok=True)
    obj = objdir / (src.name + "." + str(abs(hash(str(src))) % 100000) + ".o")
    cmd = [cc] + hostenv.CFLAGS + ["-c", "-o", str(obj)]
    cmd += hostenv.include_flags(hostenv.INCLUDE_DIRS)
    cmd.append(str(src))
    rc, text = hostenv.run_capture(cmd)
    if rc != 0:
        return None, text
    return obj, None


def nm_symbols(nm, obj, undefined):
    """undefined=True 取未定义符号，False 取已定义的全局符号。"""
    cmd = [nm, "-u", str(obj)] if undefined else [nm, "--defined-only", "-g", str(obj)]
    rc, text = hostenv.run_capture(cmd)
    if rc != 0:
        return None
    out = []
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        if undefined:
            # "         U sym"
            if len(parts) >= 2 and parts[0] == "U":
                out.append(parts[1])
        else:
            # "0000000000000000 T sym"（COFF 可能只有两列）
            sym = parts[-1]
            if sym and sym[0] not in "US":
                out.append(sym)
    return out


def strip_code(text):
    for pat in _STRIP:
        text = pat.sub(" ", text)
    return text


# ---------------------------------------------------------------- 判定 1：依赖
def check_deps(cc, pol, sources):
    print("== 依赖判定 ==")
    violations = 0
    checked = 0
    for src in sources:
        r = rel(src)
        tag = pol.role_of(r)
        if tag is None:
            continue
        checked += 1
        deps, err = clang_deps(cc, src)
        if deps is None:
            print("  ERROR   {}  预处理失败（依赖关系未知）".format(r))
            for line in err.strip().splitlines()[:3]:
                print("          " + line.strip())
            violations += 1
            continue
        for d in deps:
            if not pol.can_read(tag, d):
                print("  DEP     {}  (tag {}) 读取了不允许的 {}".format(r, tag, d))
                violations += 1
    print("  检查 {} 个有角色的编译单元".format(checked))
    return violations


# ---------------------------------------------------------------- 判定 2：符号
def check_symbols(cc, nm, pol, sources):
    print("== 符号判定 ==")
    objdir = hostenv.BUILD_DIR / "policy"
    objs = {}
    violations = 0

    for src in sources:
        obj, err = compile_obj(cc, src, objdir)
        if obj is None:
            print("  ERROR   {}  编译失败，符号关系未知".format(rel(src)))
            for line in err.strip().splitlines()[:3]:
                print("          " + line.strip())
            violations += 1
            continue
        objs[src] = obj

    # 仓库内定义索引：符号 → 定义它的文件
    definers = {}
    for src, obj in objs.items():
        defined = nm_symbols(nm, obj, undefined=False)
        if defined is None:
            print("  ERROR   {}  nm 读不出已定义符号".format(rel(src)))
            violations += 1
            continue
        for s in defined:
            definers.setdefault(s, []).append(rel(src))

    checked = 0
    for src, obj in objs.items():
        r = rel(src)
        tag = pol.role_of(r)
        if tag is None:
            continue
        checked += 1
        used = nm_symbols(nm, obj, undefined=True)
        if used is None:
            print("  ERROR   {}  nm 读不出未定义符号".format(r))
            violations += 1
            continue

        deps, err = clang_deps(cc, src)
        if deps is None:
            print("  ERROR   {}  预处理失败，无法收集可见声明".format(r))
            violations += 1
            continue
        visible = set()
        for d in deps:
            # 只认头文件。把编译单元自己也算进来，它自己的 extern 声明就会变成
            # "已声明"——那正是这条判定要抓的东西。
            if not d.endswith(".h"):
                continue
            try:
                visible |= set(_IDENT.findall(strip_code((ROOT / d).read_text(
                    encoding="utf-8", errors="replace"))))
            except OSError:
                print("  ERROR   {}  读不到依赖头 {}".format(r, d))
                violations += 1

        for sym in used:
            if sym not in definers:
                continue          # 仓库外符号（libc/系统）不归这里管
            if sym in visible:
                continue          # 声明可见 → 正常链接
            where = definers[sym][0]
            print("  SYM     {}  (tag {}) 用了 {}，但没有任何实际 include 的头声明它（定义在 {}）"
                  .format(r, tag, sym, where))
            violations += 1

    print("  检查 {} 个有角色的编译单元，{} 个仓库内定义符号"
          .format(checked, len(definers)))
    return violations


# ---------------------------------------------------------------- 判定 3：写入
def git_lines(args):
    # -c core.autocrlf=false：只为了消掉 "LF will be replaced by CRLF" 的噪声。
    # 那条警告走 stderr，而 run_capture 把两个流合并，混进路径列表会被当成文件名。
    rc, text = hostenv.run_capture(
        ["git", "-c", "core.autocrlf=false"] + args)
    if rc != 0:
        return None
    return [x.strip() for x in text.splitlines()
            if x.strip() and not x.strip().lower().startswith("warning:")]


def check_write(pol, as_tag):
    print("== 写入判定 ==")
    if as_tag is None:
        print("  SKIP    未声明身份（--as <tag|human>）。")
        print("          这不是通过：CI 里必须声明身份，否则等于没判。")
        return -1

    changed = []
    for args in (["diff", "--name-only", "HEAD"],
                 ["diff", "--name-only", "--cached"],
                 ["ls-files", "--others", "--exclude-standard"]):
        got = git_lines(args)
        if got is None:
            print("  ERROR   git 不可用或不在仓库里")
            return None
        changed += got
    changed = sorted({c.replace("\\", "/") for c in changed if c.strip()})

    if not changed:
        print("  没有变更文件")
        return 0

    if as_tag == "human":
        print("  身份: human（人）。放行 {} 个变更文件，但请确认这份改动确实该由人提交。".format(len(changed)))
        for c in changed:
            print("    M       {}".format(c))
        return 0

    tag = int(as_tag)
    if tag not in pol.roles:
        print("  ERROR  未知角色 tag {}".format(tag))
        return None

    violations = 0
    for c in changed:
        if pol.can_write(tag, c):
            continue
        print("  WRITE   {}  不在 tag {} ({}) 的写权限内".format(c, tag, pol.roles[tag]["name"]))
        violations += 1

    print("  身份: tag {} ({})，检查 {} 个变更文件"
          .format(tag, pol.roles[tag]["name"], len(changed)))
    return violations


def main(argv):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)

    ap = argparse.ArgumentParser(description="权限判定：依赖 / 符号 / 写入")
    ap.add_argument("--deps", action="store_true")
    ap.add_argument("--symbols", action="store_true")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--as", dest="as_tag", help="声明的身份：角色 tag 数字，或 human")
    ap.add_argument("--cc", help="编译器路径（默认自动探测）")
    opts = ap.parse_args(argv[1:])

    if not (opts.deps or opts.symbols or opts.write or opts.all):
        print(__doc__)
        return 2
    if opts.all:
        opts.deps = opts.symbols = opts.write = True

    try:
        pol = policymod.Policy().load()
    except FileNotFoundError:
        return hostenv.fail_env("[policy] 缺少 policy.txt")

    table = pol.check()
    if table:
        for lineno, msg in table:
            print("  TABLE   行{}  {}".format(lineno, msg))
        print("FAIL: 策略表本身有问题（{} 处），先修数据再判定".format(len(table)))
        return 1

    total = 0

    if opts.write:
        w = check_write(pol, opts.as_tag)
        if w is None:
            return hostenv.fail_env("[policy] 写入判定无法执行")
        if w >= 0:
            total += w

    if opts.deps or opts.symbols:
        cc = hostenv.find_cc(opts.cc)
        if not cc:
            return hostenv.fail_env(hostenv.no_cc_message("policy"))
        sources = iter_sources()
        if not sources:
            return hostenv.fail_env("[policy] 没有可检查的 .c 文件")

        if opts.deps:
            total += check_deps(cc, pol, sources)
        if opts.symbols:
            nm = find_nm(cc)
            if not nm:
                return hostenv.fail_env("[policy] 找不到 nm / llvm-nm，符号判定无法执行")
            total += check_symbols(cc, nm, pol, sources)

    print("----")
    if total:
        print("FAIL: 权限违规 {} 处".format(total))
        print("      规则见 policy.txt，语义见 docs/access-control.md。")
        print("      越界不要绕过：按你那份 skill 的「触发边界」停下來要评审。")
        return 1
    print("PASS: 权限判定通过")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
