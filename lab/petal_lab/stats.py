"""Cross-dataset statistics for batch comparisons.

Why ranks instead of raw metrics
--------------------------------
Averaging an error rate across data sets is misleading: a 0.30 error on a hard
data set may be excellent while 0.05 on an easy one is poor. The standard remedy
(Demšar, JMLR 2006) is to rank the algorithms *within* each data set and then
average those ranks, which removes the difficulty of the data set itself.

This module implements that recipe:

* average ranks per algorithm,
* the Friedman test (are the algorithms different at all?),
* the Nemenyi post-hoc test and its critical difference,
* pairwise win/draw/loss counts.

All critical values are tabulated so that no statistics library is needed.
"""

from __future__ import annotations

# pyright: reportExplicitAny=none, reportAny=none, reportUnknownMemberType=none, reportUnknownArgumentType=none, reportUnknownVariableType=none, reportUnknownParameterType=none, reportUnusedCallResult=none, reportImplicitStringConcatenation=none, reportMissingTypeArgument=error

import math
import sqlite3
from typing import Optional

# Critical values of the Nemenyi test (q_alpha / sqrt(2)), Demšar Table 5.
NEMENYI_Q = {
    0.05: {2: 1.960, 3: 2.343, 4: 2.569, 5: 2.728, 6: 2.850,
           7: 2.949, 8: 3.031, 9: 3.102, 10: 3.164},
    0.10: {2: 1.645, 3: 2.052, 4: 2.291, 5: 2.459, 6: 2.589,
           7: 2.693, 8: 2.780, 9: 2.855, 10: 2.920},
}

# Upper-tail critical values of chi-square, indexed by degrees of freedom.
CHI2_CRITICAL = {
    0.05: {1: 3.841, 2: 5.991, 3: 7.815, 4: 9.488, 5: 11.070,
           6: 12.592, 7: 14.067, 8: 15.507, 9: 16.919, 10: 18.307},
    0.10: {1: 2.706, 2: 4.605, 3: 6.251, 4: 7.779, 5: 9.236,
           6: 10.645, 7: 12.017, 8: 13.362, 9: 14.684, 10: 15.987},
}

# Metrics for which a *larger* value is better. Everything else (loss, error,
# RMSE…) is "smaller is better". Ranking must respect this, otherwise
# higher-is-better metrics such as AUC / MCC get compared backwards and the
# apparent differences shrink or invert.
HIGHER_IS_BETTER = {
    "auc", "mcc", "accuracy", "f1", "f1_score", "precision", "recall",
    "lift", "roc", "auc_macro", "auc_weighted",
}


def _rank(values: list[float], higher_is_better: bool = False) -> list[float]:
    """Rank with ties sharing the average rank. 1 = best.

    For "smaller is better" metrics (loss, error, RMSE…) the smallest value gets
    rank 1; for "higher is better" metrics (AUC, MCC…) the largest value does.
    """
    if higher_is_better:
        values = [-v for v in values]
    order = sorted(range(len(values)), key=lambda i: values[i])
    ranks = [0.0] * len(values)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and values[order[j + 1]] == values[order[i]]:
            j += 1
        # Ranks i+1 .. j+1 are tied, so they share their mean.
        shared = (i + j + 2) / 2.0
        for k in range(i, j + 1):
            ranks[order[k]] = shared
        i = j + 1
    return ranks


def _pooled_metric(
    conn: sqlite3.Connection, run_ids: list[int], learner: str, metric: str
) -> Optional[float]:
    """Prefer the pooled value (fold = -1); fall back to the mean over folds."""
    row = conn.execute(
        """
        SELECT value FROM metrics
        WHERE run_id = ? AND learner = ? AND metric = ? AND fold = -1
        LIMIT 1
        """,
        (run_ids[0], learner, metric),
    ).fetchone()
    if row is not None:
        return float(row["value"])

    row = conn.execute(
        """
        SELECT AVG(value) AS v FROM metrics
        WHERE run_id = ? AND learner = ? AND metric = ? AND fold >= 0
        """,
        (run_ids[0], learner, metric),
    ).fetchone()
    return float(row["v"]) if row and row["v"] is not None else None


