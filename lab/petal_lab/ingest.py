"""Import a petal ``--json`` payload into the database.

A single petal invocation may cover several learners (learning curves do), so
the learner name is carried on every child row rather than only on the run.
"""

from __future__ import annotations

# pyright: reportExplicitAny=none, reportAny=none, reportUnknownMemberType=none, reportUnknownArgumentType=none, reportUnknownVariableType=none, reportUnknownParameterType=none, reportUnusedCallResult=none, reportImplicitStringConcatenation=none, reportMissingTypeArgument=error

import json
import sqlite3
from pathlib import Path
from typing import Any, Optional

from .db import now


def _load(source: str | Path | dict[str, Any]) -> dict[str, Any]:
    if isinstance(source, dict):
        return source
    path = Path(source)
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def ingest(
    conn: sqlite3.Connection,
    payload: str | Path | dict[str, Any],
    command: str = "",
    batch_id: Optional[int] = None,
    raw_json: Optional[str] = None,
) -> int:
    """Store one result document, returning the new run id."""
    data = _load(payload)

    dataset = data.get("dataset", "unknown")
    mode = data.get("mode", "unknown")
    learners = data.get("learners") or sorted(
        {
            m.get("learner")
            for m in data.get("metrics", [])
            if m.get("learner")
        }
        | {c.get("learner") for c in data.get("curves", []) if c.get("learner")}
    )

    if raw_json is None:
        raw_json = json.dumps(data, ensure_ascii=False)

    cur = conn.execute(
        """
        INSERT INTO runs (batch_id, dataset, mode, learners, command, created_at, raw_json)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (
            batch_id,
            dataset,
            mode,
            json.dumps(sorted(learners), ensure_ascii=False),
            command,
            now(),
            raw_json,
        ),
    )
    run_id = int(cur.lastrowid or 0)

    metrics = [
        (
            run_id,
            m.get("learner", ""),
            m.get("metric", ""),
            int(m.get("trial", 0) or 0),
            int(m.get("fold", -1) if m.get("fold") is not None else -1),
            float(m.get("value", 0.0)),
        )
        for m in data.get("metrics", [])
    ]
    if metrics:
        conn.executemany(
            "INSERT INTO metrics (run_id, learner, metric, trial, fold, value) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            metrics,
        )

    curves = [
        (
            run_id,
            c.get("learner", ""),
            int(c.get("trainSize", 0) or 0),
            int(c.get("trial", 0) or 0),
            c.get("error"),
            c.get("rmse"),
            c.get("logloss"),
        )
        for c in data.get("curves", [])
    ]
    if curves:
        conn.executemany(
            "INSERT INTO curves (run_id, learner, train_size, trial, error, rmse, logloss) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            curves,
        )

    confusion = [
        (
            run_id,
            c.get("learner", ""),
            int(c.get("noClasses", 0) or 0),
            json.dumps(c.get("matrix", []), ensure_ascii=False),
        )
        for c in data.get("confusion", [])
        if c.get("matrix")
    ]
    if confusion:
        conn.executemany(
            "INSERT INTO confusion (run_id, learner, no_classes, matrix) VALUES (?, ?, ?, ?)",
            confusion,
        )

    predictions = []
    for group in data.get("predictions", []):
        learner = group.get("learner", "")
        for inst in group.get("instances", []):
            predictions.append(
                (
                    run_id,
                    learner,
                    int(inst.get("trueClass", 0) or 0),
                    json.dumps(inst.get("probs", []), ensure_ascii=False),
                )
            )
    if predictions:
        conn.executemany(
            "INSERT INTO predictions (run_id, learner, true_class, probs) VALUES (?, ?, ?, ?)",
            predictions,
        )

    conn.commit()
    return run_id


def ingest_file(conn: sqlite3.Connection, path: str | Path, **kwargs: Any) -> int:
    path = Path(path)
    with path.open("r", encoding="utf-8") as fh:
        raw = fh.read()
    return ingest(conn, json.loads(raw), raw_json=raw, **kwargs)
