#!/usr/bin/env python3
"""
Rebuilds the numbers behind the "Odour vector" tab of odor_dashboard.html.

The dashboard contains fixed numbers (the scaler, the PCA loadings and every
plotted point). They were originally generated offline, and the script that
generated them was not saved. This script repeats the same method and prints
the numbers in the format the dashboard expects, so a new compound can be added.

Method (identical to the dashboard's own "Classify a sample" code):
  1. Keep only distinct BME688 sweeps (drop rows that repeat the previous gas values).
  2. Baseline = median of the run's first 15% of sweeps (clean air before exposure).
  3. Exposed window = the run's last 35% of sweeps (the settled response).
  4. For each exposed sweep: log10(gas_i / baseline_i) for the 10 heater steps.
  5. Standardise every step (subtract mean, divide by standard deviation).
  6. PCA on all classes pooled together. PC1 is not plotted (it tracks overall
     response size). PC2 gives x and PC3 gives y.
  7. radius = sqrt(x^2 + y^2); angle = atan2(y, x) in degrees (0-360).

Usage:
    python build_pca_odour_map.py --data-dir "<folder with the *_bmecalibrate.csv files>"
Edit CLASS_FILES below to add, remove or swap a compound. The result is written
to pca_map_constants.js.txt; paste its contents over the matching lines in
odor_dashboard.html (SCALER_MEAN ... PC3_LOADINGS, then SAMPLES and CENTROIDS).

Requires: numpy, pandas, scikit-learn.
"""

import argparse
import json
import os

import numpy as np
import pandas as pd
from sklearn.decomposition import PCA
from sklearn.preprocessing import StandardScaler

# class name shown on the plot -> calibration file (one file per compound)
CLASS_FILES = {
    "Clean air": "clean air_bmecalibrate.csv",
    "Ammonia": "ammonia_bmecalibrate.csv",
    "Organic acids": "acetic acid_bmecalibrate_2.csv",
    "Linalyl acetate": "linalyl acetate_bmecalibrate.csv",
    "Phenolics": "thyme_oil_bmecalibrate.csv",
    "Terpene alcohols": "rosewater_bmecalibrate.csv",
}
BASELINE_FRACTION = 0.15
EXPOSED_FRACTION = 0.35
GAS_COLUMNS = [f"gas_{i}_ohm" for i in range(10)]


def distinct_sweeps(path):
    """Gas-resistance rows with consecutive repeats removed (one row per real sweep)."""
    values = pd.read_csv(path)[GAS_COLUMNS].dropna().values
    keep = [0] + [i for i in range(1, len(values)) if (values[i] != values[i - 1]).any()]
    return values[keep]


def log_ratio_tail(sweeps):
    """log10 of the exposed window divided by the run's own clean-air baseline."""
    n = len(sweeps)
    n_base = max(2, int(n * BASELINE_FRACTION))
    n_tail = max(2, int(n * EXPOSED_FRACTION))
    baseline = np.median(sweeps[:n_base], axis=0)
    return np.log10(sweeps[n - n_tail:] / baseline)


def polar(x, y):
    return float(np.hypot(x, y)), float(np.degrees(np.arctan2(y, x)) % 360)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data-dir", required=True, help="Folder containing the calibration CSV files")
    parser.add_argument("--out", default="pca_map_constants.js.txt", help="Where to write the numbers to paste")
    args = parser.parse_args()

    blocks, labels = [], []
    for name, filename in CLASS_FILES.items():
        tail = log_ratio_tail(distinct_sweeps(os.path.join(args.data_dir, filename)))
        blocks.append(tail)
        labels += [name] * len(tail)
        print(f"{name:18s} {len(tail):3d} exposed sweeps  <- {filename}")

    scaler = StandardScaler().fit(np.vstack(blocks))
    pca = PCA().fit(scaler.transform(np.vstack(blocks)))
    scores = pca.transform(scaler.transform(np.vstack(blocks)))
    labels = np.array(labels)
    share = pca.explained_variance_ratio_
    print(f"Variance explained: PC1 {share[0]:.1%}, PC2 {share[1]:.2%}, PC3 {share[2]:.2%}")

    samples, centroids = [], []
    for name in CLASS_FILES:
        rows = scores[labels == name]
        for i, (x, y) in enumerate(rows[:, 1:3], start=1):
            m, a = polar(x, y)
            samples.append({"id": name.replace(" ", "")[:4].upper() + f"-{i}", "cls": name,
                            "x": round(float(x), 3), "y": round(float(y), 3), "m": round(m, 3), "a": round(a, 2)})
        cx, cy = rows[:, 1].mean(), rows[:, 2].mean()
        m, a = polar(cx, cy)
        centroids.append({"cls": name, "x": round(float(cx), 3), "y": round(float(cy), 3),
                          "m": round(m, 3), "a": round(a, 2), "n": int(len(rows))})

    def js_list(values):
        return "[" + ", ".join(f"{v:.6g}" for v in values) + "]"

    with open(args.out, "w") as f:
        f.write(f"var SCALER_MEAN = {js_list(scaler.mean_)};\n")
        f.write(f"var SCALER_SCALE = {js_list(scaler.scale_)};\n")
        f.write(f"var PC2_LOADINGS = {js_list(pca.components_[1])};\n")
        f.write(f"var PC3_LOADINGS = {js_list(pca.components_[2])};\n\n")
        f.write("var SAMPLES = " + json.dumps(samples, separators=(",", ":")) + ";\n\n")
        f.write("// Add the reserved (placeholder) classes back by hand if still wanted.\n")
        f.write("var CENTROIDS = [\n  " + ",\n  ".join(json.dumps(c, separators=(",", ":")) for c in centroids) + "\n];\n")
    print(f"Wrote {args.out}. Paste it over the matching lines in odor_dashboard.html.")


if __name__ == "__main__":
    main()
