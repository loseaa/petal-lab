"""Local HTTP server: JSON API over SQLite plus the built front-end.

Deliberately uses only ``http.server``/``sqlite3`` — this is a single-user tool
that runs on a laptop, so a framework would be dead weight. The server binds to
127.0.0.1 by default and never leaves the machine.
"""

from __future__ import annotations

# pyright: reportExplicitAny=none, reportAny=none, reportUnknownMemberType=none, reportUnknownArgumentType=none, reportUnknownVariableType=none, reportUnknownParameterType=none, reportUnusedCallResult=none, reportImplicitStringConcatenation=none, reportMissingTypeArgument=error

import json
import os
import sqlite3
import tempfile
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

from . import builders, db, jobs, stats, nl_agent, dataset_meta

# Folders the web UI is allowed to load data sets from.
DEFAULT_DATASET_ROOTS = [
    "../data",
    "../examples",
]


class Api:
    """Thin layer turning HTTP requests into rows."""

    def __init__(self, db_path: str | Path, petal_path: str | Path,
                 dataset_roots: list[str] | None = None):
        self.db_path = str(db_path)
        self.petal_path = str(petal_path)
        self.dataset_roots = dataset_roots or DEFAULT_DATASET_ROOTS
        self.manager = jobs.JobManager(self.db_path, self.petal_path)

    def _conn(self) -> sqlite3.Connection:
        return db.connect(self.db_path)

    # ------------------------------------------------------------------- jobs

    def submit_job(self, spec: dict) -> dict:
        argv, meta = builders.build_command(spec, self.dataset_roots)
        cmd = [self.petal_path] + argv

        # petal writes its structured result here; the job files it away when done.
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp:
            out_path = Path(tmp.name)

        # --json must precede -l<learner> or the learner parser swallows it.
        insert_at = 3 if len(argv) >= 2 and not argv[0].startswith("-") else 1
        cmd.insert(insert_at, f"--json={out_path}")

        display = " ".join(str(c) for c in cmd).replace(str(out_path), "<json>")
        job = self.manager.submit(
            cmd,
            display=display,
            dataset=meta.get("dataset", ""),
            learner=", ".join(meta.get("learners", [])),
            batch_name=spec.get("batch") or None,
            total=meta.get("folds"),
        )
        job._result_path = str(out_path)
        return job.snapshot()

    def list_jobs(self) -> list[dict]:
        return self.manager.list_jobs()

    def get_job(self, job_id: int):
        return self.manager.get(job_id)

    def cancel_job(self, job_id: int) -> dict:
        return {"ok": self.manager.cancel(job_id)}

    def available_datasets(self) -> list[dict]:
        """Data set files on disk that may be used to launch a run.

        Uses os.walk with followlinks=True: data/uci is a symlink to the lab
        data folder, and Path.rglob() would silently skip it.
        """
        out = []
        seen = set()
        for root in self.dataset_roots:
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
                    out.append({
                        "name": meta.stem,
                        "meta": str(meta),
                        "data": str(data),
                        "size": data.stat().st_size,
                    })
        return sorted(out, key=lambda d: d["name"].lower())

    # ------------------------------------------------------------------ reads

    def dataset_analysis(self) -> list[dict]:
        """Per-dataset metadata used by the data-analysis page and batch sorting."""
        out = []
        for ds in self.available_datasets():
            m = dataset_meta.analyze_dataset(ds["meta"], ds["data"])
            out.append({
                "name": ds["name"], "meta": ds["meta"], "data": ds["data"], "size": ds["size"],
                "n_attributes": m["n_attributes"], "n_predictors": m["n_predictors"],
                "n_class_attrs": m["n_class_attrs"], "n_classes": m["n_classes"],
                "class_attr_name": m["class_attr_name"], "class_labels": m["class_labels"],
                "class_type": m["class_type"], "n_records": m["n_records"],
            })
        return out

    def dataset_detail(self, name: str) -> dict | None:
        """Full analysis of one data set, including the class distribution."""
        for ds in self.available_datasets():
            if ds["name"] == name:
                m = dataset_meta.analyze_dataset(ds["meta"], ds["data"], compute_distribution=True)
                return {
                    "name": ds["name"], "meta": ds["meta"], "data": ds["data"], "size": ds["size"],
                    "n_attributes": m["n_attributes"], "n_predictors": m["n_predictors"],
                    "n_class_attrs": m["n_class_attrs"], "n_classes": m["n_classes"],
                    "class_attr_name": m["class_attr_name"], "class_labels": m["class_labels"],
                    "class_type": m["class_type"], "n_records": m["n_records"],
                    "attributes": m["attributes"], "class_distribution": m["class_distribution"],
                }
        return None

    def datasets(self) -> list[dict]:
        conn = self._conn()
        try:
            rows = conn.execute(
                """
                SELECT dataset, COUNT(*) AS runs, MIN(created_at) AS first_seen,
                       MAX(created_at) AS last_seen
                FROM runs GROUP BY dataset ORDER BY dataset
                """
            ).fetchall()
            return [dict(r) for r in rows]
        finally:
            conn.close()

    def runs(self, params: dict) -> list[dict]:
        conn = self._conn()
        try:
            return db.list_runs(
                conn,
                dataset=params.get("dataset") or None,
                learner=params.get("learner") or None,
                mode=params.get("mode") or None,
                batch_id=int(params["batch_id"]) if params.get("batch_id") else None,
            )
        finally:
            conn.close()

    def run_detail(self, run_id: int) -> dict | None:
        conn = self._conn()
        try:
            run = db.get_run(conn, run_id)
            if not run:
                return None

            metrics = [
                dict(r)
                for r in conn.execute(
                    "SELECT learner, metric, trial, fold, value FROM metrics "
                    "WHERE run_id = ? ORDER BY learner, metric, trial, fold",
                    (run_id,),
                )
            ]
            curves = [
                dict(r)
                for r in conn.execute(
                    "SELECT learner, train_size, trial, error, rmse, logloss FROM curves "
                    "WHERE run_id = ? ORDER BY learner, train_size, trial",
                    (run_id,),
                )
            ]
            confusion = []
            for r in conn.execute(
                "SELECT learner, no_classes, matrix FROM confusion WHERE run_id = ?",
                (run_id,),
            ):
                d = dict(r)
                d["matrix"] = json.loads(d["matrix"])
                confusion.append(d)

            pred_count = conn.execute(
                "SELECT COUNT(*) AS n FROM predictions WHERE run_id = ?", (run_id,)
            ).fetchone()["n"]

            return {
                **run,
                "metrics": metrics,
                "curves": curves,
                "confusion": confusion,
                "prediction_count": pred_count,
            }
        finally:
            conn.close()

    def predictions(self, run_id: int, limit: int = 20000) -> list[dict]:
        """Per-instance predictions, down-sampled if there are very many.

        ROC curves are visually identical well before 20k points, and shipping
        millions of rows to the browser would stall it.
        """
        conn = self._conn()
        try:
            total = conn.execute(
                "SELECT COUNT(*) AS n FROM predictions WHERE run_id = ?", (run_id,)
            ).fetchone()["n"]
            step = max(1, (total + limit - 1) // limit)
            rows = conn.execute(
                "SELECT learner, true_class, probs FROM predictions "
                "WHERE run_id = ? AND (id % ?) = 0",
                (run_id, step),
            ).fetchall()
            return [
                {
                    "learner": r["learner"],
                    "trueClass": r["true_class"],
                    "probs": json.loads(r["probs"]),
                }
                for r in rows
            ]
        finally:
            conn.close()

    def batches(self) -> list[dict]:
        conn = self._conn()
        try:
            rows = db.list_batches(conn)
            out = []
            for d in rows:
                d = dict(d)
                d["progress"] = self.manager.batch_progress(d["id"])
                out.append(d)
            return out
        finally:
            conn.close()

    def batch_progress(self, batch_id: int) -> dict:
        return self.manager.batch_progress(batch_id)

    def compare(self, params: dict) -> dict:
        conn = self._conn()
        try:
            metric = params.get("metric", "0-1_loss")
            batch_id = int(params["batch_id"]) if params.get("batch_id") else None
            run_ids = None
            if params.get("run_ids"):
                run_ids = [int(x) for x in params["run_ids"].split(",") if x]
            return stats.compare_batch(conn, batch_id, metric, run_ids)
        finally:
            conn.close()

    # ----------------------------------------------------------------- writes

    def create_batch(self, payload: dict) -> dict:
        conn = self._conn()
        try:
            bid = db.create_batch(
                conn, payload.get("name", "未命名批次"), payload.get("description", "")
            )
            return {"id": bid}
        finally:
            conn.close()

    def assign_batch(self, run_id: int, batch_id: int | None) -> dict:
        conn = self._conn()
        try:
            conn.execute("UPDATE runs SET batch_id = ? WHERE id = ?", (batch_id, run_id))
            conn.commit()
            return {"ok": True}
        finally:
            conn.close()

    def plan_nl_batch(self, text: str) -> dict:
        datasets = self.available_datasets()
        return nl_agent.plan_batch(text or "", datasets)

    def run_batch_plan(self, plan: dict) -> dict:
        """Launch a parsed natural-language plan: one job per data set x learner."""
        learners = plan.get("learners") or []
        if not learners:
            raise ValueError("未选择任何算法，无法创建批次")
        datasets = plan.get("datasets") or []
        if not datasets:
            raise ValueError("未选择任何数据集，无法创建批次")
        total = len(datasets) * len(learners)

        conn = db.connect(self.db_path)
        try:
            bid = db.create_batch(
                conn, plan.get("name", "nl-batch"), plan.get("description", ""),
                total_jobs=total,
            )
        finally:
            conn.close()
        mode = plan.get("mode", "xval")
        folds = plan.get("folds") or 10
        discretiser = plan.get("discretiser") or ""
        jobs_out = []
        for ds in plan.get("datasets", []):
            for learner in learners:
                spec = {
                    "meta": ds["meta"], "data": ds["data"], "learners": [learner],
                    "mode": mode, "folds": folds, "discretiser": discretiser,
                    "batch": bid,
                }
                argv, meta = builders.build_command(spec, self.dataset_roots)
                cmd = [self.petal_path] + argv
                with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp:
                    out_path = Path(tmp.name)
                insert_at = 3 if len(argv) >= 2 and not argv[0].startswith("-") else 1
                cmd.insert(insert_at, f"--json={out_path}")
                display = " ".join(str(c) for c in cmd).replace(str(out_path), "<json>")
                job = self.manager.submit(
                    cmd, display=display, dataset=meta.get("dataset", ""),
                    learner=learner, batch_name=plan.get("name"),
                    batch_id=bid, total=folds,
                )
                # 与 submit_job 一样：把 petal 的 --json 临时文件路径交给 job，
                # 否则任务跑完后 _file_result 拿不到结果文件，运行永远不入库。
                job._result_path = str(out_path)
                jobs_out.append(job.snapshot())
        return {"batch_id": bid, "jobs": jobs_out}

    def delete_run(self, run_id: int) -> dict:
        conn = self._conn()
        try:
            db.delete_run(conn, run_id)
            return {"ok": True}
        finally:
            conn.close()


class Handler(BaseHTTPRequestHandler):
    api: Api
    static_dir: Path | None = None

    server_version = "petal-lab/0.1"

    # ------------------------------------------------------------- plumbing

    def log_message(self, fmt: str, *args) -> None:  # keep the console quiet
        pass

    def _send_json(self, obj, status: int = 200) -> None:
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path: Path) -> None:
        if not path.exists() or path.is_dir():
            # SPA fallback: unknown paths are handled by the front-end router.
            index = (self.static_dir or Path(".")) / "index.html"
            if index.exists():
                self._send_file(index)
            else:
                self._send_json({"error": "not found"}, 404)
            return

        types = {
            ".html": "text/html; charset=utf-8",
            ".js": "text/javascript; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".svg": "image/svg+xml",
            ".json": "application/json; charset=utf-8",
            ".woff2": "font/woff2",
            ".png": "image/png",
            ".ico": "image/x-icon",
        }
        body = path.read_bytes()
        self.send_response(200)
        self.send_header(
            "Content-Type", types.get(path.suffix.lower(), "application/octet-stream")
        )
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _body(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        if not length:
            return {}
        return json.loads(self.rfile.read(length).decode("utf-8"))

    # ---------------------------------------------------------------- routing

    def do_GET(self) -> None:  # noqa: N802 - required by BaseHTTPRequestHandler
        url = urlparse(self.path)
        path, params = url.path, {k: v[0] for k, v in parse_qs(url.query).items()}

        try:
            if path.startswith("/api/"):
                self._api_get(path, params)
            else:
                rel = path.lstrip("/") or "index.html"
                self._send_file((self.static_dir or Path(".")) / rel)
        except Exception as exc:  # noqa: BLE001 - report, never crash the server
            self._send_json({"error": str(exc)}, 500)

    # ------------------------------------------------------------------- SSE

    def _stream_job(self, run_id: int, since: int) -> None:
        """Server-Sent Events: push log lines until the job reaches a terminal state."""
        job = self.api.get_job(run_id)
        if job is None:
            return self._send_json({"error": "job not found"}, 404)

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "keep-alive")
        self.send_header("X-Accel-Buffering", "no")   # disable proxy buffering
        self.end_headers()

        def emit(event: str, payload: str) -> None:
            chunk = f"event: {event}\ndata: {payload}\n\n"
            self.wfile.write(chunk.encode("utf-8"))

        try:
            emit("snapshot", json.dumps(job.snapshot(), ensure_ascii=False))
            last_seq = since
            idle = 0
            while True:
                lines = job.tail(last_seq)
                if lines:
                    for line in lines:
                        emit("log", line)
                    last_seq = job.log_seq
                    idle = 0
                snap = job.snapshot()
                emit("status", json.dumps(snap, ensure_ascii=False))

                if snap["status"] in jobs.TERMINAL:
                    emit("done", json.dumps(snap, ensure_ascii=False))
                    break

                idle += 1
                if idle > 600:      # ~3 min without output: keep alive anyway
                    idle = 0
                    emit("ping", "")
                self.wfile.flush()
                time.sleep(0.4)
        except (BrokenPipeError, ConnectionResetError):
            pass   # client went away; the job keeps running regardless

    def _api_get(self, path: str, params: dict) -> None:
        api = self.api
        if path == "/api/datasets":
            return self._send_json({"datasets": api.datasets()})
        if path == "/api/datasets/available":
            return self._send_json({"datasets": api.available_datasets()})
        if path == "/api/datasets/analysis":
            return self._send_json({"datasets": api.dataset_analysis()})
        if path.startswith("/api/datasets/analysis/"):
            name = unquote(path.rstrip("/").split("/")[-1])
            if not name:
                return self._send_json({"error": "missing dataset name"}, 400)
            detail = api.dataset_detail(name)
            if detail is None:
                return self._send_json({"error": "dataset not found"}, 404)
            return self._send_json(detail)
        if path == "/api/jobs":
            return self._send_json({"jobs": api.list_jobs()})

        if path.startswith("/api/jobs/"):
            parts = path.split("/")
            try:
                job_id = int(parts[3])
            except (IndexError, ValueError):
                return self._send_json({"error": "bad job id"}, 400)
            if len(parts) == 5 and parts[4] == "stream":
                since = int(params.get("since", 0) or 0)
                return self._stream_job(job_id, since)
            job = api.get_job(job_id)
            if job is None:
                return self._send_json({"error": "job not found"}, 404)
            return self._send_json(job.snapshot())
        if path == "/api/runs":
            return self._send_json({"runs": api.runs(params)})
        if path == "/api/batches":
            return self._send_json({"batches": api.batches()})
        if path.startswith("/api/batches/") and path.endswith("/progress"):
            try:
                bid = int(path.rstrip("/").split("/")[-2])
            except (IndexError, ValueError):
                return self._send_json({"error": "bad batch id"}, 400)
            return self._send_json(api.batch_progress(bid))
        if path == "/api/compare":
            return self._send_json(api.compare(params))

        parts = path.split("/")
        if len(parts) >= 4 and parts[2] == "runs":
            run_id = int(parts[3])
            if len(parts) == 4:
                detail = api.run_detail(run_id)
                if detail is None:
                    return self._send_json({"error": "run not found"}, 404)
                return self._send_json(detail)
            if parts[4] == "predictions":
                return self._send_json({"instances": api.predictions(run_id)})

        self._send_json({"error": "unknown endpoint"}, 404)

    def do_POST(self) -> None:  # noqa: N802
        url = urlparse(self.path)
        try:
            if url.path == "/api/jobs":
                spec = self._body()
                job = self.api.submit_job(spec)
                return self._send_json(job, 201)
            if url.path.endswith("/cancel") and url.path.startswith("/api/jobs/"):
                parts = url.path.split("/")
                return self._send_json(self.api.cancel_job(int(parts[3])))
            if url.path == "/api/batches":
                return self._send_json(self.api.create_batch(self._body()), 201)
            if url.path == "/api/nl-batch":
                return self._send_json(self.api.plan_nl_batch(self._body().get("text", "")), 200)
            if url.path == "/api/nl-batch/run":
                return self._send_json(self.api.run_batch_plan(self._body()), 201)
            if url.path == "/api/batch/run":
                return self._send_json(self.api.run_batch_plan(self._body()), 201)
            parts = url.path.split("/")
            if len(parts) == 5 and parts[2] == "runs" and parts[4] == "batch":
                payload = self._body()
                return self._send_json(
                    self.api.assign_batch(int(parts[3]), payload.get("batch_id"))
                )
            self._send_json({"error": "unknown endpoint"}, 404)
        except Exception as exc:  # noqa: BLE001
            self._send_json({"error": str(exc)}, 500)

    def do_DELETE(self) -> None:  # noqa: N802
        parts = urlparse(self.path).path.split("/")
        try:
            if len(parts) == 4 and parts[2] == "runs":
                return self._send_json(self.api.delete_run(int(parts[3])))
            self._send_json({"error": "unknown endpoint"}, 404)
        except Exception as exc:  # noqa: BLE001
            self._send_json({"error": str(exc)}, 500)


def serve(db_path: str | Path, static_dir: str | Path | None, host: str, port: int,
          petal_path: str | Path, dataset_roots: list[str] | None = None):
    handler = type(
        "BoundHandler",
        (Handler,),
        {
            "api": Api(db_path, petal_path, dataset_roots),
            "static_dir": Path(static_dir) if static_dir else None,
        },
    )
    httpd = ThreadingHTTPServer((host, port), handler)
    return httpd


def open_browser(port: int) -> None:
    import webbrowser

    webbrowser.open(f"http://127.0.0.1:{port}")
