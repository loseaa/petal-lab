"""Background job execution for the "run an experiment from the web UI" flow.

Design notes
------------
* Standard library only: threads + subprocess + pty.
* Output is read through a **pseudo-terminal**. When stdout is a pipe, C stdio
  switches to full buffering and petal's output only appears in 4 KB chunks (or
  all at once for short runs); a pty makes petal think it is attached to a
  terminal, restoring line buffering so lines arrive as they are produced.
* Jobs keep a bounded log buffer so a client reconnecting (or arriving after
  completion) still sees the full output.
"""

from __future__ import annotations

# pyright: reportExplicitAny=none, reportAny=none, reportUnknownMemberType=none, reportUnknownArgumentType=none, reportUnknownVariableType=none, reportUnknownParameterType=none, reportUnusedCallResult=none, reportImplicitStringConcatenation=none, reportMissingTypeArgument=error

import os
import pty
import re
import select
import subprocess
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Deque, Optional

from .db import now
from .job_state import (
    PENDING, RUNNING, DONE, FAILED, CANCELLED, TERMINAL,
    transition, is_terminal, InvalidTransition,
)

LOG_LIMIT = 3000          # lines retained per job

# petal -v2 prints e.g. "0-1 loss (fold 5): 0.0650"; use it for real progress.
FOLD_RE = re.compile(r"fold\s+(\d+)", re.IGNORECASE)
CURVE_RE = re.compile(r"trainsize|sample size\s*[:=]\s*(\d+)", re.IGNORECASE)


@dataclass
class Job:
    id: int
    command: list[str]
    display: str                      # command line with any temp path elided
    dataset: str = ""
    learner: str = ""
    batch_name: Optional[str] = None
    batch_id: Optional[int] = None
    status: str = "pending"           # pending|running|done|failed|cancelled
    exit_code: Optional[int] = None
    run_id: Optional[int] = None      # set once the result is filed away
    error: Optional[str] = None
    created_at: str = field(default_factory=now)
    started_at: Optional[str] = None
    finished_at: Optional[str] = None
    finished_ts: float = 0.0            # epoch seconds, for bounded retention

    log: Deque[str] = field(default_factory=lambda: deque(maxlen=LOG_LIMIT))
    log_seq: int = 0                  # total lines ever appended
    progress: dict = field(default_factory=dict)

    _proc: Optional[subprocess.Popen] = None
    _result_path: Optional[str] = None   # temp JSON written by petal --json
    _lock: threading.Lock = field(default_factory=threading.Lock, repr=False)

    # ------------------------------------------------------------------ write

    def append(self, line: str) -> None:
        with self._lock:
            self.log.append(line.rstrip("\r\n"))
            self.log_seq += 1
            self._update_progress(line)

    def _update_progress(self, line: str) -> None:
        m = FOLD_RE.search(line)
        if m:
            n = int(m.group(1))            # petal reports folds 0-based (0..total-1)
            total = self.progress.get("total")
            done = n + 1                   # folds finished so far (1-based)
            pct = round(100 * done / total) if total else None
            self.progress = {
                "kind": "fold",
                "current": done,
                "total": total,
                "percent": pct,
                "label": (f"交叉验证 第 {done} / {total} 折"
                          if total else f"交叉验证 第 {done} 折"),
            }
            return
        m = re.search(r"^\s*(\d+)\s*(?:/|,)\s*(\d+)", line)
        if m:
            self.progress = {
                "kind": "count",
                "current": int(m.group(1)),
                "total": int(m.group(2)),
                "label": f"{m.group(1)} / {m.group(2)}",
            }

    def set_total(self, total: int) -> None:
        if total and self.progress.get("total") is None:
            self.progress["total"] = total

    # ------------------------------------------------------------------- read

    def tail(self, since: int = 0) -> list[str]:
        """Log lines appended after `since` (a log_seq value)."""
        with self._lock:
            if since >= self.log_seq:
                return []
            skip = self.log_seq - since - len(self.log)
            if skip < 0:
                skip = 0
            return list(self.log)[skip + (len(self.log) - (self.log_seq - since)):]

    def snapshot(self) -> dict:
        with self._lock:
            return {
                "id": self.id,
                "status": self.status,
                "command": self.display,
                "dataset": self.dataset,
                "learner": self.learner,
                "batch": self.batch_name,
                "batch_id": self.batch_id,
                "exit_code": self.exit_code,
                "run_id": self.run_id,
                "error": self.error,
                "created_at": self.created_at,
                "started_at": self.started_at,
                "finished_at": self.finished_at,
                "log_seq": self.log_seq,
                "progress": self.progress,
            }


