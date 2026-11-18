#run code
#python /Users/midoriwatabe/Desktop/HKSTP/MQ137_data/voltage_to_ppm.py
#
# Adds a ppm column to a logged MQ137 CSV (must have time_s and voltage_V
# columns), using the curve fitted from real calibration data:
#   Rs/R0 = 1.470 * ppm^-0.2356   (5-point fit, 50/100/100/200/300 ppm, R^2=0.83)
#
# Run this file directly (e.g. press Run in your editor) — it will ask for
# the input CSV path, an optional output path, and an optional known
# baseline voltage.
#
# By default the clean-air baseline (R0) is auto-computed as the median
# voltage over the first 30 seconds of the file, same as how the fit itself
# was calibrated. If a run's recorded baseline is unreliable (e.g. logging
# started after gas injection), enter the real baseline voltage manually
# when prompted.a

import csv
import os

VC = 5.0
CURVE_A = 1.470
CURVE_B = -0.2356


def rs_over_rl(voltage):
    return VC / voltage - 1.0


def voltage_to_ppm(voltage, r0):
    ratio = rs_over_rl(voltage) / r0
    return (ratio / CURVE_A) ** (1.0 / CURVE_B)


def auto_baseline(rows, window_s=30.0):
    t0 = float(rows[0]["time_s"])
    baseline_voltages = sorted(
        float(r["voltage_V"]) for r in rows
        if float(r["time_s"]) - t0 <= window_s
    )
    n = len(baseline_voltages)
    return baseline_voltages[n // 2]  # median


def ask_path(prompt):
    raw = input(prompt).strip()
    # strip surrounding quotes, e.g. from a drag-and-dropped file path
    if len(raw) >= 2 and raw[0] == raw[-1] and raw[0] in ("'", '"'):
        raw = raw[1:-1]
    return raw


def main():
    input_csv = ask_path("Path to input CSV: ")
    while not os.path.isfile(input_csv):
        input_csv = ask_path(f"'{input_csv}' not found. Path to input CSV: ")

    output_csv = ask_path(
        "Path to output CSV (leave blank for default, same folder + '_ppm'): "
    )
    output_path = output_csv or input_csv.rsplit(".csv", 1)[0] + "_ppm.csv"

    baseline_input = input(
        "Known clean-air baseline voltage in V (leave blank to auto-detect "
        "from the first 30s of the file): "
    ).strip()
    baseline = float(baseline_input) if baseline_input else None

    with open(input_csv, newline="") as f:
        reader = csv.DictReader(f)
        rows = [r for r in reader if r.get("voltage_V")]

    if not rows:
        raise SystemExit(f"No rows with voltage_V found in {input_csv}")

    r0_voltage = baseline if baseline is not None else auto_baseline(rows)
    r0 = rs_over_rl(r0_voltage)
    print(f"Baseline voltage: {r0_voltage:.4f} V  (R0 = {r0:.4f})")

    fieldnames = list(rows[0].keys())
    if "ppm" not in fieldnames:
        fieldnames.append("ppm")

    with open(output_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            voltage = float(row["voltage_V"])
            row["ppm"] = f"{voltage_to_ppm(voltage, r0):.2f}"
            writer.writerow(row)

    print(f"Wrote {len(rows)} rows to {output_path}")


if __name__ == "__main__":
    main()
