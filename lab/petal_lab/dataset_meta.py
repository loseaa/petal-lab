"""Read richer data-set facts straight from the on-disk ``.pmeta`` / ``.pdata`` files.

The web UI wants more than a file name and size: how many attributes, how many
of those are class attributes, how many distinct class labels, how many instances
(rows), and a first-pass class distribution. Those facts live only in the files,
so this module parses them without invoking the (heavy) ``petal`` binary.

``.pmeta`` layout varies a little between data sets, so the parser is deliberately
lenient. Observed shapes (column order matches the ``.pdata`` file)::

    :::comment / :comment lines (creator / source)        # any leading ':'
    'attr':numeric                  # numeric predictor
    'attr':v1,v2,v3                 # categorical predictor
    class:v1,v2                     # inline class attribute
    ::class:'Name':v1,v2            # class declared separately (1 or 2 leading colons)
"""

from __future__ import annotations

import os
from pathlib import Path

# In-memory caches so the analysis page and repeated requests stay fast even for
# large data files. Keyed by path + mtime + size so they invalidate on change.
_RECORD_CACHE: dict = {}
_DIST_CACHE: dict = {}

_NUMERIC = ("numeric", "real", "integer", "continuous", "float", "int")


def _add_class(attributes: list[dict], name: str, rest: str) -> None:
    rest = rest.strip()
    if rest.lower() in _NUMERIC:
        attributes.append({"name": name, "role": "class", "type": "numeric", "labels": []})
    else:
        labels = [v.strip() for v in rest.split(",") if v.strip()]
        attributes.append({"name": name, "role": "class", "type": "categorical", "labels": labels})


def _add_predictor(attributes: list[dict], name: str, rest: str) -> None:
    rest = rest.strip()
    if rest.lower() in _NUMERIC:
        attributes.append({"name": name, "role": "predictor", "type": "numeric", "labels": []})
    else:
        labels = [v.strip() for v in rest.split(",") if v.strip()]
        attributes.append({"name": name, "role": "predictor", "type": "categorical", "labels": labels})


def parse_pmeta(meta_path: str | os.PathLike) -> dict:
    """Extract the attribute layout and class declaration from a ``.pmeta`` file."""
    attributes: list[dict] = []
    class_attr_name: str | None = None

    with open(meta_path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            s = raw.strip()
            if not s:
                continue

            # Class declaration: "::class:Name:v1,v2" or ":class:Name:v1,v2".
            if s.startswith("::class:"):
                body = s[len("::class:"):]
            elif s.startswith(":class:"):
                body = s[len(":class:"):]
            else:
                body = None
            if body is not None:
                if ":" in body:
                    name, vals = body.split(":", 1)
                    _add_class(attributes, name.strip().strip("'\""), vals)
                    class_attr_name = name.strip().strip("'\"")
                continue

            # Comment / meta lines start with a colon.
            if s.startswith(":"):
                continue
            if ":" not in s:
                continue

            name, rest = s.split(":", 1)
            name = name.strip().strip("'\"")
            rest = rest.strip()
            if name.lower() == "class":
                _add_class(attributes, name, rest)
                class_attr_name = name
                continue
            _add_predictor(attributes, name, rest)

    class_index = None
    for idx, a in enumerate(attributes):
        if a["role"] == "class":
            class_index = idx
            break

    if class_index is not None:
        class_labels = attributes[class_index]["labels"]
        class_type = attributes[class_index]["type"]
    else:
        class_labels = []
        class_type = None

    return {
        "attributes": attributes,
        "n_attributes": len(attributes),
        "n_predictors": sum(1 for a in attributes if a["role"] == "predictor"),
        "n_class_attrs": sum(1 for a in attributes if a["role"] == "class"),
        "class_attr_name": class_attr_name,
        "class_labels": class_labels,
        "n_classes": len(class_labels),
        "class_index": class_index,
        "class_type": class_type,
    }


def count_records(data_path: str | os.PathLike) -> int:
    """Number of data rows (non-empty lines) in a ``.pdata`` file."""
    p = Path(data_path)
    try:
        st = p.stat()
        key = (str(p), st.st_mtime, st.st_size)
    except OSError:
        return 0
    if key in _RECORD_CACHE:
        return _RECORD_CACHE[key]
    n = 0
    try:
        with open(p, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if line.strip():
                    n += 1
    except OSError:
        n = 0
    _RECORD_CACHE[key] = n
    return n


def _class_distribution(data_path: str | os.PathLike, class_index: int | None, class_type: str | None) -> dict | None:
    if class_index is None or class_type != "categorical":
        return None
    p = Path(data_path)
    try:
        st = p.stat()
        key = (str(p), st.st_mtime, st.st_size, "dist")
    except OSError:
        return None
    if key in _DIST_CACHE:
        return _DIST_CACHE[key]
    counts: dict[str, int] = {}
    try:
        with open(p, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.strip()
                if not s:
                    continue
                parts = s.split(",")
                if class_index < len(parts):
                    val = parts[class_index].strip().strip("'\"")
                    counts[val] = counts.get(val, 0) + 1
    except OSError:
        counts = {}
    _DIST_CACHE[key] = counts
    return counts


def analyze_dataset(meta_path: str, data_path: str, compute_distribution: bool = False) -> dict:
    """Combine metadata + row count (+ optional class distribution)."""
    meta = parse_pmeta(meta_path)
    result = {
        "n_attributes": meta["n_attributes"],
        "n_predictors": meta["n_predictors"],
        "n_class_attrs": meta["n_class_attrs"],
        "n_classes": meta["n_classes"],
        "class_attr_name": meta["class_attr_name"],
        "class_labels": meta["class_labels"],
        "class_index": meta["class_index"],
        "class_type": meta["class_type"],
        "n_records": count_records(data_path),
        "attributes": meta["attributes"],
    }
    if compute_distribution:
        result["class_distribution"] = _class_distribution(
            data_path, meta["class_index"], meta["class_type"]
        )
    return result
