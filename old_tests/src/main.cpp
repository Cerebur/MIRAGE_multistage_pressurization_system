#include <Arduino.h>
#include "MPX5500/MPX5500Sensor.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <math.h>

// Set to false to disable DHT11 support in the program
#define ENABLE_DHT11 false

#if ENABLE_DHT11
// DHT11 temperature/humidity sensor
#include <DHT.h>
#endif

// The G4-4 Diaphragm Pump White Wire (SP) is on Pin 9 (OCR1A)
const int DIAPHRAGM_PUMP_PIN = 9;
// The W6 Piston Pump MOSFET Gate is on Pin 10 (OCR1B)
const int PISTON_PUMP_PIN = 10;
// The solenoid valve is driven by an NPN transistor base from Arduino D4
const int SOLENOID_VALVE_PIN = 4;
bool solenoidOpen = false;

static const unsigned long READ_INTERVAL_MS = 1000;  // Update interval (1 second)
unsigned long lastUpdateTime = 0;                    // Tracks the last execution timestamp

// Global tracking for individual pump speeds
uint8_t diaphragmSpeedPercentage = 0;
uint8_t pistonSpeedPercentage = 0;

// Operating modes
enum OperatingMode {
  MODE_MANUAL,
  MODE_CYCLIC_PISTON
};

OperatingMode currentMode = MODE_MANUAL;

// Cyclic mode parameters
unsigned long cycleIntervalMs = 0;    // Time between pump activations (milliseconds)
unsigned long cycleDurationMs = 0;    // Duration of pump activation (milliseconds)
unsigned long lastCycleStartTime = 0; // When the last cycle started
bool pistonActiveDuringCycle = false; // Whether piston is currently active in cycle
uint8_t pistonCyclePwm = 100;         // PWM level (0-100%) for piston pump during activation

MPX5500Sensor pressureSensor(A3);
MPX5500Data   pressureData;

// BMP280 over I2C (SDA=A4, SCL=A5 on AVR/Uno)
Adafruit_BMP280 bmp;
float bmp_temp_c = 0.0;
float bmp_pressure_hPa = 0.0;

float dht_temp_c = NAN;
float dht_humidity = NAN;

#if ENABLE_DHT11
// DHT11 on analog pin A1
#define DHTTYPE DHT11
DHT dht(A1, DHTTYPE);
unsigned long lastDHTRead = 0;
static const unsigned long DHT_READ_INTERVAL_MS = 2000; // DHT11 min read interval ~1-2s
#endif

String inletChoked = "unknown";

// Function declarations
void setupTimer1_20kHz();
void setPumpSpeeds(uint8_t diaphragmPct, uint8_t pistonPct);
void setSolenoidValve(bool open);
void printSystemStatus();

void setup() {
  Serial.begin(115200);
  
  // Initialize the 20kHz PWM timer for BOTH pins 9 and 10
  setupTimer1_20kHz();
  
  // Make sure both pumps are off at startup
  setPumpSpeeds(0, 0);
  pinMode(SOLENOID_VALVE_PIN, OUTPUT);
  setSolenoidValve(false);

  Serial.println(F("System Initialized. Dual Pump Control Active (20kHz)"));
  Serial.println(F("Commands:"));
  Serial.println(F("  MANUAL MODE:"));
  Serial.println(F("    <n>        -> set BOTH pumps to <n>% (0-100)"));
  Serial.println(F("    d<n>       -> set Diaphragm pump to <n>%"));
  Serial.println(F("    p<n>       -> set Piston pump to <n>%"));
  Serial.println(F("  CYCLIC MODE:"));
  Serial.println(F("    m or mode  -> toggle between Manual and Cyclic modes"));
  Serial.println(F("    i<n>       -> set cycle interval to <n> seconds"));
  Serial.println(F("    c<n>       -> set cycle duration to <n> seconds"));
  Serial.println(F("    d<n>       -> set Diaphragm pump to <n>% (works in Cyclic mode)"));
  Serial.println(F("    cp<n>      -> set Piston pump PWM to <n>% for activation"));
  Serial.println(F("  SOLENOID VALVE:"));
  Serial.println(F("    open       -> open the valve"));
  Serial.println(F("    close      -> close the valve"));
  Serial.println(F("  OTHER:"));
  Serial.println(F("    s or status-> print status"));
  Serial.println(F("    h or help  -> print this help"));

  // Ask user for inlet choked metadata
  Serial.println(F("Is the inlet choked? (y/n) ->"));
  unsigned long startPrompt = millis();
  while (millis() - startPrompt < 10000) { // wait up to 10s for reply
    if (Serial.available()) {
      String resp = Serial.readStringUntil('\n');
      resp.trim();
      if (resp.length() > 0) {
        char c = tolower(resp.charAt(0));
        if (c == 'y') inletChoked = "yes";
        else if (c == 'n') inletChoked = "no";
        else inletChoked = resp;
        break;
      }
    }
  }
  if (inletChoked == "unknown") {
    Serial.println(F("No inlet choked response; defaulting to 'unknown'."));
  } else {
    Serial.print(F("Inlet choked: ")); Serial.println(inletChoked);
  }

  // Emit a CSV header for machine-readable logging/plotting (and META for metadata)
  Serial.print(F("META,inlet_choked,")); Serial.println(inletChoked);
  Serial.println(F("DATA,ts_ms,diaphragm_pct,piston_pct,mpx_pressure_hPa,bmp_temp_C,bmp_pressure_hPa,dht_temp_C,dht_humidity"));

  // Initialize I2C and BMP280
  Wire.begin();
  if (!bmp.begin(0x76)) {
    // try common alternate address
    if (!bmp.begin(0x77)) {
      Serial.println(F("Warning: BMP280 not found at 0x76 or 0x77"));
    } else {
      Serial.println(F("BMP280 found at 0x77"));
    }
  } else {
    Serial.println(F("BMP280 found at 0x76"));
  }

#if ENABLE_DHT11
  // Initialize DHT sensor
  dht.begin();
#else
  Serial.println(F("DHT11 support disabled."));
#endif
}

