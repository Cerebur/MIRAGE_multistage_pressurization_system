#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <math.h>
#include <sensor_readout/sensor_readout.h>
#include <programmes/vacuum_pressure_programme.h>
#include <programmes/programme_framework.h>


// The G4-4 Diaphragm Pump White Wire (SP) is on Pin 9 (OCR1A)
const int DIAPHRAGM_PUMP_PIN = 9;
// The W6 Piston Pump MOSFET Gate is on Pin 10 (OCR1B)
const int PISTON_PUMP_PIN = 10;
// The solenoid valve is driven by an NPN transistor base from Arduino D7
const int SOLENOID_VALVE_PIN = 7;
bool solenoidOpen = false;

static const unsigned long READ_INTERVAL_MS = 1000;  // Update interval (1 second)
unsigned long lastUpdateTime = 0;                    // Tracks the last execution timestamp

// Global tracking for individual pump speeds
uint8_t diaphragmSpeedPercentage = 0;
uint8_t pistonSpeedPercentage = 0;

// Operating modes
enum OperatingMode {
  MODE_MANUAL,
  MODE_CYCLIC_PISTON,
  MODE_PROGRAMME
};

OperatingMode currentMode = MODE_MANUAL;

// Select which programme runs when automatic/programme mode is active.
const ProgrammeDefinition *ACTIVE_PROGRAMME = &VACUUM_PRESSURE_PROGRAMME;
ProgrammeOutputs programmeOutputs = {0, 0, false, false};
bool programmeHasBegun = false;
bool programmeFinished = false;

// Cyclic mode parameters
unsigned long cycleIntervalMs = 0;    // Time between pump activations (milliseconds)
unsigned long cycleDurationMs = 0;    // Duration of pump activation (milliseconds)
unsigned long lastCycleStartTime = 0; // When the last cycle started
bool pistonActiveDuringCycle = false; // Whether piston is currently active in cycle
uint8_t pistonCyclePwm = 100;         // PWM level (0-100%) for piston pump during activation


// Sensor data 
float tmp1075_c = 0.0;
float abp2_pressure_bar = 0.0;
float abp2_temp_c = 0.0;
float ms5803_pressure_bar = 0.0;
float ms5803_temp_c = 0.0;
float sht45_temp_c = 0.0;
float sht45_rh = 0.0;

float ABP2_REF = ms5803_pressure_bar;

float bmp_temp_c = 0.0;
float bmp_pressure_hPa = 0.0;

String META_TEST = "unknown";

// Function declarations
void setupTimer1_20kHz();
void setPumpSpeeds(uint8_t diaphragmPct, uint8_t pistonPct);
void setSolenoidValve(bool open);
void printSystemStatus();
void applyProgrammeOutputs(const ProgrammeOutputs &outputs);
void enterProgrammeMode();
void exitProgrammeMode();
void updateProgrammeMode(unsigned long currentMillis, bool safetyStopTriggered);

void setup() {
  Serial.begin(115200);
  
  Serial.println(F("Starting Initialization..."));
  // Initialize the 20kHz PWM timer for BOTH pins 9 and 10
  setupTimer1_20kHz();

  Serial.println(F("Initializing I2C and Sensors..."));
  // Initialize the sensors
  Wire.begin();
  initSensors(); 
  
  Serial.println(F("Setting Initial Pump Speeds and Solenoid State..."));
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
  Serial.println(F("  PROGRAMME MODE:"));
  Serial.println(F("    auto | programme | program -> start the selected programme"));
  Serial.println(F("    manual     -> leave programme mode and return to manual"));
  Serial.println(F("  SOLENOID VALVE:"));
  Serial.println(F("    open       -> open the valve"));
  Serial.println(F("    close      -> close the valve"));
  Serial.println(F("  OTHER:"));
  Serial.println(F("    s or status-> print status"));
  Serial.println(F("    h or help  -> print this help"));

  // Ask user for inlet choked metadata
  Serial.println(F("TEST META DATA ->"));
  unsigned long startPrompt = millis();
  while (millis() - startPrompt < 10000) { // wait up to 10s for reply
    if (Serial.available()) {
      String resp = Serial.readStringUntil('\n');
      resp.trim();
      if (resp.length() > 0) {
        META_TEST = resp;
        break;
      }
    }
  }
  if (META_TEST == "unknown") {
    Serial.println(F("No meta test response; defaulting to 'unknown'."));
  } else {
    Serial.print(F("Meta data of test: ")); Serial.println(META_TEST);
  }

  // Emit a CSV header for machine-readable logging/plotting (and META for metadata)
  Serial.print(F("META,")); Serial.println(META_TEST);
  Serial.println(F("DATA,ts_ms,diaphragm_pct,piston_pct,TMP1075_temp_C,abp2_temp_C,abp2_pressure_bar,abp2_ms_pressure_bar,MS5803_temp_C,MS5803_pressure_bar,BMP280_temperature_C,BMP280_pressure_bar,SHT45_temp_C,SHT45_humidity"));
}

