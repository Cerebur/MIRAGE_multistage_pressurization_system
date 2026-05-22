#include <Arduino.h>
#include "MPX5500/MPX5500Sensor.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <math.h>

// The G4-4 Diaphragm Pump White Wire (SP) is on Pin 9 (OCR1A)
const int DIAPHRAGM_PUMP_PIN = 9;
// The W6 Piston Pump MOSFET Gate is on Pin 10 (OCR1B)
const int PISTON_PUMP_PIN = 10;

static const unsigned long READ_INTERVAL_MS = 1000;  // Update interval (1 second)
unsigned long lastUpdateTime = 0;                    // Tracks the last execution timestamp

// Global tracking for individual pump speeds
uint8_t diaphragmSpeedPercentage = 0;
uint8_t pistonSpeedPercentage = 0;

MPX5500Sensor pressureSensor(A3);
MPX5500Data   pressureData;

// BMP280 over I2C (SDA=A4, SCL=A5 on AVR/Uno)
Adafruit_BMP280 bmp;
float bmp_temp_c = 0.0;
float bmp_pressure_hPa = 0.0;

String inletChoked = "unknown";

// Function declarations
void setupTimer1_20kHz();
void setPumpSpeeds(uint8_t diaphragmPct, uint8_t pistonPct);
void printSystemStatus();

void setup() {
  Serial.begin(115200);
  
  // Initialize the 20kHz PWM timer for BOTH pins 9 and 10
  setupTimer1_20kHz();
  
  // Make sure both pumps are off at startup
  setPumpSpeeds(0, 0);

  Serial.println(F("System Initialized. Dual Pump Control Active (20kHz)"));
  Serial.println(F("Commands:"));
  Serial.println(F("  <n>        -> set BOTH pumps to <n>% (0-100)"));
  Serial.println(F("  d<n>       -> set Diaphragm pump to <n>%"));
  Serial.println(F("  p<n>       -> set Piston pump to <n>%"));
  Serial.println(F("  s or status-> print status"));
  Serial.println(F("  h or help  -> print this help"));

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
  Serial.println(F("DATA,ts_ms,diaphragm_pct,piston_pct,mpx_pressure_hPa,bmp_temp_C,bmp_pressure_hPa"));

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

    // Print the consolidated sensor data and speed settings
    printSystemStatus();
  }

  // Task 2: Listen for user serial command inputs asynchronously
  if (Serial.available() > 0) {
    // Read the incoming line (until newline) and trim whitespace
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    // Help/status commands
    if (line.equalsIgnoreCase("h") || line.equalsIgnoreCase("help")) {
      Serial.println(F("Commands:"));
      Serial.println(F("  <n>        -> set BOTH pumps to <n>% (0-100)"));
      Serial.println(F("  d<n>       -> set Diaphragm pump to <n>%"));
      Serial.println(F("  p<n>       -> set Piston pump to <n>%"));
      Serial.println(F("  s or status-> print status"));
      return;
    }

    if (line.equalsIgnoreCase("s") || line.equalsIgnoreCase("status")) {
      printSystemStatus();
      return;
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

/**
 * Clean data print showing system dynamics once a second
 */
void printSystemStatus() {
  Serial.println(F("\n--- SYSTEM STATUS REPORT ---"));
  Serial.print(F("Diaphragm Output : ")); Serial.print(diaphragmSpeedPercentage); Serial.println(F("%"));
  Serial.print(F("Piston Output     : ")); Serial.print(pistonSpeedPercentage); Serial.println(F("%"));
  Serial.print(F("Chamber Pressure    : ")); Serial.print(pressureData.pressure_hPa+1015, 2); Serial.println(F(" hPa"));
  Serial.print(F("BMP280 Temperature  : ")); Serial.print(bmp_temp_c, 2); Serial.println(F(" °C"));
  Serial.print(F("BMP280 Pressure     : ")); Serial.print(bmp_pressure_hPa, 2); Serial.println(F(" hPa"));
  Serial.println(F("----------------------------"));

  // Machine-readable CSV data line for host-side logging and plotting
  // Compose CSV DATA line using dtostrf for float->string on AVR
  char buf[200];
  char mpx_buf[32] = {0};
  char bmp_t_buf[32] = {0};
  char bmp_p_buf[32] = {0};

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

  snprintf(buf, sizeof(buf), "DATA,%lu,%u,%u,%s,%s,%s",
           (unsigned long)millis(), (unsigned int)diaphragmSpeedPercentage, (unsigned int)pistonSpeedPercentage,
           mpx_buf, bmp_t_buf, bmp_p_buf);
  Serial.println(buf);

  // (Host-side logger should capture Serial output and write to logs/)
}