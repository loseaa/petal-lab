"""SQLite schema and connection helpers.

Only the standard library is used. The schema stores each run twice over:

* ``runs.raw_json`` keeps the exact JSON petal produced, so a run can always be
  re-examined or re-imported even if the structured tables change;
* the normalised tables (metrics/curves/confusion/predictions) exist so that
  cross-dataset aggregation — average ranks, critical-difference diagrams — can
  be expressed as plain SQL instead of re-parsing JSON on every request.
"""

from __future__ import annotations

# pyright: reportExplicitAny=none, reportAny=none, reportUnknownMemberType=none, reportUnknownArgumentType=none, reportUnknownVariableType=none, reportUnknownParameterType=none, reportUnusedCallResult=none, reportImplicitStringConcatenation=none, reportMissingTypeArgument=error

import json
import sqlite3
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Optional

SCHEMA_VERSION = 2

# Timestamps exist so a user can match a run against their own wall clock, and
# this lab is used from one place, so they are recorded in Beijing time rather
# than UTC. The +08:00 offset stays in the string, which keeps each value
# unambiguous and keeps ISO 8601 ordering correct.
BEIJING = timezone(timedelta(hours=8), "CST")


def now() -> str:
    """Current Beijing time, as ISO 8601 with its +08:00 offset."""
    return datetime.now(BEIJING).isoformat(timespec="seconds")

SCHEMA = """
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- A batch groups several runs that belong to one comparison.
CREATE TABLE IF NOT EXISTS batches (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    created_at  TEXT NOT NULL
);

-- One petal invocation.
CREATE TABLE IF NOT EXISTS runs (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    batch_id   INTEGER REFERENCES batches(id) ON DELETE CASCADE,
    dataset    TEXT NOT NULL,
    mode       TEXT NOT NULL,
    learners   TEXT NOT NULL DEFAULT '[]',   -- JSON array of learner names
    command    TEXT NOT NULL DEFAULT '',     -- full command line, for reproducibility
    created_at TEXT NOT NULL,
    raw_json   TEXT                          -- verbatim output of --json
);

-- Tidy long format: one row per (learner, metric, trial, fold).
-- fold = -1 means the pooled/overall value.
CREATE TABLE IF NOT EXISTS metrics (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id  INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    learner TEXT NOT NULL,
    metric  TEXT NOT NULL,
    trial   INTEGER NOT NULL,
    fold    INTEGER NOT NULL,
    value   REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS curves (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id     INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    learner    TEXT NOT NULL,
    train_size INTEGER NOT NULL,
    trial      INTEGER NOT NULL,
    error      REAL,
    rmse       REAL,
    logloss    REAL
);

CREATE TABLE IF NOT EXISTS confusion (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id     INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    learner    TEXT NOT NULL,
    no_classes INTEGER NOT NULL,
    matrix     TEXT NOT NULL   -- JSON array, row-major [true][predicted]
);

-- Per-instance predictions; only present when --dump-predictions was used.
CREATE TABLE IF NOT EXISTS predictions (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id     INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    learner    TEXT NOT NULL,
    true_class INTEGER NOT NULL,
    probs      TEXT NOT NULL   -- JSON array of per-class probabilities
);

CREATE INDEX IF NOT EXISTS idx_metrics_run    ON metrics(run_id);
CREATE INDEX IF NOT EXISTS idx_metrics_lookup ON metrics(learner, metric, fold);
CREATE INDEX IF NOT EXISTS idx_curves_run     ON curves(run_id);
CREATE INDEX IF NOT EXISTS idx_confusion_run  ON confusion(run_id);
CREATE INDEX IF NOT EXISTS idx_predictions_run ON predictions(run_id);
CREATE INDEX IF NOT EXISTS idx_runs_batch     ON runs(batch_id);
CREATE INDEX IF NOT EXISTS idx_runs_dataset   ON runs(dataset);
"""


def connect(path: str | Path) -> sqlite3.Connection:
    """Open the database, creating and migrating the schema if needed."""
    path = Path(path)
    if path.parent and not path.parent.exists():
        path.parent.mkdir(parents=True, exist_ok=True)

    conn = sqlite3.connect(str(path))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute("PRAGMA journal_mode = WAL")
    _migrate(conn)
    return conn


