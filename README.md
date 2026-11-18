# Odor chamber – final

Code for the odor detection chamber: a 4-sensor Arduino array (BME688, MQ137, MQ138, TGS2620) streaming over serial to a browser dashboard.

## hardware/
- `firmware/UNiT_4sensor_array/` – Arduino sketch that reads all four sensors and prints one CSV row per sample at 115200 baud. See `hardware/README.md` for pins and libraries.

## software/
- `dashboard/` – single-file web dashboard (Web Serial, live readings, CSV logging, odour vector graph). Live: https://midori-apesl.github.io/odor-dashboard/
- `logging/log_4sensor_array.py` – raw CSV logger for the sketch's serial output.
- `calibration/voltage_to_ppm.py` – MQ137 voltage to NH3 ppm (same curve the dashboard uses).
- `analysis/build_pca_odour_map.py` – regenerates the scaler/PCA constants behind the dashboard's odour vector tab.

Note: `log_4sensor_array.py` and `voltage_to_ppm.py` contain hardcoded local paths; edit them for your machine. `build_pca_odour_map.py` calls the dashboard `odor_dashboard.html`; in this repo that file is `software/dashboard/index.html`.