void loop() {
  unsigned long currentMillis = millis();
  bool safetyStopTriggered = false;

  // Task 1: Non-blocking periodic sensor reading & status print (Once a second)
  if (currentMillis - lastUpdateTime >= READ_INTERVAL_MS) {
    lastUpdateTime = currentMillis;

    // Read sensors:
    tmp1075_c = readTMP1075();
    readABP2(abp2_pressure_bar, abp2_temp_c);
    abp2_pressure_bar += ABP2_REF; // Adjust ABP2 pressure relative to MS5803 reference
    readMS5803(ms5803_pressure_bar, ms5803_temp_c);
    readSHT45(sht45_temp_c, sht45_rh);
    if (bmp280Present) {
        readBMP280(bmp_temp_c, bmp_pressure_hPa);
        } else {
        bmp_temp_c = 0.0f;
        bmp_pressure_hPa = 0.0f;
        }

    // Stop all pumps if the ABP2 pressure exceeds 2.1 bar (safety limit)
    if (abp2_pressure_bar > 2.1f && (pistonSpeedPercentage > 0 || diaphragmSpeedPercentage > 0)) {
      Serial.println(F(">> Safety: ABP2 pressure exceeded 2.1 bar! Stopping all pumps."));
      if (currentMode == MODE_PROGRAMME) {
        if (ACTIVE_PROGRAMME->advanceOnSafetyStop) {
          safetyStopTriggered = true;
          Serial.println(F(">> Programme safety handling enabled: pumps forced off, stage will advance on update."));
        } else {
          programmeFinished = true;
          programmeOutputs = ProgrammeOutputs{0, 0, false, true};
        }
      }
      setPumpSpeeds(0, 0);
      if (currentMode != MODE_PROGRAMME || !ACTIVE_PROGRAMME->advanceOnSafetyStop) {
        setSolenoidValve(false);
        printSystemStatus();
        return;
      }
    }

    updateProgrammeMode(currentMillis, safetyStopTriggered);

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
      Serial.println(F("  PROGRAMME MODE:"));
      Serial.println(F("    auto | programme | program -> start the selected programme"));
      Serial.println(F("    manual     -> leave programme mode and return to manual"));
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
      if (currentMode == MODE_PROGRAMME) {
        Serial.println(F(">> Error: Programme mode owns the valve. Use 'manual' first."));
        return;
      }
      setSolenoidValve(true);
      return;
    }

    if (line.equalsIgnoreCase("close") || line.equalsIgnoreCase("valve close")) {
      if (currentMode == MODE_PROGRAMME) {
        Serial.println(F(">> Error: Programme mode owns the valve. Use 'manual' first."));
        return;
      }
      setSolenoidValve(false);
      return;
    }

    if (line.equalsIgnoreCase("auto") || line.equalsIgnoreCase("programme") || line.equalsIgnoreCase("program")) {
      enterProgrammeMode();
      return;
    }

    if (line.equalsIgnoreCase("manual")) {
      if (currentMode == MODE_PROGRAMME) {
        exitProgrammeMode();
      }
      currentMode = MODE_MANUAL;
      Serial.println(F(">> Switched to MANUAL mode"));
      return;
    }

    if (line.equalsIgnoreCase("cyclic")) {
      if (currentMode == MODE_PROGRAMME) {
        exitProgrammeMode();
      }
      currentMode = MODE_CYCLIC_PISTON;
      Serial.println(F(">> Switched to CYCLIC mode (piston pump will cycle)"));
      Serial.println(F(">> Use 'i<n>' to set interval and 'c<n>' to set duration (in seconds)"));
      return;
    }

    if (line.equalsIgnoreCase("m") || line.equalsIgnoreCase("mode")) {
      if (currentMode == MODE_MANUAL) {
        currentMode = MODE_CYCLIC_PISTON;
        Serial.println(F(">> Switched to CYCLIC mode (piston pump will cycle)"));
        Serial.println(F(">> Use 'i<n>' to set interval and 'c<n>' to set duration (in seconds)"));
      } else if (currentMode == MODE_CYCLIC_PISTON) {
        currentMode = MODE_MANUAL;
        // Turn off piston pump when switching back to manual
        pistonActiveDuringCycle = false;
        pistonSpeedPercentage = 0;
        setPumpSpeeds(diaphragmSpeedPercentage, pistonSpeedPercentage);
        Serial.println(F(">> Switched to MANUAL mode"));
      } else {
        exitProgrammeMode();
        currentMode = MODE_MANUAL;
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

    if (currentMode == MODE_PROGRAMME) {
      if (line.charAt(0) == 'd' || line.charAt(0) == 'D' || line.charAt(0) == 'p' || line.charAt(0) == 'P' || isdigit(line.charAt(0))) {
        Serial.println(F(">> Error: Programme mode controls pumps automatically. Use 'manual' to take over."));
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
  static uint8_t lastAppliedDiaphragmPct = 255;
  static uint8_t lastAppliedPistonPct = 255;

  diaphragmSpeedPercentage = diaphragmPct;
  pistonSpeedPercentage = pistonPct;

  if (lastAppliedDiaphragmPct == diaphragmPct && lastAppliedPistonPct == pistonPct) {
    return;
  }

  lastAppliedDiaphragmPct = diaphragmPct;
  lastAppliedPistonPct = pistonPct;

  uint16_t diaTimerValue = (diaphragmPct * 400) / 100;
  uint16_t pisTimerValue = (pistonPct * 400) / 100;

  // OCR1A handles Pin 9 (G4-4 Diaphragm Pump Control Signal)
  OCR1A = diaTimerValue;

  // OCR1B handles Pin 10 (W6 Piston Pump MOSFET Gate Driver)
  OCR1B = pisTimerValue;
}

void setSolenoidValve(bool open) {
  static bool hasAppliedOpen = false;
  static bool lastAppliedOpen = false;

  solenoidOpen = open;

  if (hasAppliedOpen && lastAppliedOpen == open) {
    return;
  }

  hasAppliedOpen = true;
  lastAppliedOpen = open;
  digitalWrite(SOLENOID_VALVE_PIN, open ? HIGH : LOW);
  Serial.print(F(">> Solenoid valve "));
  Serial.println(open ? F("OPEN") : F("CLOSED"));
}

void applyProgrammeOutputs(const ProgrammeOutputs &outputs) {
  setPumpSpeeds(outputs.diaphragmPct, outputs.pistonPct);
  setSolenoidValve(outputs.solenoidOpen);
}

void enterProgrammeMode() {
  if (currentMode == MODE_PROGRAMME) {
    Serial.print(F(">> Programme mode already active: "));
    Serial.println(ACTIVE_PROGRAMME->name);
    return;
  }

  if (currentMode == MODE_CYCLIC_PISTON) {
    pistonActiveDuringCycle = false;
    pistonSpeedPercentage = 0;
    setPumpSpeeds(diaphragmSpeedPercentage, pistonSpeedPercentage);
  }

  currentMode = MODE_PROGRAMME;
  programmeHasBegun = true;
  programmeFinished = false;
  programmeOutputs = ProgrammeOutputs{0, 0, false, false};
  ACTIVE_PROGRAMME->begin();
  applyProgrammeOutputs(programmeOutputs);
  Serial.print(F(">> Switched to PROGRAMME mode: "));
  Serial.println(ACTIVE_PROGRAMME->name);
}

void exitProgrammeMode() {
  if (!programmeHasBegun && currentMode != MODE_PROGRAMME) {
    return;
  }

  ACTIVE_PROGRAMME->end();
  programmeHasBegun = false;
  programmeFinished = false;
  programmeOutputs = ProgrammeOutputs{0, 0, false, false};
  setPumpSpeeds(0, 0);
  setSolenoidValve(false);
}

void updateProgrammeMode(unsigned long currentMillis, bool safetyStopTriggered) {
  if (currentMode != MODE_PROGRAMME || !programmeHasBegun || programmeFinished) {
    return;
  }

  ProgrammeInputs inputs;
  inputs.nowMs = currentMillis;
  inputs.tmp1075_c = tmp1075_c;
  inputs.abp2_pressure_bar = abp2_pressure_bar;
  inputs.abp2_temp_c = abp2_temp_c;
  inputs.ms5803_pressure_bar = ms5803_pressure_bar;
  inputs.ms5803_temp_c = ms5803_temp_c;
  inputs.sht45_temp_c = sht45_temp_c;
  inputs.sht45_rh = sht45_rh;
  inputs.bmp_temp_c = bmp_temp_c;
  inputs.bmp_pressure_bar = bmp_pressure_hPa;
  inputs.currentDiaphragmPct = diaphragmSpeedPercentage;
  inputs.currentPistonPct = pistonSpeedPercentage;
  inputs.safetyStopTriggered = safetyStopTriggered;

  ProgrammeOutputs nextOutputs = programmeOutputs;
  if (safetyStopTriggered) {
    nextOutputs.diaphragmPct = 0;
    nextOutputs.pistonPct = 0;
  }
  ACTIVE_PROGRAMME->update(inputs, nextOutputs);
  applyProgrammeOutputs(nextOutputs);
  programmeOutputs = nextOutputs;

  if (nextOutputs.finished) {
    programmeFinished = true;
    Serial.print(F(">> Programme finished: "));
    Serial.println(ACTIVE_PROGRAMME->name);
  }
}

/**
 * Clean data print showing system dynamics once a second
 */
void printSystemStatus() {
  Serial.println(F("\n--- SYSTEM STATUS REPORT ---"));
  Serial.print(F("Operating Mode    : "));
  if (currentMode == MODE_MANUAL) {
    Serial.println(F("MANUAL"));
  } else if (currentMode == MODE_CYCLIC_PISTON) {
    Serial.print(F("CYCLIC (Interval: "));
    Serial.print(cycleIntervalMs / 1000);
    Serial.print(F("s, Duration: "));
    Serial.print(cycleDurationMs / 1000);
    Serial.print(F("s, PWM: "));
    Serial.print(pistonCyclePwm);
    Serial.print(F("%, Active: "));
    Serial.print(pistonActiveDuringCycle ? "YES" : "NO");
    Serial.println(F(")"));
  } else {
    Serial.print(F("PROGRAMME ("));
    Serial.print(ACTIVE_PROGRAMME->name);
    Serial.print(F(", Finished: "));
    Serial.print(programmeFinished ? "YES" : "NO");
    Serial.println(F(")"));
  }
  Serial.print(F("Diaphragm Output : ")); Serial.print(diaphragmSpeedPercentage); Serial.println(F("%"));
  Serial.print(F("Piston Output     : ")); Serial.print(pistonSpeedPercentage); Serial.println(F("%"));
  Serial.print(F("Solenoid Valve    : ")); Serial.println(solenoidOpen ? F("OPEN") : F("CLOSED"));
  Serial.print(F("TMP1075 Temp       : ")); Serial.print(tmp1075_c, 2); Serial.println(F(" °C"));
  Serial.print(F("ABP2 Pressure      : ")); Serial.print(abp2_pressure_bar, 4); Serial.println(F(" bar"));
  Serial.print(F("ABP2 Pressure + MS5803      : ")); Serial.print(abp2_pressure_bar + ms5803_pressure_bar, 4); Serial.println(F(" bar"));
  Serial.print(F("ABP2 Temperature   : ")); Serial.print(abp2_temp_c, 2); Serial.println(F(" °C"));
  Serial.print(F("MS5803 Pressure    : ")); Serial.print(ms5803_pressure_bar, 6); Serial.println(F(" bar"));
  Serial.print(F("MS5803 Temperature : ")); Serial.print(ms5803_temp_c, 2); Serial.println(F(" °C"));
  Serial.print(F("BMP280 Pressure    : ")); Serial.print(bmp_pressure_hPa, 2); Serial.println(F(" bar"));
  Serial.print(F("BMP280 Temperature : ")); Serial.print(bmp_temp_c, 2); Serial.println(F(" °C"));
  Serial.print(F("SHT45 Temp/RH      : ")); Serial.print(sht45_temp_c, 2); Serial.print(F(" °C / ")); 
                                           Serial.print(sht45_rh, 2); Serial.println(F(" %"));
  Serial.println(F("----------------------------"));

  // Machine-readable CSV data line for host-side logging and plotting
  // Compose CSV DATA line using dtostrf for float->string on AVR
  char buf[250];
  char tmp_buf[32], abp_p_buf[32], abp_t_buf[32],abp_ms_pbuf[32], ms_p_buf[32],ms_t_buf[32], bmp_p_buf[32], bmp_t_buf[32], sht_t_buf[32], sht_h_buf[32];
  
  dtostrf(tmp1075_c, 6, 2, tmp_buf);
  dtostrf(abp2_pressure_bar, 6, 4, abp_p_buf);
  dtostrf(abp2_temp_c, 6, 2, abp_t_buf);
  dtostrf(abp2_pressure_bar + ms5803_pressure_bar, 6, 4, abp_ms_pbuf);
  dtostrf(ms5803_pressure_bar, 6, 2, ms_p_buf);
  dtostrf(ms5803_temp_c, 6, 2, ms_t_buf);
  dtostrf(bmp_pressure_hPa, 6, 2, bmp_p_buf);
  dtostrf(bmp_temp_c, 6, 2, bmp_t_buf);
  dtostrf(sht45_temp_c, 6, 2, sht_t_buf);
  dtostrf(sht45_rh, 6, 2, sht_h_buf);

snprintf(buf, sizeof(buf), "DATA,%lu,%u,%u,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s",
         (unsigned long)millis(), (unsigned int)diaphragmSpeedPercentage, 
         (unsigned int)pistonSpeedPercentage, tmp_buf, abp_t_buf,abp_p_buf,abp_ms_pbuf,ms_t_buf,ms_p_buf,bmp_t_buf, bmp_p_buf, sht_t_buf, sht_h_buf);
  Serial.println(buf);

  // (Host-side logger should capture Serial output and write to logs/)
}