void loop() {
  unsigned long currentMillis = millis();

  // Task 1: Non-blocking periodic sensor reading & status print (Once a second)
  if (currentMillis - lastUpdateTime >= READ_INTERVAL_MS) {
    lastUpdateTime = currentMillis;

    // Update the pressure sensor readings
    pressureSensor.read(pressureData);

    // Update BMP280 readings if available
    // we only need to read bmp if it's present; bmp.begin() is expensive so check once
    static bool bmp_initialized = false;
    static bool bmp_present = false;
    if (!bmp_initialized) {
      bmp_initialized = true;
      if (bmp.begin(0x76) || bmp.begin(0x77)) {
        bmp_present = true;
      }
    }
    if (bmp_present) {
      bmp_temp_c = bmp.readTemperature();
      bmp_pressure_hPa = bmp.readPressure() / 100.0F;
    }

#if ENABLE_DHT11
    // Read DHT11 at a safe interval (DHT11 should not be polled too quickly)
    if (currentMillis - lastDHTRead >= DHT_READ_INTERVAL_MS) {
      lastDHTRead = currentMillis;
      float t = dht.readTemperature();
      float h = dht.readHumidity();
      if (isnan(t)) dht_temp_c = NAN; else dht_temp_c = t;
      if (isnan(h)) dht_humidity = NAN; else dht_humidity = h;
    }
#else
    dht_temp_c = NAN;
    dht_humidity = NAN;
#endif

    // Print the consolidated sensor data and speed settings
    printSystemStatus();
  }

  // Task 2: Handle cyclic mode piston pump timing (checked frequently for responsive control)
  if (currentMode == MODE_CYCLIC_PISTON && cycleIntervalMs > 0) {
    unsigned long currentMillis2 = millis();
    unsigned long elapsedSinceCycleStart = currentMillis2 - lastCycleStartTime;
    
    if (!pistonActiveDuringCycle && elapsedSinceCycleStart >= cycleIntervalMs) {
      // Start a new cycle
      lastCycleStartTime = currentMillis2;
      pistonActiveDuringCycle = true;
      pistonSpeedPercentage = pistonCyclePwm; // Activate at configured PWM level
      setPumpSpeeds(diaphragmSpeedPercentage, pistonSpeedPercentage);
      Serial.print(F(">> Cyclic: Piston pump activated at "));
      Serial.print(pistonCyclePwm);
      Serial.println(F("%"));
    } else if (pistonActiveDuringCycle && elapsedSinceCycleStart >= cycleDurationMs) {
      // End the current activation
      pistonActiveDuringCycle = false;
      pistonSpeedPercentage = 0; // Deactivate
      setPumpSpeeds(diaphragmSpeedPercentage, pistonSpeedPercentage);
      Serial.println(F(">> Cyclic: Piston pump deactivated"));
    }
  }

  // Task 3: Listen for user serial command inputs asynchronously
  if (Serial.available() > 0) {
    // Read the incoming line (until newline) and trim whitespace
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    // Help/status commands
    if (line.equalsIgnoreCase("h") || line.equalsIgnoreCase("help")) {
      Serial.println(F("Commands:"));
      Serial.println(F("  MANUAL MODE:"));
      Serial.println(F("    <n>        -> set BOTH pumps to <n>% (0-100)"));
      Serial.println(F("    d<n>       -> set Diaphragm pump to <n>%"));
      Serial.println(F("    p<n>       -> set Piston pump to <n>%"));
      Serial.println(F("  CYCLIC MODE:"));
      Serial.println(F("    m or mode  -> toggle between Manual and Cyclic modes"));
      Serial.println(F("    i<n>       -> set cycle interval to <n> seconds"));
      Serial.println(F("    c<n>       -> set cycle duration to <n> seconds"));
      Serial.println(F("    d<n>       -> set Diaphragm pump to <n>% (works in Cyclic mode)"));
      Serial.println(F("    cp<n>      -> set Piston pump PWM to <n>% for activation"));
      Serial.println(F("  SOLENOID VALVE:"));
      Serial.println(F("    open       -> open the valve"));
      Serial.println(F("    close      -> close the valve"));
      Serial.println(F("  OTHER:"));
      Serial.println(F("    s or status-> print status"));
      Serial.println(F("    h or help  -> print this help"));
      return;
    }

    if (line.equalsIgnoreCase("s") || line.equalsIgnoreCase("status")) {
      printSystemStatus();
      return;
    }

    if (line.equalsIgnoreCase("open") || line.equalsIgnoreCase("valve open")) {
      setSolenoidValve(true);
      return;
    }

    if (line.equalsIgnoreCase("close") || line.equalsIgnoreCase("valve close")) {
      setSolenoidValve(false);
      return;
    }

    if (line.equalsIgnoreCase("m") || line.equalsIgnoreCase("mode")) {
      // Toggle between modes
      if (currentMode == MODE_MANUAL) {
        currentMode = MODE_CYCLIC_PISTON;
        Serial.println(F(">> Switched to CYCLIC mode (piston pump will cycle)"));
        Serial.println(F(">> Use 'i<n>' to set interval and 'c<n>' to set duration (in seconds)"));
      } else {
        currentMode = MODE_MANUAL;
        // Turn off piston pump when switching back to manual
        pistonActiveDuringCycle = false;
        pistonSpeedPercentage = 0;
        setPumpSpeeds(diaphragmSpeedPercentage, pistonSpeedPercentage);
        Serial.println(F(">> Switched to MANUAL mode"));
      }
      return;
    }

    // Cyclic mode configuration commands
    if (line.charAt(0) == 'i' || line.charAt(0) == 'I') {
      if (currentMode != MODE_CYCLIC_PISTON) {
        Serial.println(F(">> Error: Switch to Cyclic mode first using 'm' or 'mode'"));
        return;
      }
      String num = line.substring(1);
      num.trim();
      unsigned long intervalSecs = num.toInt();
      if (intervalSecs < 1) {
        Serial.println(F(">> Error: Interval must be >= 1 second"));
        return;
      }
      cycleIntervalMs = intervalSecs * 1000;
      Serial.print(F(">> Cycle interval set to: "));
      Serial.print(intervalSecs);
      Serial.println(F(" seconds"));
      return;
    }

    if (line.charAt(0) == 'c' || line.charAt(0) == 'C') {
      if (currentMode != MODE_CYCLIC_PISTON) {
        Serial.println(F(">> Error: Switch to Cyclic mode first using 'm' or 'mode'"));
        return;
      }
      
      // Check if this is 'cp' (cyclic piston PWM) or just 'c' (cycle duration)
      if (line.length() > 1 && (line.charAt(1) == 'p' || line.charAt(1) == 'P')) {
        // Handle cyclic piston PWM command (cp<n>)
        String num = line.substring(2);
        num.trim();
        unsigned long pwmLevel = num.toInt();
        if (pwmLevel < 0) pwmLevel = 0;
        if (pwmLevel > 100) pwmLevel = 100;
        pistonCyclePwm = (uint8_t)pwmLevel;
        Serial.print(F(">> Piston pump PWM (cyclic activation) set to: "));
        Serial.print(pistonCyclePwm);
        Serial.println(F("%"));
        return;
      } else {
        // Handle cycle duration command (c<n>)
        String num = line.substring(1);
        num.trim();
        unsigned long durationSecs = num.toInt();
        if (durationSecs < 1) {
          Serial.println(F(">> Error: Duration must be >= 1 second"));
          return;
        }
        cycleDurationMs = durationSecs * 1000;
        Serial.print(F(">> Cycle duration set to: "));
        Serial.print(durationSecs);
        Serial.println(F(" seconds"));
        return;
      }
    }

    // Manual mode pump control commands (block direct piston control in cyclic mode)
    if (currentMode == MODE_CYCLIC_PISTON) {
      // Allow diaphragm control in cyclic mode, but not direct piston PWM
      if (line.charAt(0) == 'p' || line.charAt(0) == 'P') {
        Serial.println(F(">> Error: Direct piston control not allowed in Cyclic mode. Use 'cp<n>' to set activation PWM."));
        return;
      }
      // Plain numeric commands are also not allowed in cyclic mode
      if (isdigit(line.charAt(0))) {
        Serial.println(F(">> Error: Numeric pump commands not allowed in Cyclic mode. Use 'd<n>' for diaphragm control."));
        return;
      }
    }

    // Determine command type: starts with 'd' or 'p' or plain number
    char first = tolower(line.charAt(0));
    int value = 0;
    if (first == 'd' || first == 'p') {
      String num = line.substring(1);
      num.trim();
      value = num.toInt();
      if (value < 0) value = 0;
      if (value > 100) value = 100;

      if (first == 'd') {
        diaphragmSpeedPercentage = (uint8_t)value;
        setPumpSpeeds(diaphragmSpeedPercentage, pistonSpeedPercentage);
        Serial.print(F(">> Diaphragm set to: "));
        Serial.print(diaphragmSpeedPercentage);
        Serial.println(F("%"));
      } else {
        pistonSpeedPercentage = (uint8_t)value;
        setPumpSpeeds(diaphragmSpeedPercentage, pistonSpeedPercentage);
        Serial.print(F(">> Piston set to: "));
        Serial.print(pistonSpeedPercentage);
        Serial.println(F("%"));
      }
    } else {
      // Treat as a plain numeric value for BOTH pumps
      value = line.toInt();
      if (value < 0) value = 0;
      if (value > 100) value = 100;
      diaphragmSpeedPercentage = pistonSpeedPercentage = (uint8_t)value;
      setPumpSpeeds(diaphragmSpeedPercentage, pistonSpeedPercentage);
      Serial.print(F(">> BOTH pumps set to: "));
      Serial.print(diaphragmSpeedPercentage);
      Serial.println(F("%"));
    }
  }
}