def _migrate(conn: sqlite3.Connection) -> None:
    """Apply the schema, then any pending versioned migrations."""
    conn.executescript(SCHEMA)
    row = conn.execute("SELECT value FROM meta WHERE key = 'schema_version'").fetchone()

    version = int(row["value"]) if row is not None else SCHEMA_VERSION
    if row is None:
        conn.execute(
            "INSERT INTO meta (key, value) VALUES ('schema_version', ?)",
            (str(version),),
        )

    if version < 2:
        _to_beijing_time(conn)
        version = 2
        conn.execute(
            "UPDATE meta SET value = ? WHERE key = 'schema_version'",
            (str(version),),
        )

    conn.commit()


def _to_beijing_time(conn: sqlite3.Connection) -> None:
    """Rewrite timestamps that schema v1 recorded in UTC as Beijing time.

    Left in UTC they would sort and display eight hours off, and mixing the two
    offsets in one column would break the ISO 8601 lexicographic ordering that
    `ORDER BY created_at` relies on. Rows already carrying a non-zero offset, or
    that fail to parse, are left untouched.
    """
    for table in ("batches", "runs"):
        for row in conn.execute(f"SELECT id, created_at FROM {table}").fetchall():
            try:
                stamp = datetime.fromisoformat(row["created_at"])
            except (TypeError, ValueError):
                continue
            if stamp.tzinfo is None:
                stamp = stamp.replace(tzinfo=timezone.utc)
            if stamp.utcoffset() != timedelta(0):
                continue
            conn.execute(
                f"UPDATE {table} SET created_at = ? WHERE id = ?",
                (stamp.astimezone(BEIJING).isoformat(timespec="seconds"), row["id"]),
            )
    conn.commit()


def create_batch(conn: sqlite3.Connection, name: str, description: str = "") -> int:
    cur = conn.execute(
        "INSERT INTO batches (name, description, created_at) VALUES (?, ?, ?)",
        (name, description, now()),
    )
    conn.commit()
    return int(cur.lastrowid or 0)


def get_or_create_batch(conn: sqlite3.Connection, name: str) -> int:
    row = conn.execute("SELECT id FROM batches WHERE name = ?", (name,)).fetchone()
    if row:
        return int(row["id"])
    return create_batch(conn, name)


def list_batches(conn: sqlite3.Connection) -> list[dict[str, Any]]:
    rows = conn.execute(
        """
        SELECT b.*, COUNT(r.id) AS run_count
        FROM batches b LEFT JOIN runs r ON r.batch_id = b.id
        GROUP BY b.id ORDER BY b.created_at DESC
        """
    ).fetchall()
    return [dict(r) for r in rows]


def list_runs(
    conn: sqlite3.Connection,
    dataset: Optional[str] = None,
    learner: Optional[str] = None,
    mode: Optional[str] = None,
    batch_id: Optional[int] = None,
    limit: int = 200,
) -> list[dict[str, Any]]:
    """Return run summaries, newest first."""
    sql = """
        SELECT r.id, r.dataset, r.mode, r.learners, r.command, r.created_at,
               r.batch_id, b.name AS batch_name
        FROM runs r LEFT JOIN batches b ON b.id = r.batch_id
        WHERE 1=1
    """
    params: list[Any] = []
    if dataset:
        sql += " AND r.dataset LIKE ?"
        params.append(f"%{dataset}%")
    if learner:
        sql += " AND r.learners LIKE ?"
        params.append(f"%{learner}%")
    if mode:
        sql += " AND r.mode = ?"
        params.append(mode)
    if batch_id is not None:
        sql += " AND r.batch_id = ?"
        params.append(batch_id)
    sql += " ORDER BY r.created_at DESC, r.id DESC LIMIT ?"
    params.append(limit)

    rows = conn.execute(sql, params).fetchall()
    out = []
    for r in rows:
        d = dict(r)
        d["learners"] = json.loads(d["learners"] or "[]")
        out.append(d)
    return out


def get_run(conn: sqlite3.Connection, run_id: int) -> Optional[dict[str, Any]]:
    row = conn.execute("SELECT * FROM runs WHERE id = ?", (run_id,)).fetchone()
    if not row:
        return None
    d = dict(row)
    d["learners"] = json.loads(d["learners"] or "[]")
    return d


def delete_run(conn: sqlite3.Connection, run_id: int) -> None:
    conn.execute("DELETE FROM runs WHERE id = ?", (run_id,))
    conn.commit()
