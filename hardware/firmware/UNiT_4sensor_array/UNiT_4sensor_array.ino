#include <bme68xLibrary.h>
#define NEW_GAS_MEAS (BME68X_GASM_VALID_MSK | BME68X_HEAT_STAB_MSK | BME68X_NEW_DATA_MSK)
#define MEAS_DUR 140
#define NUM_PROFILES 10

// Analog sensor pins - A4/A5 are NOT available here: on the Uno R4 Minima
// (as on R3) they are the same electrical net as the SDA/SCL header pins
// used by the BME688, so anything analog wired there corrupts the I2C bus.
#define MQ137_AO_PIN A1    // NH3
#define MQ138_AO_PIN A2    // VOC
#define TGS2620_AO_PIN A0  // General combustible gas / VOC
#define TGS2620_SWITCH_PIN 2

const float ADC_MAX = 16383.0; // 14-bit resolution, matches analogReadResolution(14) below
const float VCC = 5.0;

// TGS2620 auto-ranging thresholds and load resistors, carried over from
// TGS2620_autoranging.ino but rescaled from its original 10-bit ADC (0-1023)
// to the 14-bit ADC (0-16383) used here so the switch points line up the same.
const int TGS2620_HIGH_THRESHOLD = 13612; // was 850 @ 10-bit
const int TGS2620_LOW_THRESHOLD  = 2402;  // was 150 @ 10-bit
const float TGS2620_R_HIGH_SENSITIVITY = 20000.0; // 20K load resistor
const float TGS2620_R_LOW_SENSITIVITY  = 1983.0;  // 20K parallel 2.2K combo
bool tgs2620HighConcentrationMode = false;

// Confirmed via logged tgs2620_mode data (ammonia + linalyl acetate runs):
// once the raw reading sits near a threshold, the mode was flipping on
// literally every single sample for the rest of the run - not real sensor
// response, just the range switch fighting itself. A minimum dwell time
// between switches breaks that regardless of where the signal sits.
const unsigned long TGS2620_SWITCH_COOLDOWN_MS = 3000;
unsigned long lastTgsSwitchMs = 0;

// Set false in setup() if the BME688 doesn't answer on I2C, so the board
// keeps streaming MQ137/MQ138/TGS2620 instead of hanging forever on a
// sensor those three don't depend on.
bool bmeAvailable = true;

Bme68x bme;
// Buffers one in-progress heater-profile sweep (one feature vector: 10
// gas_index resistances + temp/press/hum). The BME688 is qualitative and
// changes slowly - a full sweep takes seconds - so it runs on its own
// timescale here and is never allowed to gate the MQ137/MQ138/TGS2620
// reads below, which need to report every ANALOG_INTERVAL_MS regardless
// of how far along the current sweep is.
float gasBuf[NUM_PROFILES];
bool gasFilled[NUM_PROFILES];
float tempBuf, pressBuf, humBuf;
bool haveTPH;

// If a sweep hasn't fully landed within this long, fold in whatever we have
// (skipping any step that never came in) instead of blocking forever - one
// heater step that never stabilizes shouldn't stop the cache below from
// ever updating again.
const unsigned long SWEEP_TIMEOUT_MS = 45000;
unsigned long sweepStartMs = 0;

// Most recently completed BME688 reading, held between sweeps. Temp,
// humidity, and gas resistance drift slowly compared to ppm, so it's fine
// (and much better than blocking) for the fast analog rows below to repeat
// a several-seconds-old value while the next sweep is still running.
float lastTempBuf = 0, lastPressBuf = 0, lastHumBuf = 0;
float lastGasBuf[NUM_PROFILES];
bool lastGasKnown[NUM_PROFILES];
bool lastTPHKnown = false;

// MQ137/MQ138/TGS2620 sample+report cadence - independent of, and much
// faster than, the BME688 sweep above. 500ms by default; drop to 1000ms if
// the row rate is more than you need.
const unsigned long ANALOG_INTERVAL_MS = 500;
unsigned long lastAnalogPrintMs = 0;

void resetSweep() {
  for (int i = 0; i < NUM_PROFILES; i++) gasFilled[i] = false;
  haveTPH = false;
  sweepStartMs = millis();
}

