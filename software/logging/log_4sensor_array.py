#!/usr/bin/env python3
"""
Raw CSV logger for UNiT_4sensor_array.ino (BME688 + MQ137 + MQ138 + TGS2620).

The board's own header line is read at startup and used as-is, since it
changes shape depending on whether the BME688 answered on I2C:
  - 4-sensor mode: timestamp_ms,temperature_C,pressure_Pa,humidity_pct,
    gas_0_ohm..gas_9_ohm,mq137_raw_adc,mq137_voltage_V,mq138_raw_adc,
    mq138_voltage_V,tgs2620_mode,tgs2620_raw_adc,tgs2620_voltage_V,tgs2620_Rs_ohm
  - 3-sensor fallback mode (BME688 not found): the temperature/pressure/
    humidity/gas_* columns are omitted entirely.

No unit conversion, calibration, or ML here - just parses whatever columns
the board says it's sending and appends them to a CSV with a computer-side
timestamp prepended.

Each run writes its own file, named "4_sensor_log (<start> ~ <end>).csv".
The end time isn't known until you stop the script (Ctrl+C), so the file is
written under a "... ~ running).csv" name and renamed to include the actual
end time once logging stops.

Usage:
    python log_4sensor_array.py
"""

import csv
import os
import subprocess
import sys
from datetime import datetime

import serial  # type: ignore

PORT = "/dev/cu.usbmodem2101"  # verify after uploading UNiT_4sensor_array.ino
BAUD = 115200
OUTPUT_DIR = "/Users/midoriwatabe/Desktop/HKSTP/4sensor_array_data"

TIME_FORMAT = "%Y-%m-%d_%H-%M-%S"


def main():
    # -d/-i/-s block display, idle, and system sleep respectively (-s only
    # takes effect on AC power) for as long as this script's PID is alive.
    subprocess.Popen(["caffeinate", "-d", "-i", "-s", "-w", str(os.getpid())])

    ser = serial.Serial(PORT, BAUD, timeout=5)
    print(f"Connected to {PORT} @ {BAUD} baud.")
    print("Waiting for the board's CSV header...")

    start_str = datetime.now().strftime(TIME_FORMAT)
    running_path = os.path.join(OUTPUT_DIR, f"4_sensor_log ({start_str} ~ running).csv")

    device_columns = None
    status_line_active = False  # tracks whether the cursor is mid-line on the \r status line

    try:
        with open(running_path, "w", newline="") as f:
            writer = csv.writer(f)

            try:
                while True:
                    raw = ser.readline()
                    if not raw:
                        continue

                    line = raw.decode("utf-8", errors="replace").strip()
                    if not line:
                        continue

                    if line.startswith("DEBUG:"):
                        if status_line_active:
                            print()
                            status_line_active = False
                        print(f"[device] {line}")
                        continue

                    if line.startswith("BME688 error") or line.startswith("BME688 warning") \
                            or line.startswith("Sensor error") or line.startswith("Sensor warning"):
                        if status_line_active:
                            print()
                            status_line_active = False
                        print(f"[device] {line}", file=sys.stderr)
                        continue

                    if device_columns is None:
                        if line.startswith("timestamp_ms"):
                            device_columns = line.split(",")
                            mode = "4-sensor" if "temperature_C" in device_columns else "3-sensor fallback"
                            if status_line_active:
                                print()
                                status_line_active = False
                            print(f"Header received ({mode} mode): {line}")
                            writer.writerow(["computer_timestamp"] + device_columns)
                            f.flush()
                        continue  # nothing to log until the header shows up

                    fields = line.split(",")
                    if len(fields) != len(device_columns):
                        print(f"[skipped malformed line] {line}", file=sys.stderr)
                        continue

                    computer_timestamp = datetime.now().isoformat(timespec="seconds")
                    writer.writerow([computer_timestamp] + fields)
                    f.flush()

                    # quick live status line - pull out whatever's present by column name
                    row = dict(zip(device_columns, fields))
                    status_parts = [f"t={row['timestamp_ms']}ms"]
                    if "temperature_C" in row:
                        status_parts.append(f"T={float(row['temperature_C']):.1f}C")
                        status_parts.append(f"H={float(row['humidity_pct']):.1f}%")
                    status_parts.append(f"MQ137={float(row['mq137_voltage_V']):.3f}V")
                    status_parts.append(f"MQ138={float(row['mq138_voltage_V']):.3f}V")
                    status_parts.append(f"TGS2620={row['tgs2620_mode']}/{float(row['tgs2620_voltage_V']):.3f}V")
                    print("\r" + "  ".join(status_parts), end="", flush=True)
                    status_line_active = True

            except KeyboardInterrupt:
                print("\nStopped.")
            finally:
                ser.close()
    finally:
        end_str = datetime.now().strftime(TIME_FORMAT)
        final_path = os.path.join(OUTPUT_DIR, f"4_sensor_log ({start_str} ~ {end_str}).csv")
        os.rename(running_path, final_path)
        print(f"Log saved to {final_path}")


if __name__ == "__main__":
    main()
