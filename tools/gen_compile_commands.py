#!/usr/bin/env python3
"""Generate compile_commands.json for clangd from the Makefile.

Why this exists
---------------
All #include directives in this project use the "flat" form, e.g. src/petal.cpp
writes #include "instanceFile.h" while the header actually lives in
src/io/instanceFile.h.  The Makefile therefore passes every module directory as
an -I flag (see SRC_DIRS / INCLUDES), and `make` compiles fine.

clangd, however, needs the same information.  Without a compilation database it
falls back to a synthetic command whose working directory is *the file's own
directory*, so the relative flag -Isrc is resolved as <file dir>/src (i.e.
/…/src/src for src/petal.cpp) and every cross-directory include fails.  The
result is a cascade of bogus errors: "instanceFile.h file not found",
"unknown type name 'MTRand'", "no member named 'vector' in namespace 'std'",
and so on.  None of them are real defects.

This script emits a compile_commands.json with absolute -I paths and
"directory" set to the project root, which is exactly what clangd expects.

Usage
-----
    make compile_commands          # regenerate after changing SRC_DIRS
    python3 tools/gen_compile_commands.py

The output is a build artifact (git-ignored); regenerate it whenever SRC_DIRS,
CXXFLAGS or EXCLUDED_SRCS in the Makefile change.
"""

from __future__ import annotations

import json
import os
import re
import shlex
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAKEFILE = os.path.join(ROOT, "Makefile")
OUTPUT = os.path.join(ROOT, "compile_commands.json")

# OPUSMinerCR sources include its headers by relative path, so `make` works
# without it; add it anyway so the IDE resolves those includes too.
EXTRA_INCLUDE_DIRS = ("thirdparty/OPUSMinerCR",)

# Directory scanned for C++ sources in addition to SRC_DIRS (see Makefile).
EXTRA_CPP_DIRS = ("thirdparty/OPUSMinerCR",)

CXX = os.environ.get("CXX", "clang++")
CC = os.environ.get("CC", "clang")


def read_makefile_lines() -> list[str]:
    with open(MAKEFILE, encoding="utf-8") as handle:
        return handle.read().splitlines()


def parse_make_list(lines: list[str], var: str) -> list[str]:
    """Return the items of a backslash-continued assignment such as SRC_DIRS."""
    header = re.compile(rf"^{var}\s*(?::=|\+=|=)\s*(.*)$")
    items: list[str] = []
    collecting = False
    for raw in lines:
        if not collecting:
            match = header.match(raw)
            if not match:
                continue
            raw = match.group(1)
            collecting = True
        item = raw.strip()
        if item.endswith("\\"):  # more lines to come
            items.append(item[:-1].strip())
            continue
        if item:
            items.append(item)
        break
    return [item for item in items if item and not item.startswith("#")]


def parse_make_var(text: str, var: str, default: str) -> list[str]:
    match = re.search(rf"^{var}\s*\?=\s*(.*)$", text, re.M)
    return shlex.split(match.group(1)) if match else shlex.split(default)


def rel(path: str) -> str:
    return os.path.relpath(path, ROOT)


def make_entry(source: str, compiler: str, flags: list[str], includes: list[str]) -> dict:
    obj = os.path.splitext(source)[0] + ".o"
    return {
        "directory": ROOT,
        "file": os.path.join(ROOT, source),
        "arguments": [compiler, *flags, *includes, "-c", "-o", obj, source],
    }


def main() -> int:
    lines = read_makefile_lines()
    text = "\n".join(lines)

    src_dirs = parse_make_list(lines, "SRC_DIRS")
    excluded = set(parse_make_list(lines, "EXCLUDED_SRCS"))
    cxxflags = parse_make_var(text, "CXXFLAGS", "-std=c++11 -O2 -Wall -fexceptions")
    cflags = parse_make_var(text, "CFLAGS", "-O2")

    if not src_dirs:
        print("error: could not parse SRC_DIRS from Makefile", file=sys.stderr)
        return 1

    includes = ["-I" + os.path.join(ROOT, d) for d in (*src_dirs, *EXTRA_INCLUDE_DIRS)]

    entries: list[dict] = []
    for directory in (*src_dirs, *EXTRA_CPP_DIRS):
        abs_dir = os.path.join(ROOT, directory)
        if not os.path.isdir(abs_dir):
            continue
        for name in sorted(os.listdir(abs_dir)):
            if not name.endswith((".cpp", ".c")):
                continue
            source = f"{directory}/{name}"
            if source in excluded:
                continue
            compiler, flags = (CXX, cxxflags) if name.endswith(".cpp") else (CC, cflags)
            entries.append(make_entry(source, compiler, flags, includes))

    with open(OUTPUT, "w", encoding="utf-8") as handle:
        json.dump(entries, handle, indent=2)
        handle.write("\n")

    print(f"wrote {rel(OUTPUT)}: {len(entries)} files, {len(includes)} include dirs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