class JobManager:
    """Runs jobs on background threads, at most `max_concurrent` at a time."""

    def __init__(self, db_path, petal_path, max_concurrent: int = 1):
        self.db_path = str(db_path)
        self.petal_path = str(petal_path)
        self.max_concurrent = max_concurrent
        self._job_grace = 300           # 终态任务最多在内存保留 5 分钟，随后回收
        self._jobs: dict[int, Job] = {}
        self._next_id = 1
        self._lock = threading.Lock()
        self._running = 0

    # -------------------------------------------------------------- lifecycle

    def submit(self, command: list[str], display: str, dataset: str = "",
               learner: str = "", batch_name: Optional[str] = None,
               batch_id: Optional[int] = None,
               total: Optional[int] = None) -> Job:
        self._sweep()
        with self._lock:
            job = Job(id=self._next_id, command=command, display=display,
                      dataset=dataset, learner=learner, batch_name=batch_name,
                      batch_id=batch_id)
            self._next_id += 1
            if total:
                job.progress = {"kind": "fold", "current": 0, "total": total, "label": "准备中"}
            self._jobs[job.id] = job

        t = threading.Thread(target=self._run, args=(job,), daemon=True)
        t.start()
        return job

    def get(self, job_id: int) -> Optional[Job]:
        return self._jobs.get(job_id)

    def list_jobs(self) -> list[dict]:
        self._sweep()
        with self._lock:
            jobs = sorted(self._jobs.values(), key=lambda j: j.created_at, reverse=True)
        return [j.snapshot() for j in jobs]

    def _record_terminal(self, job: "Job") -> None:
        """Release heavy per-job resources and record failures durably.

        Called once when a job reaches a terminal state. The large log buffer is
        cleared immediately so memory does not grow with the number of completed
        batches; the lightweight snapshot (status/run_id/error/timestamps) is kept
        until `_sweep` removes the object, so progress and recent logs stay visible.
        """
        job.finished_ts = time.time()
        if job.batch_id is not None and job.status in ("failed", "cancelled"):
            try:
                from . import db as _db
                conn = _db.connect(self.db_path)
                try:
                    conn.execute(
                        "UPDATE batches SET failed_jobs = failed_jobs + 1 WHERE id = ?",
                        (job.batch_id,),
                    )
                    conn.commit()
                finally:
                    conn.close()
            except Exception:  # noqa: BLE001 - 失败计数丢失不应影响主流程
                pass
        with job._lock:
            try:
                job.log.clear()
            except Exception:  # noqa: BLE001
                pass
            job._result_path = None
            job._proc = None

    def _sweep(self) -> None:
        """Drop terminal jobs whose results have been held past the grace window.

        Keeps memory bounded: a finished job lingers briefly (so a client can still
        reconnect to its stream/log) and is then removed. Active jobs are never
        pruned, and progress is read from the database, not from these objects.
        """
        if not self._job_grace:
            return
        cutoff = time.time() - self._job_grace
        with self._lock:
            stale = [
                jid for jid, j in self._jobs.items()
                if j.status in TERMINAL and j.finished_ts and j.finished_ts < cutoff
            ]
            for jid in stale:
                del self._jobs[jid]

    def batch_progress(self, batch_id: int) -> dict:
        """Live progress for a batch, read from the database (survives restart).

        `total_jobs`/`failed_jobs` are persisted when the batch is created and when
        tasks fail; the successful run count comes from the runs table. Only the
        still-active (running/pending) tasks are taken from in-memory job objects.
        """
        self._sweep()
        with self._lock:
            batch_jobs = [j for j in self._jobs.values() if j.batch_id == batch_id]
            active = [j for j in batch_jobs if j.status not in TERMINAL]
            running = sum(1 for j in active if j.status == "running")
            pending = sum(1 for j in active if j.status == "pending")
            tasks = [
                {
                    "id": j.id,
                    "dataset": j.dataset,
                    "learner": j.learner,
                    "status": j.status,
                    "display": j.display,
                    "started_at": j.started_at,
                    "finished_at": j.finished_at,
                    "error": j.error,
                }
                for j in batch_jobs
            ]
            tasks.sort(key=lambda t: (0 if t["status"] in ("running", "pending") else 1, t["id"]))
        try:
            from . import db as _db
            conn = _db.connect(self.db_path)
            try:
                row = conn.execute(
                    "SELECT total_jobs, failed_jobs FROM batches WHERE id = ?",
                    (batch_id,),
                ).fetchone()
                done = conn.execute(
                    "SELECT COUNT(*) AS c FROM runs WHERE batch_id = ?", (batch_id,)
                ).fetchone()["c"]
            finally:
                conn.close()
        except Exception:  # noqa: BLE001
            row, done = None, 0
        if not row:
            return {
                "batch_id": batch_id, "total": 0, "done": done,
                "running": running, "pending": pending, "failed": 0,
                "percent": 100, "complete": True, "tasks": tasks,
            }
        total = row["total_jobs"] or 0
        failed = row["failed_jobs"] or 0
        if total == 0:
            total_disp = done
            percent = 100 if done else 100
        else:
            total_disp = total
            percent = round(100 * done / total) if total else 100
        complete = (running + pending) == 0
        return {
            "batch_id": batch_id, "total": total_disp, "done": done,
            "running": running, "pending": pending, "failed": failed,
            "percent": percent, "complete": complete, "tasks": tasks,
        }

    def _set_state(self, job: "Job", to: str) -> bool:
        """Move a job to `to` through the task state machine.

        Returns True if the state actually changed. Illegal transitions are
        rejected (and logged) so a stray event cannot corrupt the lifecycle;
        the job simply keeps its current state instead of becoming inconsistent.
        """
        with job._lock:
            frm = job.status
            if frm == to:
                return False
            try:
                transition(frm, to)
            except InvalidTransition:
                import logging
                logging.getLogger(__name__).warning(
                    "job %s 忽略非法状态转移 %s -> %s", job.id, frm, to
                )
                return False
            job.status = to
            if to == RUNNING and job.started_at is None:
                job.started_at = now()
            if is_terminal(to) and job.finished_at is None:
                job.finished_at = now()
            return True

    def cancel(self, job_id: int) -> bool:
        job = self._jobs.get(job_id)
        if not job or is_terminal(job.status):
            return False
        self._set_state(job, CANCELLED)
        if job._proc and job._proc.poll() is None:
            job._proc.terminate()
        return True

    # ------------------------------------------------------------------ runner

    def _run(self, job: Job) -> None:
        self._set_state(job, RUNNING)

        master_fd = slave_fd = None
        try:
            master_fd, slave_fd = pty.openpty()
            proc = subprocess.Popen(
                job.command,
                stdout=slave_fd,
                stderr=slave_fd,
                stdin=subprocess.DEVNULL,
                close_fds=True,
            )
            job._proc = proc
            os.close(slave_fd)   # only the child needs the slave end
            slave_fd = None

            buf = b""
            while True:
                if job.status == CANCELLED:
                    break
                r, _, _ = select.select([master_fd], [], [], 0.3)
                if r:
                    try:
                        chunk = os.read(master_fd, 8192)
                    except OSError:
                        break
                    if not chunk:
                        break
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        job.append(line.decode("utf-8", errors="replace"))
                elif proc.poll() is not None:
                    break

            if buf.strip():
                job.append(buf.decode("utf-8", errors="replace"))

            job.exit_code = proc.wait()
            if job.status == CANCELLED:
                pass  # 已在终态，保持 cancelled
            elif job.exit_code != 0:
                self._set_state(job, FAILED)
                job.error = f"petal 退出码 {job.exit_code}"
            # 退出码为 0：保持 RUNNING，由 _file_result 在结果入库后转入 DONE / FAILED
        except FileNotFoundError:
            self._set_state(job, FAILED)
            job.error = f"找不到可执行文件: {job.command[0]}"
        except Exception as exc:                      # noqa: BLE001
            self._set_state(job, FAILED)
            job.error = str(exc)
        finally:
            for fd in (master_fd, slave_fd):
                if fd is not None:
                    try:
                        os.close(fd)
                    except OSError:
                        pass
            job.finished_at = now()
            if job.status == CANCELLED:
                job.error = job.error or "已取消"
            self._file_result(job)
            self._record_terminal(job)

    def _file_result(self, job: Job) -> None:
        """Persist a successful run into the database and finalise the state.

        The task only becomes ``done`` once petal's ``--json`` result is actually
        ingested; a missing result file or an ingest error moves it to ``failed``
        so the state machine never reports a misleading ``done`` for an empty run.
        """
        if job.status != RUNNING:
            return
        if not job._result_path:
            job.error = "缺少结果文件路径"
            self._set_state(job, FAILED)
            return
        try:
            from . import db, ingest

            path = Path(job._result_path)
            if not path.exists() or path.stat().st_size == 0:
                job.error = "petal 未产生结果文件"
                self._set_state(job, FAILED)
                return
            conn = db.connect(self.db_path)
            if job.batch_id is not None:
                batch_id = job.batch_id
            elif job.batch_name:
                batch_id = db.get_or_create_batch(conn, job.batch_name)
            else:
                batch_id = None
            job.run_id = ingest.ingest_file(conn, path, command=job.display, batch_id=batch_id)
            conn.close()
            self._set_state(job, DONE)
        except Exception as exc:                      # noqa: BLE001
            job.error = f"入库失败: {exc}"
            self._set_state(job, FAILED)
        finally:
            try:
                if job._result_path and os.path.exists(job._result_path):
                    os.unlink(job._result_path)
            except OSError:
                pass
