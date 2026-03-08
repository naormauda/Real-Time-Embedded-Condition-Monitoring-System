#!/usr/bin/env python3
"""
Export a trained scikit-learn IsolationForest model to embedded C source files.

Outputs:
  - Core/Inc/generated_iforest_model.h
  - Core/Src/generated_iforest_model.c

The generated C backend performs:
  1) Standardization using exported scaler (mean/scale)
  2) Isolation Forest tree traversal
  3) Anomaly score computation in [0, 1]

Optional calibration:
  - If a labeled CSV is provided, this script computes a threshold that maximizes F1.
"""

from __future__ import annotations

import argparse
import json
import math
import pickle
from dataclasses import dataclass
from pathlib import Path
from typing import List, Tuple

import numpy as np
import pandas as pd


FEATURE_NAMES = [
    "x_mean", "x_var", "x_rms", "x_peak",
    "y_mean", "y_var", "y_rms", "y_peak",
    "z_mean", "z_var", "z_rms", "z_peak",
]


@dataclass
class TreeExport:
    left: np.ndarray
    right: np.ndarray
    feature: np.ndarray
    threshold: np.ndarray
    n_node_samples: np.ndarray


def avg_path_length(n: float) -> float:
    # Same approximation family used by isolation forest literature.
    if n <= 1.0:
        return 0.0
    if n <= 2.0:
        return 1.0
    gamma = 0.5772156649015329
    return 2.0 * (math.log(n - 1.0) + gamma) - (2.0 * (n - 1.0) / n)


def c_array_int(name: str, values: np.ndarray, ctype: str = "int16_t") -> str:
    body = ", ".join(str(int(v)) for v in values.tolist())
    return f"static const {ctype} {name}[] = {{{body}}};\n"


def c_array_float(name: str, values: np.ndarray) -> str:
    def fmt(v: float) -> str:
        s = f"{float(v):.9g}"
        if ("." not in s) and ("e" not in s) and ("E" not in s):
            s += ".0"
        return s + "f"

    body = ", ".join(fmt(v) for v in values.tolist())
    return f"static const float {name}[] = {{{body}}};\n"


def find_best_threshold(scores: np.ndarray, labels: np.ndarray) -> Tuple[float, dict]:
    # labels: 0=normal, 1=anomaly
    best_thr = 0.5
    best_f1 = -1.0
    best_metrics = {}

    for thr in np.linspace(0.05, 0.95, 181):
        pred = (scores > thr).astype(np.int32)
        tp = int(np.sum((pred == 1) & (labels == 1)))
        tn = int(np.sum((pred == 0) & (labels == 0)))
        fp = int(np.sum((pred == 1) & (labels == 0)))
        fn = int(np.sum((pred == 0) & (labels == 1)))

        precision = tp / (tp + fp) if (tp + fp) else 0.0
        recall = tp / (tp + fn) if (tp + fn) else 0.0
        if precision + recall == 0:
            f1 = 0.0
        else:
            f1 = 2.0 * precision * recall / (precision + recall)

        if f1 > best_f1:
            best_f1 = f1
            best_thr = float(thr)
            best_metrics = {
                "precision": precision,
                "recall": recall,
                "f1": f1,
                "tp": tp,
                "tn": tn,
                "fp": fp,
                "fn": fn,
            }

    return best_thr, best_metrics


