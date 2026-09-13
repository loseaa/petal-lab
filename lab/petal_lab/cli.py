"""Command line entry point for petal-lab."""

from __future__ import annotations

# pyright: reportExplicitAny=none, reportAny=none, reportUnknownMemberType=none, reportUnknownArgumentType=none, reportUnknownVariableType=none, reportUnknownParameterType=none, reportUnusedCallResult=none, reportImplicitStringConcatenation=none, reportMissingTypeArgument=error

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from . import builders, db, ingest, nl_agent, server


def default_db() -> Path:
    return Path(__file__).resolve().parent.parent / "petal.db"


def default_petal() -> Path:
    """The petal binary built by the top-level Makefile."""
    return Path(__file__).resolve().parent.parent.parent / "petal"


# Folders scanned for data sets when planning from natural language.
DEFAULT_DATASET_ROOTS = ["../data", "../examples"]


def _resolve_petal(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit).expanduser()
    candidate = default_petal()
    if candidate.exists():
        return candidate
    found = shutil.which("petal")
    if found:
        return Path(found)
    raise SystemExit(
        "找不到 petal 可执行文件。请先 make 构建，或用 --petal 指定路径。"
    )


def cmd_run(args: argparse.Namespace) -> int:
    """Run petal once and file the result away automatically."""
    petal = _resolve_petal(args.petal)
    if not args.rest:
        raise SystemExit("用法: petal-lab run [选项] -- <petal 的参数>")

    conn = db.connect(args.db)
    batch_id = None
    if args.batch:
        batch_id = db.get_or_create_batch(conn, args.batch)

    # petal is invoked as: petal <meta> <data> <options...>
    # --json must precede any -l<learner>, otherwise the learner parser
    # consumes it, so it is inserted right after the two file arguments.
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp:
        out_path = Path(tmp.name)

    rest = list(args.rest)
    # argparse keeps the "--" separator in REMAINDER; petal must not see it.
    while rest and rest[0] == "--":
        rest.pop(0)

    if len(rest) >= 2 and not rest[0].startswith("-") and not rest[1].startswith("-"):
        cmd = [str(petal), rest[0], rest[1], f"--json={out_path}"] + rest[2:]
    else:
        cmd = [str(petal), f"--json={out_path}"] + rest

    if args.dump_predictions:
        cmd.insert(3 if len(rest) >= 2 else 1, "--dump-predictions")

    command = " ".join(str(c) for c in cmd).replace(str(out_path), "<json>")
    print(f"$ {command}", flush=True)

    completed = subprocess.run(cmd, check=False)
    if completed.returncode != 0:
        print(f"petal 以退出码 {completed.returncode} 结束，结果未入库。", file=sys.stderr)
        return completed.returncode

    if not out_path.exists() or out_path.stat().st_size == 0:
        print("petal 没有产生 JSON 输出，结果未入库。", file=sys.stderr)
        return 1

    run_id = ingest.ingest_file(conn, out_path, command=command, batch_id=batch_id)
    out_path.unlink(missing_ok=True)

    run = db.get_run(conn, run_id)
    if run:
        print(f"\n已记录为 run #{run_id}  [{run['dataset']} / {run['mode']}]")
    if args.batch:
        print(f"批次: {args.batch}")
    print(f"查看: petal-lab serve  然后打开 http://127.0.0.1:{args.port or 8899}")
    return 0