def build_matrix(
    conn: sqlite3.Connection,
    batch_id: Optional[int] = None,
    metric: str = "0-1_loss",
    run_ids: Optional[list[int]] = None,
) -> tuple[list[str], list[str], list[list[Optional[float]]]]:
    """Build the data-set x algorithm matrix used by every view below.

    Returns ``(learners, datasets, matrix)`` where ``matrix[i][j]`` is the score
    of learner *j* on data set *i*, or ``None`` when that combination is absent.
    """
    if run_ids is None:
        if batch_id is not None:
            rows = conn.execute(
                "SELECT id FROM runs WHERE batch_id = ? ORDER BY id", (batch_id,)
            ).fetchall()
        else:
            rows = conn.execute("SELECT id FROM runs ORDER BY id").fetchall()
        run_ids = [int(r["id"]) for r in rows]

    # Data set -> learner -> (run ids, best score). Several runs may cover the
    # same data set (repeated experiments); they are averaged.
    per_dataset: dict[str, dict[str, list[float]]] = {}
    for rid in run_ids:
        run = conn.execute(
            "SELECT dataset FROM runs WHERE id = ?", (rid,)
        ).fetchone()
        if not run:
            continue
        dataset = run["dataset"]
        learners = conn.execute(
            "SELECT DISTINCT learner FROM metrics WHERE run_id = ? AND learner != ''",
            (rid,),
        ).fetchall()
        bucket = per_dataset.setdefault(dataset, {})
        for lr in learners:
            name = lr["learner"]
            val = _pooled_metric(conn, [rid], name, metric)
            if val is not None:
                bucket.setdefault(name, []).append(val)

    learners = sorted({l for d in per_dataset.values() for l in d})
    # Only data sets where every learner has a score support a fair comparison.
    datasets = sorted(d for d, m in per_dataset.items() if all(l in m for l in learners))

    matrix = [
        [sum(per_dataset[d][l]) / len(per_dataset[d][l]) for l in learners]
        for d in datasets
    ]
    return learners, datasets, matrix


def average_ranks(
    matrix: list[list[Optional[float]]], higher_is_better: bool = False
) -> list[float]:
    """Mean rank per column (algorithm), averaging over rows (data sets)."""
    if not matrix:
        return []
    k = len(matrix[0])
    totals = [0.0] * k
    for row in matrix:
        ranks = _rank(
            [v if v is not None else float("inf") for v in row], higher_is_better
        )
        for j in range(k):
            totals[j] += ranks[j]
    n = len(matrix)
    return [t / n for t in totals]


def friedman(matrix: list[list[Optional[float]]], ranks: list[float]) -> dict:
    """Friedman test statistic and whether it clears the critical value."""
    n = len(matrix)
    k = len(ranks)
    if n < 2 or k < 2:
        return {"statistic": None, "significant": False, "n_datasets": n, "k": k}

    sum_sq = sum(r * r for r in ranks)
    chi2 = (12.0 * n / (k * (k + 1))) * (sum_sq - (k * (k + 1) ** 2) / 4.0)

    df = k - 1
    crit = CHI2_CRITICAL[0.05].get(df)
    significant = crit is not None and chi2 > crit
    return {
        "statistic": chi2,
        "df": df,
        "critical_value": crit,
        "significant": significant,
        "n_datasets": n,
        "k": k,
    }


def critical_difference(k: int, n: int, alpha: float = 0.05) -> Optional[float]:
    """Nemenyi critical difference: rank gaps above this are significant."""
    if n < 2 or k < 2:
        return None
    q = NEMENYI_Q.get(alpha, NEMENYI_Q[0.05]).get(k)
    if q is None:
        return None
    return q * math.sqrt(k * (k + 1) / (6.0 * n))


