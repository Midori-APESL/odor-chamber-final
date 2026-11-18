# Hardware

Board: Arduino Uno R4 Minima (14-bit ADC, 5 V). Serial 115200 baud.

| Sensor | Pin | Measures |
|---|---|---|
| TGS2620 | A0 | combustible gas / VOC (auto-ranging load resistor, switch on D2) |
| MQ137 | A1 | NH3 |
| MQ138 | A2 | VOC |
| BME688 | I2C (SDA/SCL) | temperature, pressure, humidity, 10-step gas heater profile |

Do not wire analog sensors to A4/A5: they share the I2C lines used by the BME688.

Library: Bosch `bme68xLibrary` (install via Arduino Library Manager).

The sketch outputs raw ADC counts and voltages (plus TGS2620 Rs in ohms); ppm conversion happens in `software/`.
