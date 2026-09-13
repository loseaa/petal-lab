#!/usr/bin/env python3
"""Petal 跨平台环境初始化引导器。

在任何机器（macOS / Linux / Windows-WSL）上，由智能体或用户运行：

    python3 setup_env.py

它会：
  1. 检测操作系统与缺失的前置依赖（C++ 编译器、make、Node.js、Python）。
  2. 缺失时给出对应系统的安装命令（不擅自 sudo，避免破坏环境）。
  3. 构建 C++ 执行内核 ./petal（thirdparty 已内置仓库，无需下载）。
  4. 安装并构建前端 (lab/web)。
  5. 校验产物：./petal 能打印用法；前端 lab/web/dist 已生成。

纯标准库实现，无需任何第三方包。
"""
from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.abspath(__file__))


def log(msg: str) -> None:
    print("• " + msg)


def have(cmd: str) -> bool:
    return shutil.which(cmd) is not None


def run(cmd: str, cwd=None, check=True) -> bool:
    log(f"运行: {cmd}" + (f"  (in {cwd})" if cwd else ""))
    r = subprocess.run(cmd, shell=True, cwd=cwd)
    if check and r.returncode != 0:
        print(f"  ! 命令失败 (exit {r.returncode}): {cmd}")
    return r.returncode == 0


def detect_os() -> str:
    s = platform.system().lower()
    if s.startswith("darwin"):
        return "macos"
    if s.startswith("win"):
        return "windows"
    return "linux"


def check_prereqs(os_name: str):
    print(f"\n== 检测前置依赖 (OS={os_name}) ==")
    missing = set()
    ver = sys.version_info
    if ver >= (3, 8):
        log(f"Python {ver.major}.{ver.minor}.{ver.micro} ✓")
    else:
        print(f"  ✗ 需要 Python >= 3.8（当前 {ver.major}.{ver.minor}）")
        missing.add("python")

    compiler = None
    for c in ("g++", "clang++", "cc"):
        if have(c):
            compiler = c
            break
    if compiler:
        log(f"C++ 编译器 {compiler} ✓")
    else:
        print("  ✗ 缺少 C++11 编译器")
        missing.add("compiler")

    if have("make"):
        log("make ✓")
    else:
        print("  ✗ 缺少 make")
        missing.add("make")

    if have("node") and have("npm"):
        out = subprocess.run("node -v", shell=True, capture_output=True, text=True).stdout.strip()
        log(f"Node {out} ✓")
    else:
        print("  ✗ 缺少 Node.js / npm")
        missing.add("node")

    return missing


def install_hints(os_name: str, missing):
    if not missing:
        return
    print("\n== 缺失依赖的安装命令 ==")
    if {"compiler", "make"} & missing:
        if os_name == "macos":
            print("  macOS:  xcode-select --install          # 安装 clang + make")
        elif os_name == "linux":
            print("  Debian/Ubuntu:  sudo apt-get update && sudo apt-get install -y build-essential")
            print("  Fedora/RHEL:    sudo dnf groupinstall -y 'Development Tools'")
        else:
            print("  Windows:  推荐用 WSL2 后按 Linux 命令安装；或安装 MinGW-w64 + make")
    if "node" in missing:
        if os_name == "macos":
            print("  macOS:  brew install node     # 或 nvm install 18")
        elif os_name == "linux":
            print("  Linux:  sudo apt-get install -y nodejs npm   # 或 nvm install 18")
        else:
            print("  Windows:  https://nodejs.org 下载安装，或在 WSL 内 nvm install 18")


def build_cpp() -> bool:
    print("\n== 构建 C++ 执行内核 petal ==")
    if os.path.exists(os.path.join(REPO, "petal")):
        log("./petal 已存在，执行增量重建")
    if not run("make -j4", cwd=REPO, check=False):
        print("  ! make 失败，请查看上方输出（多半是缺少编译器，先按安装命令补上）")
        return False
    petal = os.path.join(REPO, "petal")
    if os.path.exists(petal):
        log("./petal 构建成功 ✓")
        run(f"{petal}", cwd=REPO, check=False)  # 应打印用法说明
        return True
    print("  ✗ 未生成 ./petal")
    return False


def build_frontend() -> bool:
    print("\n== 安装并构建前端 ==")
    web = os.path.join(REPO, "lab", "web")
    if not os.path.isdir(web):
        print("  ✗ 找不到 lab/web")
        return False
    if not run("npm install", cwd=web, check=False):
        print("  ! npm install 失败")
        return False
    if not run("npm run build", cwd=web, check=False):
        print("  ! npm run build 失败")
        return False
    dist = os.path.join(web, "dist", "index.html")
    if os.path.exists(dist):
        log("前端构建成功 (lab/web/dist) ✓")
        return True
    print("  ✗ 未生成 lab/web/dist")
    return False


def main() -> None:
    os_name = detect_os()
    print("Petal 环境初始化引导器")
    print(f"仓库根目录: {REPO}")
    missing = check_prereqs(os_name)
    install_hints(os_name, missing)
    if missing:
        print("\n⚠ 存在缺失依赖；下面会尝试构建，缺失编译器时会失败并给出安装命令。")
    build_cpp()
    build_frontend()
    print("\n== 完成 ==")
    print("启动服务:  python3 ./lab/petal-lab serve --port 8899")
    print("浏览器打开: http://localhost:8899")


if __name__ == "__main__":
    main()
