"""Turn a natural-language sentence into a batch experiment plan.

Two resolution paths, both producing the *same* plain-dict shape:

* LLM mode (optional): when ``PETAL_LLM_PROVIDER`` is set together with a usable
  API key, ask a model to structure the request.
* Rule mode (default, zero-config): keyword matching over the available data
  sets and a known learner vocabulary. This is also the fallback whenever the
  LLM call fails, so the feature always works on a laptop with no network.

Crucially, the planner only ever emits *parameters* (data set paths, learner
names, metric, folds). Downstream code feeds those through
``builders.build_command`` exactly like the manual form, so the LLM can never
manufacture a shell command — the existing security boundary still applies.
"""

from __future__ import annotations

# pyright: reportExplicitAny=none, reportAny=none, reportUnknownMemberType=none, reportUnknownArgumentType=none, reportUnknownVariableType=none, reportUnknownParameterType=none, reportUnusedCallResult=none, reportImplicitStringConcatenation=none, reportMissingTypeArgument=error

import json
import os
import re
import urllib.request

# Known petal learner names. Rule mode matches these case-insensitively; the LLM
# may return others, and builders._check_name still gates anything exotic.
KNOWN_LEARNERS = [
    "nb", "nbayes", "aode", "tan", "j48", "cart", "id3", "c45",
    "knn", "ibk", "svm", "smo", "mlp", "lr", "logistic", "ripper",
    "rbf", "ht", "bayesnet", "lwl", "bag", "rf", "dt",
]

METRIC_MAP = [
    ("auc", "auc"),
    ("roc", "auc"),
    ("accuracy", "accuracy"),
    ("准确", "accuracy"),
    ("f1", "f1"),
    ("0-1", "0-1_loss"),
    ("loss", "0-1_loss"),
    ("损失", "0-1_loss"),
    ("rmse", "rmse"),
    ("error", "0-1_loss"),
    ("mae", "mae"),
]

MODE_MAP = [
    ("学习曲线", "learningCurves"),
    ("learning curve", "learningCurves"),
    ("训练测试", "trainTest"),
    ("train test", "trainTest"),
    ("train/test", "trainTest"),
    ("交叉", "xval"),
    ("cross", "xval"),
    ("xval", "xval"),
]

DISCRETISE_HINTS = ["离散", "discret", "bin", "分箱", "discretiz"]


# --------------------------------------------------------------------------- #
# Rule-based planner (always available)
# --------------------------------------------------------------------------- #

def _match_datasets(text: str, datasets: list[dict]) -> list[dict]:
    low = text.lower()
    # explicit "all / every / 全部 / 所有数据集"
    if re.search(r"\b(all|every|each|每个|全部|所有)\b", low) and "数据集" in text:
        return list(datasets)
    picked: list[dict] = []
    for ds in datasets:
        name = (ds.get("name") or "").lower()
        if not name:
            continue
        tokens = [t for t in re.split(r"[_\-.\s]+", name) if len(t) > 2]
        if name in low or any(t in low for t in tokens):
            picked.append(ds)
    # fuzzy: a 4+ char name prefix appears in the sentence
    if not picked:
        for ds in datasets:
            name = ds.get("name", "")
            if len(name) >= 4 and name[:4] in low:
                picked.append(ds)
    return picked


def _match_learners(text: str) -> list[str]:
    low = text.lower()
    out: list[str] = []
    for lr in KNOWN_LEARNERS:
        if re.search(rf"\b{re.escape(lr)}\b", low):
            out.append(lr)
    for token in re.split(r"[,，、\s]+|和|与|以及", low):
        token = token.strip()
        if token in KNOWN_LEARNERS and token not in out:
            out.append(token)
    return out


def _metric(text: str) -> str:
    low = text.lower()
    for key, val in METRIC_MAP:
        if key in low:
            return val
    return "0-1_loss"


def _mode(text: str) -> str:
    low = text.lower()
    for key, val in MODE_MAP:
        if key in low:
            return val
    return "xval"


def _folds(text: str) -> int | None:
    m = re.search(r"(\d+)\s*(?:折|fold|重|cross)", text.lower())
    if m:
        return max(2, min(20, int(m.group(1))))
    return None


def _discretise(text: str) -> str:
    if any(h in text.lower() for h in DISCRETISE_HINTS):
        return "m"  # equal-width discretiser; builders validates the name
    return ""


def _suggest_name(picked: list[dict], learners: list[str]) -> str:
    bits: list[str] = []
    if learners:
        bits.append("/".join(learners[:3]))
    if picked:
        bits.append(f"{len(picked)}ds")
    return (("-".join(bits) if bits else "nl-batch"))[:40]


def rule_plan(text: str, datasets: list[dict]) -> dict:
    learners = _match_learners(text)
    picked = _match_datasets(text, datasets)
    return {
        "name": _suggest_name(picked, learners),
        "description": text.strip(),
        "datasets": picked,
        "learners": learners,
        "metric": _metric(text),
        "mode": _mode(text),
        "folds": _folds(text),
        "discretiser": _discretise(text),
        "source": "rule",
    }


# --------------------------------------------------------------------------- #
# Optional LLM planner
# --------------------------------------------------------------------------- #