/**
 * Configures Timer 1 for 20kHz Phase and Frequency Correct PWM on BOTH Pin 9 and Pin 10
 */
void setupTimer1_20kHz() {
  pinMode(DIAPHRAGM_PUMP_PIN, OUTPUT);
  pinMode(PISTON_PUMP_PIN, OUTPUT);

  // Stop Timer 1 to safely manipulate registers
  TCCR1A = 0;
  TCCR1B = 0;

  // Set Mode 8: Phase and Frequency Correct PWM, TOP is determined by ICR1
  TCCR1B |= (1 << WGM13);

  // Enable non-inverting PWM output on Pin 9 (COM1A1 = 1)
  TCCR1A |= (1 << COM1A1);
  
  // Enable non-inverting PWM output on Pin 10 (COM1B1 = 1)
  TCCR1A |= (1 << COM1B1);

  // Set TOP value for 20kHz frequency
  // Formula: f_PWM = f_clk / (2 * Prescaler * TOP) -> 20k = 16M / (2 * 1 * 400)
  ICR1 = 400;

  // Start the timer with No Prescaling (CS10 = 1)
  TCCR1B |= (1 << CS10);

  // Initialize both duty cycles to 0 (Fully off)
  OCR1A = 0;
  OCR1B = 0;
}

/**
 * Sets the dual duty cycle based on a 0-100 percentage.
 * Maps the 0-100% range to the timer's 0-400 TOP range.
 */
