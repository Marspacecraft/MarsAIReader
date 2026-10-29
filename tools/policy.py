#!/usr/bin/env python3
"""读取 policy.txt，回答三个问题：某路径归哪个角色、某角色能否读某路径、某角色能否写某路径。

这是策略表的**数据校验层**，不是强制层。
真正的三条判定（依赖 / 符号 / 写入）见 docs/access-control.md §4，尚未实现。

用法：
    python tools/policy.py --check
    python tools/policy.py role  apps/demo/x/app.c
    python tools/policy.py read  2 api/sys/app_sys.h
    python tools/policy.py write 2 apps/app_manifest.c

退出码：0 正常 / 1 表本身有问题 / 2 环境或参数问题。
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POLICY = os.path.join(ROOT, "policy.txt")


class Policy:
    def __init__(self):
        self.roles = {}       # tag -> {"name": str, "writes": [prefix, ...], "line": int}
        self.reads = {}       # prefix -> set(tag)
        self.read_lines = {}  # prefix -> 行号（报错用）
        self.errors = []

    # ---- 加载 ----------------------------------------------------------
    def load(self, path=POLICY):
        if not os.path.isfile(path):
            raise FileNotFoundError(path)
        errors = []
        with open(path, encoding="utf-8") as f:
            for lineno, raw in enumerate(f, 1):
                line = raw.split("#", 1)[0].strip()
                if not line:
                    continue
                parts = line.split()
                kind = parts[0]
                if kind == "role":
                    if len(parts) < 3:
                        errors.append((lineno, "role 行至少要 tag、名字、一个写路径"))
                        continue
                    try:
                        tag = int(parts[1])
                    except ValueError:
                        errors.append((lineno, "role 的 tag 不是整数: %r" % parts[1]))
                        continue
                    if tag in self.roles:
                        errors.append((lineno, "tag %d 重复定义" % tag))
                    self.roles[tag] = {"name": parts[2], "writes": parts[3:], "line": lineno}
                elif kind == "read":
                    if len(parts) < 3:
                        errors.append((lineno, "read 行至少要一个路径和一个 tag"))
                        continue
                    path_prefix, tags = parts[1], parts[2:]
                    try:
                        self.reads[path_prefix] = {int(t) for t in tags}
                    except ValueError:
                        errors.append((lineno, "read 的 tag 不是整数: %r" % (tags,)))
                    self.read_lines[path_prefix] = lineno
                else:
                    errors.append((lineno, "未知行类型 %r（只认 role / read）" % kind))
        self.errors = errors
        return self

    # ---- 查询 ----------------------------------------------------------
    @staticmethod
    def _longest(prefixes, path):
        path = path.replace("\\", "/")
        hit = [p for p in prefixes if path.startswith(p)]
        return max(hit, key=len) if hit else None

    def role_of(self, path):
        """文件属于哪个角色。没有匹配 = None（只有人能写）。"""
        best = None
        for tag, info in self.roles.items():
            for w in info["writes"]:
                if path.replace("\\", "/").startswith(w):
                    if best is None or len(w) > best[0]:
                        best = (len(w), tag)
        return best[1] if best else None

    def can_read(self, tag, path):
        prefix = self._longest(self.reads, path)
        return prefix is not None and tag in self.reads[prefix]

    def can_write(self, tag, path):
        info = self.roles.get(tag)
        if not info:
            return False
        return any(path.replace("\\", "/").startswith(w) for w in info["writes"])

    # ---- 自检 ----------------------------------------------------------
    def check(self):
        errs = list(self.errors)

        # 规则 2.4：同一个文件不能被两个角色认领
        owned = [(w, tag) for tag, i in self.roles.items() for w in i["writes"]]
        for i, (a, ta) in enumerate(owned):
            for b, tb in owned[i + 1:]:
                if ta != tb and (a.startswith(b) or b.startswith(a)):
                    errs.append((self.roles[ta].get("line", 0),
                                 "写路径 %r (tag %d) 与 %r (tag %d) 重叠" % (a, ta, b, tb)))

        # read 引用的 tag 必须存在
        known = set(self.roles)
        for prefix, tags in self.reads.items():
            for t in tags:
                if t not in known:
                    errs.append((self.read_lines.get(prefix, 0),
                                 "read %r 引用了未定义的 tag %d" % (prefix, t)))

        # 每个角色至少要能读到自己写的地方（否则它写出来的东西自己都 include 不了）
        for tag, info in self.roles.items():
            for w in info["writes"]:
                if not self.can_read(tag, w + "self_probe.c"):
                    errs.append((info.get("line", 0),
                                 "tag %d (%s) 能写 %r 但读不到它" % (tag, info["name"], w)))
        return errs


def main(argv):
    sys.stdout.reconfigure(line_buffering=True) if hasattr(sys.stdout, "reconfigure") else None
    if len(argv) < 2:
        print(__doc__)
        return 2
    try:
        pol = Policy().load()
    except FileNotFoundError:
        print("policy.txt 不存在", file=sys.stderr)
        return 2

    cmd = argv[1]
    if cmd == "--check":
        errs = pol.check()
        for lineno, msg in errs:
            print("  ERROR 行%d  %s" % (lineno, msg))
        print("角色 %d 个，读规则 %d 条" % (len(pol.roles), len(pol.reads)))
        if errs:
            print("FAIL: 策略表自检发现 %d 个问题" % len(errs))
            return 1
        print("PASS: 策略表自检通过")
        return 0

    if cmd == "role":
        if len(argv) < 3:
            print("用法: policy.py role <路径>", file=sys.stderr)
            return 2
        path = argv[2]
        r = pol.role_of(path)
        print("%s -> %s" % (path, ("tag %d (%s)" % (r, pol.roles[r]["name"])) if r else "无角色（只有人能写）"))
        return 0

    if len(argv) < 4:
        print("用法: policy.py {read|write} <tag> <路径>", file=sys.stderr)
        return 2
    try:
        tag = int(argv[2])
    except ValueError:
        print("tag 必须是整数", file=sys.stderr)
        return 2
    path = argv[3]

    if cmd == "read":
        print("tag %d 读 %s -> %s" % (tag, path, "允许" if pol.can_read(tag, path) else "拒绝"))
    elif cmd == "write":
        print("tag %d 写 %s -> %s" % (tag, path, "允许" if pol.can_write(tag, path) else "拒绝"))
    else:
        print("未知命令 %r" % cmd, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