def _call_llm(provider: str, key: str, system: str, user: str) -> str:
    model = os.environ.get("PETAL_LLM_MODEL") or (
        "gpt-4o-mini" if provider == "openai" else "claude-3-5-sonnet-20241022"
    )
    if provider == "anthropic":
        url = "https://api.anthropic.com/v1/messages"
        body = json.dumps({
            "model": model, "max_tokens": 900,
            "system": system,
            "messages": [{"role": "user", "content": user}],
        }).encode()
        req = urllib.request.Request(
            url, data=body,
            headers={"x-api-key": key, "anthropic-version": "2023-06-01",
                     "content-type": "application/json"},
        )
    else:
        base = (os.environ.get("PETAL_LLM_BASE_URL") or "https://api.openai.com/v1").rstrip("/")
        url = base + "/chat/completions"
        body = json.dumps({
            "model": model, "temperature": 0,
            "messages": [{"role": "system", "content": system},
                         {"role": "user", "content": user}],
        }).encode()
        req = urllib.request.Request(
            url, data=body,
            headers={"Authorization": f"Bearer {key}", "content-type": "application/json"},
        )
    with urllib.request.urlopen(req, timeout=30) as resp:  # noqa: S310 - https only, key-gated
        data = json.loads(resp.read().decode())
    if provider == "anthropic":
        return "".join(b.get("text", "") for b in data.get("content", []) if isinstance(b, dict))
    return data["choices"][0]["message"]["content"]


def _llm_plan(text: str, datasets: list[dict]) -> dict | None:
    provider = (os.environ.get("PETAL_LLM_PROVIDER") or "none").lower()
    if provider == "none":
        return None
    key = (os.environ.get("PETAL_LLM_API_KEY")
           or os.environ.get("OPENAI_API_KEY")
           or os.environ.get("ANTHROPIC_API_KEY"))
    if not key:
        return None
    names = [d.get("name", "") for d in datasets]
    rubric = (
        "You convert a natural-language request for a machine-learning experiment "
        "into JSON. Reply with ONLY a JSON object and nothing else: "
        '{"name": str, "learners": [str], "metric": str, '
        '"mode": "xval"|"trainTest"|"learningCurves", '
        '"folds": int|null, "discretiser": str|null, "dataset_hints": [str]}. '
        "Learner names must be from petal's vocabulary (nb, aode, tan, j48, knn, svm, "
        "mlp, ...). Only reference data sets whose names appear in this list: "
        + ", ".join(names) + "."
    )
    try:
        raw = _call_llm(provider, key, rubric, text)
        data = json.loads(raw[raw.find("{"): raw.rfind("}") + 1])
    except Exception:  # noqa: BLE001 - any failure falls back to rules
        return None

    hints = data.get("dataset_hints") or []
    matched = _match_datasets(" ".join(hints), datasets) if hints else []
    if not matched and hints:
        matched = _match_datasets(text, datasets)
    return {
        "name": str(data.get("name") or "nl-batch"),
        "description": text.strip(),
        "datasets": matched,
        "learners": [str(x) for x in (data.get("learners") or [])],
        "metric": str(data.get("metric") or "0-1_loss"),
        "mode": str(data.get("mode") or "xval"),
        "folds": data.get("folds"),
        "discretiser": data.get("discretiser") or "",
        "source": f"llm:{provider}",
    }


# --------------------------------------------------------------------------- #
# Public entry point
# --------------------------------------------------------------------------- #

def _preview_command(plan: dict) -> str:
    """Show the real petal invocation the plan maps to (server runs petal directly).

    A batch is just N petal processes (one per data set x learner), so we render a
    representative command plus the total count — no petal-lab wrapper, matching
    what the web UI actually executes.
    """
    learners = plan.get("learners") or []
    datasets = plan.get("datasets") or []
    folds = plan.get("folds") or 10
    discretiser = plan.get("discretiser") or ""
    learner = learners[0] if learners else "<algo>"
    if datasets:
        first = datasets[0]
        meta = first.get("meta", "<dataset>.pmeta")
        data = first.get("data", "<dataset>.pdata")
        base = f"petal {meta} {data}"
    else:
        base = "petal <dataset>.pmeta <dataset>.pdata"
    opts = f" -d{discretiser}" if discretiser else ""
    opts += f" -v2 -x{folds} -l{learner}"
    n = max(1, len(datasets)) * max(1, len(learners))
    return f"{base}{opts}   # 共 {n} 次运行（数据集 × 算法）"


def _warnings(plan: dict) -> list[str]:
    w: list[str] = []
    if not plan.get("learners"):
        w.append("未识别到算法，请明确写出算法名，如 nb、aode、tan。")
    if not plan.get("datasets"):
        w.append("未匹配到数据集，请检查数据集名称或在页面中选择。")
    return w


def plan_batch(text: str, datasets: list[dict]) -> dict:
    """Parse a sentence into a batch plan, with an informational preview."""
    plan = _llm_plan(text, datasets)
    if plan is None:
        plan = rule_plan(text, datasets)
    plan["command"] = _preview_command(plan)
    plan["warnings"] = _warnings(plan)
    return plan