def discover_datasets(sources: list[str]) -> list[tuple[str, str]]:
    """Expand directories into (meta, data) pairs.

    A data set is a metadata file with a same-named data file beside it.
    Files without a matching partner are skipped rather than failing the run,
    since real data set folders routinely contain leftovers.
    """
    found: list[tuple[str, str]] = []
    for src in sources:
        path = Path(src)
        if path.is_dir():
            for suffix in builders.META_SUFFIXES:
                for meta in sorted(path.glob(f"*{suffix}")):
                    data = meta.with_suffix(builders.DATA_SUFFIX_BY_META[suffix])
                    if data.exists():
                        found.append((str(meta), str(data)))
            # Orphan data files with no metadata of any known suffix.
            for data in sorted(path.glob("*.pdata")):
                if not data.with_suffix(".pmeta").exists() and not data.with_suffix(".pm").exists():
                    print(f"  跳过（缺少元数据）: {data.name}")
        elif path.suffix in builders.META_SUFFIXES:
            data = path.with_suffix(builders.DATA_SUFFIX_BY_META[path.suffix])
            if data.exists():
                found.append((str(path), str(data)))
    return found


def _run_grid(conn, petal, datasets, learners, extra, name, batch_id) -> int:
    """Run every (data set x learner) pair and file results under `batch_id`."""
    total = len(datasets) * len(learners)
    print(f"批实验「{name}」")
    print(f"  {len(datasets)} 个数据集 × {len(learners)} 个算法 = {total} 次运行\n")

    done = failed = 0
    for meta, data in datasets:
        for learner in learners:
            with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp:
                out_path = Path(tmp.name)
            cmd = [str(petal), meta, data, f"--json={out_path}"] + extra + [f"-l{learner}"]
            label = f"{Path(data).name} · {learner}"
            print(f"  [{done + failed + 1}/{total}] {label}", end="", flush=True)

            completed = subprocess.run(cmd, capture_output=True, check=False)
            if completed.returncode == 0 and out_path.exists() and out_path.stat().st_size:
                ingest.ingest_file(
                    conn,
                    out_path,
                    command=" ".join(str(c) for c in cmd).replace(str(out_path), "<json>"),
                    batch_id=batch_id,
                )
                done += 1
                print("  ✓")
            else:
                failed += 1
                print("  ✗")
            out_path.unlink(missing_ok=True)

    print(f"\n完成 {done} 次，失败 {failed} 次。")
    print(f"查看: petal-lab serve  然后打开「批实验 → {name}」")
    return 0 if failed == 0 else 1


def cmd_batch_run(args: argparse.Namespace) -> int:
    """Run a whole grid: every data set x every learner, filed under one batch."""
    petal = _resolve_petal(args.petal)
    datasets = discover_datasets(args.datasets)
    learners = [x.strip() for x in args.learners.split(",") if x.strip()]

    if not datasets:
        raise SystemExit("没有找到数据集（需要 .pm/.pmeta 与同名数据文件）")
    if not learners:
        raise SystemExit("请用 --learners 指定至少一个算法，例如 --learners nb,aode,tan")

    extra = list(args.extra or [])
    while extra and extra[0] == "--":   # argparse keeps the "--" separator
        extra.pop(0)
    if args.dump_predictions:
        extra.append("--dump-predictions")

    conn = db.connect(args.db)
    batch_id = db.get_or_create_batch(conn, args.name)
    try:
        return _run_grid(conn, petal, datasets, learners, extra, args.name, batch_id)
    finally:
        conn.close()


def _list_datasets(roots: list[str]) -> list[dict]:  # pyright: ignore[reportMissingTypeArgument]
    """Walk the configured roots like the server does, returning {name,meta,data}."""
    out: list[dict] = []  # pyright: ignore[reportMissingTypeArgument]
    seen = set()
    for root in roots:
        base = Path(root)
        if not base.exists():
            continue
        for dirpath, _, filenames in os.walk(str(base), followlinks=True):
            for fn in filenames:
                meta = Path(dirpath) / fn
                suffix = meta.suffix
                if suffix not in builders.DATA_SUFFIX_BY_META:
                    continue
                data = meta.with_suffix(builders.DATA_SUFFIX_BY_META[suffix])
                if not data.is_file() or str(data) in seen:
                    continue
                seen.add(str(data))
                out.append({"name": meta.stem, "meta": str(meta), "data": str(data)})
    return sorted(out, key=lambda d: d["name"].lower())


