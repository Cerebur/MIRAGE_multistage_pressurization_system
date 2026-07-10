#include "vacuum_pressure_programme.h"

namespace {

enum ProgrammePhase {
  PHASE_PRESSURISING,
  PHASE_WAIT_FOR_DROP,
  PHASE_TEMPERATURE_HOLD,
  PHASE_COMPLETE
};

constexpr float CRITICAL_PRESSURE_CHAMBER_BAR = 2.0f;
constexpr float CRITICAL_PRESSURE_INTERSTAGE_BAR = 1.7f;
constexpr float RESTART_PRESSURE_BAR = 0.1f;
constexpr float STABILITY_DELTA_BAR = 0.0001f;
constexpr unsigned long STABILITY_WINDOW_MS = 10000;
constexpr float MAX_TEMPERATURE_C = 60.0f;
constexpr float MAX_TEMPERATURE_RESTART_C = 50.0f;
constexpr bool ADVANCE_ON_SAFETY_STOP = true;

ProgrammePhase phase = PHASE_PRESSURISING;
uint8_t currentDiaphragmPct = 100;
uint8_t currentPistonPct = 100;
float lastPressureBar = 0.0f;
unsigned long pressureStableSinceMs = 0;
bool completionLogged = false;
bool waitingForSafetyDrop = false;
bool waitingForTemperatureDrop = false;

void logStage() {
  Serial.print(F(">> piston "));
  Serial.print(currentPistonPct);
  Serial.print(F("%, diaphragm "));
  Serial.print(currentDiaphragmPct);
  Serial.println(F("%"));
}

void startPressurising(ProgrammeOutputs &outputs, unsigned long nowMs, float pressureBar) {
  phase = PHASE_PRESSURISING;
  waitingForSafetyDrop = false;
  outputs.diaphragmPct = currentDiaphragmPct;
  outputs.pistonPct = currentPistonPct;
  outputs.solenoidOpen = false;
  outputs.finished = false;
  pressureStableSinceMs = nowMs;
  lastPressureBar = pressureBar;
  logStage();
}

void advanceToNextStage(const ProgrammeInputs &inputs, ProgrammeOutputs &outputs) {
    phase = PHASE_COMPLETE;
    outputs.diaphragmPct = 0;
    outputs.pistonPct = 0;
    outputs.solenoidOpen = false; 
    outputs.finished = true;
    Serial.println(F(">> Programme complete."));
    return;

    //startPressurising(outputs, inputs.nowMs, inputs.abp2_pressure_bar);
}

void enterSafetyHold(const ProgrammeInputs &inputs, ProgrammeOutputs &outputs) {
  phase = PHASE_WAIT_FOR_DROP;
  waitingForSafetyDrop = true;
  waitingForTemperatureDrop = false;
  outputs.diaphragmPct = 0;
  outputs.pistonPct = 0;
  outputs.solenoidOpen = false;
  outputs.finished = false;
  pressureStableSinceMs = inputs.nowMs;
  lastPressureBar = inputs.abp2_pressure_bar;
  Serial.println(F(">> Programme: safety hold."));
}

void enterTemperatureHold(const ProgrammeInputs &inputs, ProgrammeOutputs &outputs) {
  phase = PHASE_TEMPERATURE_HOLD;
  waitingForSafetyDrop = false;
  waitingForTemperatureDrop = true;
  outputs.diaphragmPct = 0;
  outputs.pistonPct = 0;
  outputs.solenoidOpen = false;
  outputs.finished = false;
  pressureStableSinceMs = inputs.nowMs;
  lastPressureBar = inputs.abp2_pressure_bar;
  Serial.println(F(">> Programme: temperature hold."));
}

}  // namespace

static void vacuumBegin() {
  phase = PHASE_PRESSURISING;
  currentDiaphragmPct = 100;
  currentPistonPct = 100;
  lastPressureBar = 0.0f;
  pressureStableSinceMs = 0;
  completionLogged = false;
  waitingForSafetyDrop = false;
  waitingForTemperatureDrop = false;
  Serial.println(F(">> Programme loaded: pressure vacuum"));
}

