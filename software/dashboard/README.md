# Odor chamber sensor dashboard

Single-file web dashboard for the odor detection chamber: live MQ137 / MQ138 / TGS2620 / BME688 readings over Web Serial, CSV logging, and a BME688 odour vector graph with a classify-a-sample tool.

**Live:** https://midori-apesl.github.io/odor-dashboard/

## Use

- Open in **Chrome or Edge** on the computer wired to the Arduino. Web Serial is not supported in Safari or Firefox.
- The page can be hosted anywhere, but the serial connection only works from the browser on the machine the Arduino is plugged into.
- The "Classify a sample" tool runs entirely in the browser. Uploaded CSVs are not sent anywhere.