void setup() {
  // DEBUG: proves the MCU is actually booting/running this sketch,
  // independent of Serial/USB.
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }

  Serial.begin(115200);
  while (!Serial); // Wait for Serial to initialize
  Serial.println("DEBUG: Serial ready");

  Wire.begin();
  Serial.println("DEBUG: Wire ready");
  analogReadResolution(14); // shared by MQ137/MQ138/TGS2620 reads below

  pinMode(TGS2620_SWITCH_PIN, INPUT); // Start in high-sensitivity mode (20K active, 2.2K disconnected)

  Serial.println("DEBUG: calling bme.begin...");
  bme.begin(0x77, Wire); // BME688 confirmed at address 0x77 via I2C scan
  Serial.println("DEBUG: bme.begin returned, status=" + String(bme.checkStatus()));
  if (bme.checkStatus() == BME68X_ERROR) {
    // Don't hang the board over a sensor the analog three don't need -
    // fall back to 3-sensor mode and keep going.
    Serial.println("BME688 error: " + bme.statusString() + " -- continuing without it (3-sensor mode)");
    bmeAvailable = false;
  } else if (bme.checkStatus() == BME68X_WARNING) {
    Serial.println("BME688 warning: " + bme.statusString());
  }

  for (int i = 0; i < NUM_PROFILES; i++) lastGasKnown[i] = false;

  if (bmeAvailable) {
    bme.setTPH(); // Default temperature/pressure/humidity oversampling
    Serial.println("DEBUG: TPH configured");
    // Heater temperature (deg C) per profile step - scans across 10 set-points
    // so each measurement cycle produces a temperature-resolved gas resistance
    // "fingerprint" instead of a single reading.
    uint16_t tempProf[NUM_PROFILES] = { 320, 100, 100, 100, 200, 200, 200, 320, 320, 320 };
    // Multiplier applied to the shared heater duration for each step
    uint16_t mulProf[NUM_PROFILES] = { 5, 2, 10, 30, 5, 5, 5, 5, 5, 5 };
    uint16_t sharedHeatrDur = MEAS_DUR - (bme.getMeasDur(BME68X_PARALLEL_MODE) / 1000);
    bme.setHeaterProf(tempProf, mulProf, sharedHeatrDur, NUM_PROFILES);
    bme.setOpMode(BME68X_PARALLEL_MODE);
    resetSweep();
    Serial.println("DEBUG: heater profile + op mode set, printing header now");
  } else {
    Serial.println("DEBUG: skipping BME688 setup, printing header now");
  }

  // CSV header: one row per emitted reading. In 4-sensor mode this is one
  // row per completed BME688 heater-profile sweep (temp/press/hum + 10
  // gas_index resistances) plus one MQ137/MQ138/TGS2620 reading. In
  // 3-sensor mode (BME688 unavailable) the BME688 columns are omitted
  // entirely and a row is emitted on a fixed timer instead - the web
  // dashboard detects which columns are present from this header line.
  Serial.print("timestamp_ms");
  if (bmeAvailable) {
    Serial.print(",temperature_C,pressure_Pa,humidity_pct");
    for (int i = 0; i < NUM_PROFILES; i++) {
      Serial.print(",gas_" + String(i) + "_ohm");
    }
  }
  Serial.print(",mq137_raw_adc,mq137_voltage_V");
  Serial.print(",mq138_raw_adc,mq138_voltage_V");
  Serial.print(",tgs2620_mode,tgs2620_raw_adc,tgs2620_voltage_V,tgs2620_Rs_ohm");
  Serial.println();
}

// Reads MQ137/MQ138/TGS2620 and prints their columns (leading comma
// included, since this always follows a timestamp_ms print). Shared by
// both the 4-sensor and 3-sensor loop paths so the analog logic - notably
// the TGS2620 auto-ranging - only lives in one place.
void printAnalogRow() {
  int mq137Raw = analogRead(MQ137_AO_PIN);
  float mq137Voltage = mq137Raw * (VCC / ADC_MAX);

  int mq138Raw = analogRead(MQ138_AO_PIN);
  float mq138Voltage = mq138Raw * (VCC / ADC_MAX);

  int tgs2620Raw = analogRead(TGS2620_AO_PIN);
  // Auto-range: swap in the 2.2K parallel resistor once the reading nears
  // either rail, then re-read so this row reflects the settled value. Gated
  // by a cooldown so a signal sitting right at the boundary can't flip the
  // range back and forth every single sample (see TGS2620_SWITCH_COOLDOWN_MS).
  bool tgsCooldownElapsed = (millis() - lastTgsSwitchMs) > TGS2620_SWITCH_COOLDOWN_MS;
  if (!tgs2620HighConcentrationMode && tgs2620Raw > TGS2620_HIGH_THRESHOLD && tgsCooldownElapsed) {
    pinMode(TGS2620_SWITCH_PIN, OUTPUT);
    digitalWrite(TGS2620_SWITCH_PIN, LOW); // Pulls 2.2K resistor to ground
    tgs2620HighConcentrationMode = true;
    lastTgsSwitchMs = millis();
    delay(100); // Let the circuit settle
    tgs2620Raw = analogRead(TGS2620_AO_PIN);
  } else if (tgs2620HighConcentrationMode && tgs2620Raw < TGS2620_LOW_THRESHOLD && tgsCooldownElapsed) {
    pinMode(TGS2620_SWITCH_PIN, INPUT); // High-impedance, disconnects 2.2K resistor
    tgs2620HighConcentrationMode = false;
    lastTgsSwitchMs = millis();
    delay(100);
    tgs2620Raw = analogRead(TGS2620_AO_PIN);
  }
  float tgs2620CurrentRl = tgs2620HighConcentrationMode ? TGS2620_R_LOW_SENSITIVITY : TGS2620_R_HIGH_SENSITIVITY;
  float tgs2620Voltage = tgs2620Raw * (VCC / ADC_MAX);
  float tgs2620Rs = 0.0;
  if (tgs2620Voltage > 0.0) {
    tgs2620Rs = ((VCC * tgs2620CurrentRl) / tgs2620Voltage) - tgs2620CurrentRl;
  }

  Serial.print("," + String(mq137Raw));
  Serial.print("," + String(mq137Voltage, 4));
  Serial.print("," + String(mq138Raw));
  Serial.print("," + String(mq138Voltage, 4));
  Serial.print("," + String(tgs2620HighConcentrationMode ? "HIGH" : "LOW"));
  Serial.print("," + String(tgs2620Raw));
  Serial.print("," + String(tgs2620Voltage, 4));
  Serial.print("," + String(tgs2620Rs, 1));
  Serial.println();
}