def cmd_batch_from_text(args: argparse.Namespace) -> int:
    """Parse a natural-language request into a batch and (optionally) run it."""
    datasets = _list_datasets(DEFAULT_DATASET_ROOTS)
    plan = nl_agent.plan_batch(args.text or "", datasets)

    print("自然语言解析结果：")
    print(f"  批次名 : {plan['name']}")
    print(f"  数据集 : {[d['name'] for d in plan['datasets']] or '（未匹配）'}")
    print(f"  算法   : {plan['learners'] or '（未识别）'}")
    print(f"  指标   : {plan['metric']}   模式: {plan['mode']}   折数: {plan.get('folds')}")
    if plan.get("command"):
        print(f"  等效命令: {plan['command']}")
    for w in plan.get("warnings", []):
        print(f"  ⚠ {w}")
    if not args.yes:
        ans = input("确认执行？(y/N) ").strip().lower()
        if ans != "y":
            print("已取消。")
            return 0

    petal = _resolve_petal(args.petal)
    extra = []
    if plan.get("folds"):
        extra.append(f"-x{plan['folds']}")
    if plan.get("discretiser"):
        extra.append(f"-d{plan['discretiser']}")
    if plan.get("mode") == "learningCurves":
        extra.append("-c")
    if args.dump_predictions:
        extra.append("--dump-predictions")

    pairs = [(d["meta"], d["data"]) for d in plan.get("datasets", [])]
    learners = plan.get("learners") or []
    if not pairs or not learners:
        raise SystemExit("解析结果缺少数据集或算法，无法执行。")

    conn = db.connect(args.db)
    batch_id = db.get_or_create_batch(conn, plan["name"])
    try:
        return _run_grid(conn, petal, pairs, learners, extra, plan["name"], batch_id)
    finally:
        conn.close()


def cmd_import(args: argparse.Namespace) -> int:
    conn = db.connect(args.db)
    batch_id = db.get_or_create_batch(conn, args.batch) if args.batch else None
    for path in args.files:
        run_id = ingest.ingest_file(conn, path, command=f"import {path}", batch_id=batch_id)
        run = db.get_run(conn, run_id)
        if run:
            print(f"导入 {path} -> run #{run_id} [{run['dataset']} / {run['mode']}]")
    return 0


def cmd_list(args: argparse.Namespace) -> int:
    conn = db.connect(args.db)
    runs = db.list_runs(conn, limit=args.limit)
    if not runs:
        print("尚无记录。用 petal-lab run 跑一次实验。")
        return 0
    print(f"{'ID':>4}  {'数据集':<28} {'模式':<18} {'学习器':<24} 时间")
    for r in runs:
        learners = ", ".join(r["learners"]) or "-"
        print(f"{r['id']:>4}  {r['dataset'][:28]:<28} {r['mode']:<18} "
              f"{learners[:24]:<24} {r['created_at'][:19]}")
    return 0


def cmd_serve(args: argparse.Namespace) -> int:
    static = args.static or (Path(__file__).resolve().parent.parent / "web" / "dist")
    static = Path(static)
    if not static.exists():
        print(f"提示: 未找到前端构建产物 {static}", file=sys.stderr)
        print("      先执行 cd lab/web && npm install && npm run build", file=sys.stderr)

    db_path = Path(args.db)
    if not db_path.exists():
        db.connect(db_path)  # create the schema up front

    roots = args.datasets_root or [str(default_db().parent.parent / "data"),
                                   str(default_db().parent.parent / "examples")]
    httpd = server.serve(
        db_path,
        static if static.exists() else None,
        args.host,
        args.port,
        petal_path=_resolve_petal(args.petal),
        dataset_roots=roots,
    )
    url = f"http://{args.host}:{args.port}"
    print(f"petal-lab 已启动: {url}")
    print(f"数据库: {db_path}")
    print("Ctrl+C 停止")
    if args.open:
        server.open_browser(args.port)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止")
    finally:
        httpd.server_close()
    return 0


