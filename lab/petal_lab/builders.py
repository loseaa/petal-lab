"""Turn a form submission into a petal command line.

This is the security boundary of the whole feature: the web UI causes a local
process to be launched, so every value is validated here.

Rules enforced:
  * learner names and flags must match a strict allow-list pattern — no shell
    metacharacters, ever;
  * numeric options are parsed as integers;
  * data set paths must resolve inside one of the configured roots, so a
    crafted request cannot read or run anything outside them;
  * arguments are passed as a list, never through a shell.
"""

from __future__ import annotations

# pyright: reportExplicitAny=none, reportAny=none, reportUnknownMemberType=none, reportUnknownArgumentType=none, reportUnknownVariableType=none, reportUnknownParameterType=none, reportUnusedCallResult=none, reportImplicitStringConcatenation=none, reportMissingTypeArgument=error

import os
import re
from pathlib import Path
from typing import Optional

# Metadata/data suffix pairs petal is known to use. The lab data sets use
# .pmeta/.pdata rather than the .pm/.pd of the bundled examples.
META_SUFFIXES = [".pm", ".pmeta"]
DATA_SUFFIX_BY_META = {".pm": ".pd", ".pmeta": ".pdata"}

# petal learner/flag names: letters, digits and underscore only.
NAME_RE = re.compile(r"^[A-Za-z0-9_]+$")
# petal's own option letters used by the form (keep in sync with petal.cpp).
ALLOWED_FLAGS = {"d", "c", "x", "t", "s", "b", "v", "p", "n", "f", "z", "m", "e"}


class CommandError(ValueError):
    """Raised when a submitted form cannot be turned into a safe command."""


def _check_name(value: str, what: str) -> str:
    if not isinstance(value, str) or not NAME_RE.match(value):
        raise CommandError(f"{what} 不合法：{value!r}")
    return value


def _as_positive_int(value, what: str, default: Optional[int] = None) -> Optional[int]:
    if value is None or value == "":
        return default
    try:
        n = int(value)
    except (TypeError, ValueError):
        raise CommandError(f"{what} 必须是整数：{value!r}")
    if n <= 0:
        raise CommandError(f"{what} 必须为正数：{n}")
    return n


def resolve_dataset(meta: str, data: str, roots: list[str]) -> tuple[str, str]:
    """Ensure both paths exist and live under one of the allowed roots."""
    if not meta or not data:
        raise CommandError("缺少数据集文件")

    meta_p = Path(meta).expanduser()
    data_p = Path(data).expanduser()
    for p in (meta_p, data_p):
        if not p.is_file():
            raise CommandError(f"文件不存在：{p}")

    if roots:
        # Deliberately abspath, not resolve(): data/uci is a symlink to the lab
        # data folder, and resolve() would follow it out of the allowed root,
        # making every legitimate data set look out of bounds.
        allowed = [Path(os.path.abspath(Path(r).expanduser())) for r in roots]
        for p in (meta_p, data_p):
            logical = Path(os.path.abspath(p))
            if not any(_is_relative_to(logical, root) for root in allowed):
                raise CommandError(f"数据集不在允许的目录内：{p}")
    return str(meta_p), str(data_p)


def _is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def build_command(spec: dict, roots: list[str]) -> tuple[list[str], dict]:
    """Build the argv for one petal invocation.

    Returns (argv, meta) where meta carries the human-readable command and the
    progress total (number of folds) when known.
    """
    meta, data = resolve_dataset(spec.get("meta"), spec.get("data"), roots)

    learners = spec.get("learners") or []
    if not learners:
        raise CommandError("请至少选择一个算法")
    if isinstance(learners, str):
        learners = [learners]
    learners = [_check_name(x, "算法名") for x in learners]

    mode = spec.get("mode", "xval")
    folds = _as_positive_int(spec.get("folds"), "折数", 10)
    discretiser = spec.get("discretiser") or ""

    cmd: list[str] = [meta, data]

    if discretiser:
        _check_name(discretiser, "离散化方法")
        cmd.append(f"-d{discretiser}")

    verbosity = _as_positive_int(spec.get("verbosity"), "verbosity", 2)
    cmd.append(f"-v{verbosity}")

    total = None
    if mode == "xval":
        cmd.append(f"-x{folds}")
        total = folds
    elif mode == "trainTest":
        test_data = spec.get("testData")
        if not test_data:
            raise CommandError("训练/测试模式需要指定测试集")
        test_meta, test_data = resolve_dataset(meta, test_data, roots)
        cmd.append(f"-t{test_data}")
    elif mode == "learningCurves":
        cmd.append("-c")
        for key, flag in (("holdout", "h"), ("startSize", "s"),
                          ("endSize", "e"), ("points", "n"), ("trials", "t")):
            val = _as_positive_int(spec.get(key), key)
            if val is not None:
                cmd.append(f"+{flag}{val}")
    else:
        raise CommandError(f"不支持的模式：{mode}")

    extra = spec.get("extra") or ""
    if extra:
        for token in str(extra).split():
            if not re.match(r"^[+\-]?[A-Za-z0-9_.=,/]+$", token):
                raise CommandError(f"附加参数不合法：{token}")
            if token.startswith("-") and len(token) > 1 and token[1] not in ALLOWED_FLAGS:
                raise CommandError(f"不允许的选项：{token}")
        cmd.extend(str(extra).split())

    cmd.append(f"-l{learners[0]}")
    if len(learners) > 1:
        # Extra learners only make sense where petal accepts several.
        if mode != "learningCurves":
            raise CommandError("只有学习曲线模式支持多个算法；其它模式请分多次运行")
        for extra_learner in learners[1:]:
            cmd.append(f"-l{extra_learner}")

    return cmd, {"command": " ".join(cmd), "folds": total, "learners": learners,
                 "dataset": data}