void setPumpSpeeds(uint8_t diaphragmPct, uint8_t pistonPct) {
  uint16_t diaTimerValue = (diaphragmPct * 400) / 100;
  uint16_t pisTimerValue = (pistonPct * 400) / 100;

  // OCR1A handles Pin 9 (G4-4 Diaphragm Pump Control Signal)
  OCR1A = diaTimerValue;

  // OCR1B handles Pin 10 (W6 Piston Pump MOSFET Gate Driver)
  OCR1B = pisTimerValue;
}

void setSolenoidValve(bool open) {
  solenoidOpen = open;
  digitalWrite(SOLENOID_VALVE_PIN, open ? HIGH : LOW);
  Serial.print(F(">> Solenoid valve "));
  Serial.println(open ? F("OPEN") : F("CLOSED"));
}

/**
 * Clean data print showing system dynamics once a second
 */
void printSystemStatus() {
  Serial.println(F("\n--- SYSTEM STATUS REPORT ---"));
  Serial.print(F("Operating Mode    : "));
  if (currentMode == MODE_MANUAL) {
    Serial.println(F("MANUAL"));
  } else {
    Serial.print(F("CYCLIC (Interval: "));
    Serial.print(cycleIntervalMs / 1000);
    Serial.print(F("s, Duration: "));
    Serial.print(cycleDurationMs / 1000);
    Serial.print(F("s, PWM: "));
    Serial.print(pistonCyclePwm);
    Serial.print(F("%, Active: "));
    Serial.print(pistonActiveDuringCycle ? "YES" : "NO");
    Serial.println(F(")"));
  }
  Serial.print(F("Diaphragm Output : ")); Serial.print(diaphragmSpeedPercentage); Serial.println(F("%"));
  Serial.print(F("Piston Output     : ")); Serial.print(pistonSpeedPercentage); Serial.println(F("%"));
  Serial.print(F("Solenoid Valve    : ")); Serial.println(solenoidOpen ? F("OPEN") : F("CLOSED"));
  Serial.print(F("Chamber Pressure    : ")); Serial.print(pressureData.pressure_hPa+1015, 2); Serial.println(F(" hPa"));
  Serial.print(F("BMP280 Temperature  : ")); Serial.print(bmp_temp_c, 2); Serial.println(F(" °C"));
  Serial.print(F("BMP280 Pressure     : ")); Serial.print(bmp_pressure_hPa, 2); Serial.println(F(" hPa"));
#if ENABLE_DHT11
  Serial.print(F("DHT11 Temperature  : "));
  if (!isfinite(dht_temp_c)) Serial.print(F("NaN")); else Serial.print(dht_temp_c, 2);
  Serial.println(F(" °C"));
  Serial.print(F("DHT11 Humidity     : "));
  if (!isfinite(dht_humidity)) Serial.print(F("NaN")); else Serial.print(dht_humidity, 2);
  Serial.println(F(" %"));
#else
  Serial.println(F("DHT11 Sensor      : DISABLED"));
#endif
  Serial.println(F("----------------------------"));

  // Machine-readable CSV data line for host-side logging and plotting
  // Compose CSV DATA line using dtostrf for float->string on AVR
  char buf[200];
  char mpx_buf[32] = {0};
  char bmp_t_buf[32] = {0};
  char bmp_p_buf[32] = {0};
  char dht_t_buf[32] = {0};
  char dht_h_buf[32] = {0};

  if (!isfinite(pressureData.pressure_hPa)) {
    strcpy(mpx_buf, "NaN");
  } else {
    dtostrf(pressureData.pressure_hPa, 6, 2, mpx_buf); // width=6, prec=2
  }

  if (!isfinite(bmp_temp_c)) {
    strcpy(bmp_t_buf, "NaN");
  } else {
    dtostrf(bmp_temp_c, 6, 2, bmp_t_buf);
  }

  if (!isfinite(bmp_pressure_hPa)) {
    strcpy(bmp_p_buf, "NaN");
  } else {
    dtostrf(bmp_pressure_hPa, 6, 2, bmp_p_buf);
  }

  if (!isfinite(dht_temp_c)) {
    strcpy(dht_t_buf, "NaN");
  } else {
    dtostrf(dht_temp_c, 6, 2, dht_t_buf);
  }

  if (!isfinite(dht_humidity)) {
    strcpy(dht_h_buf, "NaN");
  } else {
    dtostrf(dht_humidity, 6, 2, dht_h_buf);
  }
  snprintf(buf, sizeof(buf), "DATA,%lu,%u,%u,%s,%s,%s,%s,%s",
           (unsigned long)millis(), (unsigned int)diaphragmSpeedPercentage, (unsigned int)pistonSpeedPercentage,
           mpx_buf, bmp_t_buf, bmp_p_buf, dht_t_buf, dht_h_buf);
  Serial.println(buf);

  // (Host-side logger should capture Serial output and write to logs/)
}