static void vacuumUpdate(const ProgrammeInputs &inputs, ProgrammeOutputs &outputs) {
  if (phase == PHASE_COMPLETE) {
    outputs.diaphragmPct = 0;
    outputs.pistonPct = 0;
    outputs.solenoidOpen = false;
    outputs.finished = true;
    if (!completionLogged) {
      Serial.println(F(">> Programme idle."));
      completionLogged = true;
    }
    return;
  }

  if (inputs.sht45_temp_c > MAX_TEMPERATURE_C || inputs.tmp1075_c > MAX_TEMPERATURE_C) {
    if (phase != PHASE_TEMPERATURE_HOLD) {
      enterTemperatureHold(inputs, outputs);
    } else {
      outputs.diaphragmPct = 0;
      outputs.pistonPct = 0;
      outputs.solenoidOpen = false;
      outputs.finished = false;
    }
    return;
  }

  if (phase == PHASE_TEMPERATURE_HOLD) {
    outputs.diaphragmPct = 0;
    outputs.pistonPct = 0;
    outputs.solenoidOpen = false;
    outputs.finished = false;

    if (inputs.sht45_temp_c <= MAX_TEMPERATURE_RESTART_C && inputs.tmp1075_c <= MAX_TEMPERATURE_RESTART_C) {
      Serial.println(F(">> Programme: temperature recovered."));
      phase = PHASE_PRESSURISING;
      waitingForTemperatureDrop = false;
      pressureStableSinceMs = inputs.nowMs;
      lastPressureBar = inputs.abp2_pressure_bar;
    }
    return;
  }

  if (inputs.safetyStopTriggered && ADVANCE_ON_SAFETY_STOP && outputs.diaphragmPct == 0 && outputs.pistonPct == 0) {
    enterSafetyHold(inputs, outputs);
    return;
  }

  if (phase == PHASE_PRESSURISING) {
    outputs.diaphragmPct = currentDiaphragmPct;
    outputs.pistonPct = currentPistonPct;
    outputs.solenoidOpen = false;
    outputs.finished = false;

    float pressureDelta = inputs.abp2_pressure_bar - lastPressureBar;
    if (pressureDelta < 0.0f) {
      pressureDelta = -pressureDelta;
    }

    if (pressureStableSinceMs == 0) {
      pressureStableSinceMs = inputs.nowMs;
    }

    if (pressureDelta <= STABILITY_DELTA_BAR) {
      if (inputs.nowMs - pressureStableSinceMs >= STABILITY_WINDOW_MS) {
        Serial.println(F(">> Programme: stable pressure."));
        phase = PHASE_WAIT_FOR_DROP;
        outputs.diaphragmPct = 0;
        outputs.pistonPct = 0;
        outputs.solenoidOpen = false;
        outputs.finished = false;
      }
    } else {
      pressureStableSinceMs = inputs.nowMs;
    }

    if (inputs.abp2_pressure_bar >= CRITICAL_PRESSURE_CHAMBER_BAR || inputs.bmp_pressure_bar >= CRITICAL_PRESSURE_INTERSTAGE_BAR) {
      Serial.println(F(">> Programme: critical pressure."));
      phase = PHASE_WAIT_FOR_DROP;
      outputs.diaphragmPct = 0;
      outputs.pistonPct = 0;
      outputs.solenoidOpen = false;
      outputs.finished = false;
    }

    lastPressureBar = inputs.abp2_pressure_bar;
    return;
  }

  if (phase == PHASE_WAIT_FOR_DROP) {
    outputs.diaphragmPct = 0;
    outputs.pistonPct = 0;
    outputs.solenoidOpen = false;
    outputs.finished = false;

    if (inputs.abp2_pressure_bar <= RESTART_PRESSURE_BAR) {
      if (waitingForSafetyDrop) {
        Serial.println(F(">> Programme: safety drop."));
      } else {
        Serial.println(F(">> Programme: pressure dropped."));
      }
      advanceToNextStage(inputs, outputs);
    }
  }
}

static void vacuumEnd() {
  phase = PHASE_COMPLETE;
  completionLogged = false;
  waitingForSafetyDrop = false;
  waitingForTemperatureDrop = false;
}

const ProgrammeDefinition VACUUM_PRESSURE_PROGRAMME = {
    "Pressure vacuum",
    true,
    vacuumBegin,
    vacuumUpdate,
    vacuumEnd,
};