void loop() {
  if (!bmeAvailable) {
    // 3-sensor mode: no BME688 to decouple from, just report on the same
    // fast cadence the 4-sensor path below uses.
    delay(ANALOG_INTERVAL_MS);
    Serial.print(String(millis()));
    printAnalogRow();
    return;
  }

  bme68xData data;
  uint8_t nFieldsLeft = 0;
  delay(MEAS_DUR); // Matches the heater profile timing configured in setup()
  bool fetchOk = bme.fetchData();
  if (fetchOk) {
    do {
      nFieldsLeft = bme.getData(data);
      if (data.status == NEW_GAS_MEAS) {
        tempBuf = data.temperature;
        pressBuf = data.pressure;
        humBuf = data.humidity;
        haveTPH = true;
        if (data.gas_index < NUM_PROFILES) {
          gasBuf[data.gas_index] = data.gas_resistance;
          gasFilled[data.gas_index] = true;
        }
      }
    } while (nFieldsLeft);
  }

  // DEBUG: periodic sweep-progress readout, so a stalled sweep is visible
  // instead of just silence - shows how many of the 10 gas_index steps have
  // landed and whether fetchData() itself is even succeeding.
  static unsigned long lastProgressPrintMs = 0;
  if (millis() - lastProgressPrintMs > 2000) {
    int filledCount = 0;
    String missing = "";
    for (int i = 0; i < NUM_PROFILES; i++) {
      if (gasFilled[i]) {
        filledCount++;
      } else {
        missing += String(i) + " ";
      }
    }
    Serial.println("DEBUG: sweep progress " + String(filledCount) + "/" + String(NUM_PROFILES)
                    + " fetchOk=" + String(fetchOk) + " haveTPH=" + String(haveTPH)
                    + " missing=[" + missing + "]");
    lastProgressPrintMs = millis();
  }

  // Once every profile step has a fresh reading, or once SWEEP_TIMEOUT_MS
  // has passed without completing (whichever comes first), fold the result
  // into the held cache - this never prints anything itself anymore, it
  // just updates what the fast analog rows below will report. A step that
  // never landed this cycle simply leaves the previous cached value in
  // place rather than being blanked, so a flaky step degrades to "stale"
  // instead of "missing."
  bool sweepComplete = haveTPH;
  for (int i = 0; i < NUM_PROFILES && sweepComplete; i++) {
    sweepComplete = gasFilled[i];
  }
  bool sweepTimedOut = !sweepComplete && (millis() - sweepStartMs > SWEEP_TIMEOUT_MS);
  if (sweepComplete || sweepTimedOut) {
    if (sweepTimedOut) {
      Serial.println("DEBUG: sweep timed out after " + String(SWEEP_TIMEOUT_MS / 1000) + "s, folding in partial result");
    }
    if (haveTPH) {
      lastTempBuf = tempBuf; lastPressBuf = pressBuf; lastHumBuf = humBuf;
      lastTPHKnown = true;
    }
    for (int i = 0; i < NUM_PROFILES; i++) {
      if (gasFilled[i]) { lastGasBuf[i] = gasBuf[i]; lastGasKnown[i] = true; }
    }
    resetSweep();
  }

  // Fast path: MQ137/MQ138/TGS2620 on their own ANALOG_INTERVAL_MS timer,
  // reporting whatever BME688 snapshot is currently cached above - this is
  // what actually decouples the two, regardless of how long the sweep takes.
  if (millis() - lastAnalogPrintMs >= ANALOG_INTERVAL_MS) {
    lastAnalogPrintMs = millis();
    Serial.print(String(millis()));
    Serial.print(",");
    if (lastTPHKnown) Serial.print(String(lastTempBuf));
    Serial.print(",");
    if (lastTPHKnown) Serial.print(String(lastPressBuf));
    Serial.print(",");
    if (lastTPHKnown) Serial.print(String(lastHumBuf));
    for (int i = 0; i < NUM_PROFILES; i++) {
      Serial.print(",");
      if (lastGasKnown[i]) Serial.print(String(lastGasBuf[i]));
    }
    printAnalogRow();
  }
}