def cd_groups(ranks: list[float], cd: Optional[float]) -> list[list[int]]:
    """Indices of algorithms joined by a bar (no significant difference).

    Demšar draws a bar between two *adjacent* algorithms (in rank order) when
    their rank gap is at most the critical difference. We therefore merge every
    maximal run of consecutive algorithms whose neighbouring gaps are all <= cd.
    (A previous implementation broke groups on the span from the leftmost member,
    which wrongly split runs that should have stayed connected.)
    """
    if cd is None or not ranks:
        return []
    order = sorted(range(len(ranks)), key=lambda i: ranks[i])
    groups: list[list[int]] = []
    start = 0
    for i in range(1, len(order)):
        if ranks[order[i]] - ranks[order[i - 1]] > cd:
            groups.append(order[start:i])
            start = i
    groups.append(order[start:])
    return [g for g in groups if len(g) > 1]


def win_draw_loss(matrix: list[list[Optional[float]]]) -> dict:
    """Pairwise win/draw/loss counts across data sets."""
    if not matrix:
        return {"pairs": []}
    k = len(matrix[0])
    pairs = []
    for a in range(k):
        for b in range(a + 1, k):
            win = draw = loss = 0
            for row in matrix:
                va, vb = row[a], row[b]
                if va is None or vb is None:
                    continue
                if va < vb:
                    win += 1
                elif va > vb:
                    loss += 1
                else:
                    draw += 1
            pairs.append({"a": a, "b": b, "win": win, "draw": draw, "loss": loss})
    return {"pairs": pairs}


def compare_batch(
    conn: sqlite3.Connection,
    batch_id: Optional[int] = None,
    metric: str = "0-1_loss",
    run_ids: Optional[list[int]] = None,
    alpha: float = 0.05,
) -> dict:
    """Everything the front-end needs to draw a batch comparison.

    The matrix is always returned (even when incomplete) so the UI can still
    visualise whatever results exist; `warning` explains gaps (missing learners,
    failed tasks, an unsupported metric) instead of showing a blank page.
    """
    learners, datasets, matrix = build_matrix(conn, batch_id, metric, run_ids)
    higher_is_better = metric in HIGHER_IS_BETTER
    ranks = average_ranks(matrix, higher_is_better) if matrix else []
    fr = friedman(matrix, ranks)
    cd = critical_difference(len(learners), len(datasets), alpha) if matrix else None

    # Pairwise Nemenyi: a pair of algorithms is significantly different when the
    # gap between their average ranks exceeds the critical difference. This is
    # exactly what the CD diagram encodes and what "两个值超过 CD 即显著" means.
    pairwise_significant = False
    max_gap = None
    pairwise: list[dict] = []
    if ranks and len(ranks) >= 2:
        max_gap = max(ranks) - min(ranks)
        for a in range(len(ranks)):
            for b in range(a + 1, len(ranks)):
                gap = abs(ranks[b] - ranks[a])
                sig = cd is not None and gap > cd
                if sig:
                    pairwise_significant = True
                pairwise.append({
                    "a": a, "b": b,
                    "learner_a": learners[a], "learner_b": learners[b],
                    "rank_gap": gap, "significant": sig,
                })

    filled = sum(1 for row in matrix for v in row if v is not None)
    total_cells = len(datasets) * len(learners) if (learners and datasets) else 0

    warning = None
    if not learners or not datasets:
        warning = "本批次还没有任何可用的运行结果（可能任务全部失败或未开始）。"
    elif total_cells and filled < total_cells:
        missing = total_cells - filled
        warning = (
            f"仅 {filled}/{total_cells} 个「数据集×算法」组合有结果"
            f"（缺 {missing} 个，可能因任务失败或该指标在某些数据集上缺失）；"
            f"跨数据集比较需要每个算法在每个数据集上都有结果。"
        )

    return {
        "metric": metric,
        "alpha": alpha,
        "learners": learners,
        "datasets": datasets,
        "matrix": matrix,
        "ranks": [
            {"learner": learners[i], "rank": ranks[i]} for i in range(len(learners))
        ] if ranks else [],
        "friedman": fr,
        "critical_difference": cd,
        "cd_groups": cd_groups(ranks, cd) if ranks else [],
        "win_draw_loss": win_draw_loss(matrix) if matrix else {"pairs": []},
        "filled": filled,
        "total_cells": total_cells,
        "warning": warning,
        "pairwise_significant": pairwise_significant,
        "max_rank_gap": max_gap,
        "pairwise": pairwise,
    }