def model_score_samples(
    trees: List[TreeExport],
    x_scaled: np.ndarray,
    c_n: float,
) -> np.ndarray:
    out = np.zeros(x_scaled.shape[0], dtype=np.float64)

    for i in range(x_scaled.shape[0]):
        x = x_scaled[i]
        path_sum = 0.0
        for t in trees:
            idx = 0
            depth = 0
            while t.feature[idx] >= 0:
                feat = t.feature[idx]
                thr = t.threshold[idx]
                idx = t.left[idx] if x[feat] <= thr else t.right[idx]
                depth += 1
            leaf_n = float(t.n_node_samples[idx])
            path_sum += float(depth) + avg_path_length(leaf_n)

        avg_path = path_sum / len(trees)
        s = math.pow(2.0, -(avg_path / c_n))
        out[i] = max(0.0, min(1.0, s))

    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Export IsolationForest to embedded C")
    parser.add_argument("--model", required=True, help="Path to isolation_forest.pkl")
    parser.add_argument("--scaler", required=True, help="Path to isolation_forest_scaler.pkl")
    parser.add_argument(
        "--calibration-data",
        default="",
        help="Optional labeled CSV for threshold calibration",
    )
    parser.add_argument(
        "--project-root",
        default="..",
        help="Project root path (default: .. from tools)",
    )
    args = parser.parse_args()

    model_path = Path(args.model)
    scaler_path = Path(args.scaler)
    project_root = Path(args.project_root).resolve()

    with model_path.open("rb") as f:
        model = pickle.load(f)
    with scaler_path.open("rb") as f:
        scaler = pickle.load(f)

    if not hasattr(model, "estimators_"):
        raise RuntimeError("Provided model is not an IsolationForest estimator")

    trees: List[TreeExport] = []
    for est in model.estimators_:
        t = est.tree_
        trees.append(
            TreeExport(
                left=t.children_left.astype(np.int16),
                right=t.children_right.astype(np.int16),
                feature=t.feature.astype(np.int16),
                threshold=t.threshold.astype(np.float32),
                n_node_samples=t.n_node_samples.astype(np.int16),
            )
        )

    num_trees = len(trees)
    max_samples = int(model.max_samples_)
    c_n = avg_path_length(float(max_samples))

    threshold = 0.50
    calibration_summary = {"used": False}

    if args.calibration_data:
        df = pd.read_csv(args.calibration_data)
        x = df[FEATURE_NAMES].values.astype(np.float64)
        y = (df["label"].values != "NORMAL").astype(np.int32)

        x_scaled = scaler.transform(x)
        scores = model_score_samples(trees, x_scaled, c_n)

        threshold, metrics = find_best_threshold(scores, y)
        calibration_summary = {
            "used": True,
            "threshold": threshold,
            "metrics": metrics,
            "samples": int(len(df)),
        }

    inc_dir = project_root / "Core" / "Inc"
    src_dir = project_root / "Core" / "Src"
    inc_dir.mkdir(parents=True, exist_ok=True)
    src_dir.mkdir(parents=True, exist_ok=True)

    h_path = inc_dir / "generated_iforest_model.h"
    c_path = src_dir / "generated_iforest_model.c"
    meta_path = project_root / "tools" / "models" / "generated_iforest_metadata.json"

    header = """#ifndef GENERATED_IFOREST_MODEL_H
#define GENERATED_IFOREST_MODEL_H

#include <stdbool.h>

#define IF_MODEL_NUM_FEATURES 12

bool iforest_generated_is_available(void);
float iforest_generated_default_threshold(void);
const char *iforest_generated_name(void);
float iforest_generated_predict(const float features[IF_MODEL_NUM_FEATURES]);

#endif /* GENERATED_IFOREST_MODEL_H */
"""

    lines: List[str] = []
    lines.append('#include "generated_iforest_model.h"\n')
    lines.append("#include <math.h>\n")
    lines.append("#include <stdint.h>\n\n")
    lines.append(f"#define IF_NUM_TREES {num_trees}\n")
    lines.append(f"#define IF_MAX_SAMPLES {max_samples}\n")
    lines.append(f"#define IF_C_N {c_n:.9g}f\n")
    lines.append(f"#define IF_DEFAULT_THRESHOLD {threshold:.9g}f\n\n")

    lines.append(c_array_float("g_scaler_mean", np.asarray(scaler.mean_, dtype=np.float32)))
    lines.append(c_array_float("g_scaler_scale", np.asarray(scaler.scale_, dtype=np.float32)))
    lines.append("\n")

    lines.append("typedef struct {\n")
    lines.append("    const int16_t *left;\n")
    lines.append("    const int16_t *right;\n")
    lines.append("    const int16_t *feature;\n")
    lines.append("    const float *threshold;\n")
    lines.append("    const int16_t *n_node_samples;\n")
    lines.append("    int16_t node_count;\n")
    lines.append("} if_tree_t;\n\n")

    tree_refs = []
    for i, t in enumerate(trees):
        lines.append(c_array_int(f"t{i}_left", t.left))
        lines.append(c_array_int(f"t{i}_right", t.right))
        lines.append(c_array_int(f"t{i}_feature", t.feature))
        lines.append(c_array_float(f"t{i}_threshold", t.threshold))
        lines.append(c_array_int(f"t{i}_n_samples", t.n_node_samples))
        lines.append("\n")
        tree_refs.append(
            f"    {{t{i}_left, t{i}_right, t{i}_feature, t{i}_threshold, t{i}_n_samples, {len(t.left)}}}"
        )

    lines.append("static const if_tree_t g_trees[IF_NUM_TREES] = {\n")
    lines.append(",\n".join(tree_refs))
    lines.append("\n};\n\n")

    lines.append("static float if_avg_path_length(float n) {\n")
    lines.append("    if (n <= 1.0f) return 0.0f;\n")
    lines.append("    if (n <= 2.0f) return 1.0f;\n")
    lines.append("    return 2.0f * (logf(n - 1.0f) + 0.5772156649f) - (2.0f * (n - 1.0f) / n);\n")
    lines.append("}\n\n")

    lines.append("bool iforest_generated_is_available(void) { return true; }\n")
    lines.append("float iforest_generated_default_threshold(void) { return IF_DEFAULT_THRESHOLD; }\n")
    lines.append("const char *iforest_generated_name(void) { return \"iforest_generated_v1\"; }\n\n")

    lines.append("float iforest_generated_predict(const float features[IF_MODEL_NUM_FEATURES]) {\n")
    lines.append("    float x[IF_MODEL_NUM_FEATURES];\n")
    lines.append("    float path_sum = 0.0f;\n")
    lines.append("\n")
    lines.append("    for (int i = 0; i < IF_MODEL_NUM_FEATURES; i++) {\n")
    lines.append("        float s = g_scaler_scale[i];\n")
    lines.append("        if (s == 0.0f) s = 1.0f;\n")
    lines.append("        x[i] = (features[i] - g_scaler_mean[i]) / s;\n")
    lines.append("    }\n\n")

    lines.append("    for (int t = 0; t < IF_NUM_TREES; t++) {\n")
    lines.append("        const if_tree_t *tr = &g_trees[t];\n")
    lines.append("        int idx = 0;\n")
    lines.append("        int depth = 0;\n")
    lines.append("\n")
    lines.append("        while (tr->feature[idx] >= 0) {\n")
    lines.append("            int f = tr->feature[idx];\n")
    lines.append("            float thr = tr->threshold[idx];\n")
    lines.append("            idx = (x[f] <= thr) ? tr->left[idx] : tr->right[idx];\n")
    lines.append("            depth++;\n")
    lines.append("            if (idx < 0 || idx >= tr->node_count) {\n")
    lines.append("                break;\n")
    lines.append("            }\n")
    lines.append("        }\n")
    lines.append("\n")
    lines.append("        if (idx < 0) idx = 0;\n")
    lines.append("        if (idx >= tr->node_count) idx = tr->node_count - 1;\n")
    lines.append("\n")
    lines.append("        float leaf_n = (float)tr->n_node_samples[idx];\n")
    lines.append("        path_sum += (float)depth + if_avg_path_length(leaf_n);\n")
    lines.append("    }\n\n")

    lines.append("    float avg_path = path_sum / (float)IF_NUM_TREES;\n")
    lines.append("    float score = powf(2.0f, -(avg_path / IF_C_N));\n")
    lines.append("    if (score < 0.0f) score = 0.0f;\n")
    lines.append("    if (score > 1.0f) score = 1.0f;\n")
    lines.append("    return score;\n")
    lines.append("}\n")

    h_path.write_text(header, encoding="ascii")
    c_path.write_text("".join(lines), encoding="ascii")

    metadata = {
        "model": str(model_path),
        "scaler": str(scaler_path),
        "num_trees": num_trees,
        "max_samples": max_samples,
        "c_n": c_n,
        "default_threshold": threshold,
        "calibration": calibration_summary,
        "generated_files": [str(h_path), str(c_path)],
    }
    meta_path.write_text(json.dumps(metadata, indent=2), encoding="ascii")

    print(f"Generated: {h_path}")
    print(f"Generated: {c_path}")
    print(f"Generated: {meta_path}")
    print(f"Default threshold: {threshold:.4f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