def cmd_reset(args: argparse.Namespace) -> int:
    """Drop every experiment record, keeping the schema."""
    path = Path(args.db)
    if not path.exists():
        print("数据库尚不存在，无需清理。")
        return 0

    if not args.yes:
        answer = input(f"确定清空 {path} 中的全部实验记录？[y/N] ").strip().lower()
        if answer != "y":
            print("已取消。")
            return 0

    conn = db.connect(path)
    conn.executescript(
        """
        DELETE FROM predictions;
        DELETE FROM confusion;
        DELETE FROM curves;
        DELETE FROM metrics;
        DELETE FROM runs;
        DELETE FROM batches;
        """
    )
    conn.commit()
    conn.execute("VACUUM")
    print("已清空全部实验记录。")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="petal-lab",
        description="petal 实验结果管理：执行、入库、可视化。",
    )
    p.add_argument("--db", default=str(default_db()), help="SQLite 数据库路径")
    sub = p.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run", help="运行一次 petal 并自动入库")
    r.add_argument("--batch", help="归入指定批次（不存在则创建）")
    r.add_argument("--petal", help="petal 可执行文件路径")
    r.add_argument("--dump-predictions", action="store_true", help="同时导出逐样本预测")
    r.add_argument("--port", type=int, help="仅用于提示信息中的端口")
    r.add_argument("rest", nargs=argparse.REMAINDER, help="传给 petal 的参数（用 -- 分隔）")
    r.set_defaults(func=cmd_run)

    b = sub.add_parser("batch", help="批处理实验（多数据集 × 多算法）")
    bsub = b.add_subparsers(dest="batch_cmd", required=True)
    br = bsub.add_parser("run", help="跑一整批并归入同一批次")
    br.add_argument("--name", required=True, help="批次名")
    br.add_argument("--datasets", nargs="+", required=True,
                    help="数据集目录（自动找 .pm/.pd）或 .pm 文件")
    br.add_argument("--learners", required=True, help="逗号分隔的算法，如 nb,aode,tan")
    br.add_argument("--petal", help="petal 可执行文件路径")
    br.add_argument("--dump-predictions", action="store_true")
    br.add_argument("extra", nargs=argparse.REMAINDER, help="额外传给 petal 的参数（用 -- 分隔）")
    br.set_defaults(func=cmd_batch_run)
    bt = bsub.add_parser("from-text", help="用自然语言描述一批实验并运行")
    bt.add_argument("text", help="实验需求的自然语言描述")
    bt.add_argument("--yes", "-y", action="store_true", help="跳过确认直接执行")
    bt.add_argument("--petal", help="petal 可执行文件路径")
    bt.add_argument("--dump-predictions", action="store_true")
    bt.set_defaults(func=cmd_batch_from_text)

    i = sub.add_parser("import", help="导入已有的 JSON 结果文件")
    i.add_argument("files", nargs="+")
    i.add_argument("--batch", help="归入指定批次")
    i.set_defaults(func=cmd_import)

    l = sub.add_parser("list", help="列出历史运行")
    l.add_argument("--limit", type=int, default=50)
    l.set_defaults(func=cmd_list)

    z = sub.add_parser("reset", help="清空全部实验记录（保留表结构）")
    z.add_argument("--yes", action="store_true", help="跳过确认")
    z.set_defaults(func=cmd_reset)

    s = sub.add_parser("serve", help="启动本地服务与前端")
    s.add_argument("--host", default="127.0.0.1")
    s.add_argument("--port", type=int, default=8899)
    s.add_argument("--static", help="前端构建产物目录")
    s.add_argument("--petal", help="petal 可执行文件路径")
    s.add_argument("--datasets-root", nargs="+",
                   help="允许从哪些目录加载数据集（默认 data/ 与 examples/）")
    s.add_argument("--open", action="store_true", help="自动打开浏览器")
    s.set_defaults(func=cmd_serve)

    return p


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)
