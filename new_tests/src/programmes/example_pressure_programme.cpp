#include "example_pressure_programme.h"

namespace {

enum ProgrammePhase {
  PHASE_PRESSURISING,
  PHASE_WAIT_FOR_DROP,
  PHASE_COMPLETE
};

constexpr float CRITICAL_PRESSURE_CHAMBER_BAR = 2.0f;
constexpr float CRITICAL_PRESSURE_INTERSTAGE_BAR = 1.7f;
constexpr float RESTART_PRESSURE_BAR = 0.1f;
constexpr float STABILITY_DELTA_BAR = 0.01f;
constexpr unsigned long STABILITY_WINDOW_MS = 5000;
constexpr float MAX_TEMPERATURE_C = 70.0f;
constexpr bool ADVANCE_ON_SAFETY_STOP = true;

ProgrammePhase phase = PHASE_PRESSURISING;
uint8_t currentDiaphragmPct = 20;
uint8_t currentPistonPct = 30;
float lastPressureBar = 0.0f;
unsigned long pressureStableSinceMs = 0;
bool completionLogged = false;
bool waitingForSafetyDrop = false;

void logStage() {
  Serial.print(F(">> Programme step: piston "));
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
  outputs.solenoidOpen = true;
  outputs.finished = false;
  pressureStableSinceMs = nowMs;
  lastPressureBar = pressureBar;
  logStage();
}

void advanceToNextStage(const ProgrammeInputs &inputs, ProgrammeOutputs &outputs) {
  if (currentDiaphragmPct < 100) {
    currentDiaphragmPct = static_cast<uint8_t>(currentDiaphragmPct + 10);
  } else if (currentPistonPct > 10) {
    currentPistonPct = static_cast<uint8_t>(currentPistonPct - 10);
    currentDiaphragmPct = 20;
  } else {
    phase = PHASE_COMPLETE;
    outputs.diaphragmPct = 0;
    outputs.pistonPct = 0;
    outputs.solenoidOpen = true; 
    outputs.finished = true;
    Serial.println(F(">> Programme complete."));
    return;
  }

  startPressurising(outputs, inputs.nowMs, inputs.abp2_pressure_bar);
}

void enterSafetyHold(const ProgrammeInputs &inputs, ProgrammeOutputs &outputs) {
  phase = PHASE_WAIT_FOR_DROP;
  waitingForSafetyDrop = true;
  outputs.diaphragmPct = 0;
  outputs.pistonPct = 0;
  outputs.solenoidOpen = true;
  outputs.finished = false;
  pressureStableSinceMs = inputs.nowMs;
  lastPressureBar = inputs.abp2_pressure_bar;
  Serial.println(F(">> Programme: safety hold."));
}

}  // namespace

static void exampleBegin() {
  phase = PHASE_PRESSURISING;
  currentDiaphragmPct = 20;
  currentPistonPct = 30;
  lastPressureBar = 0.0f;
  pressureStableSinceMs = 0;
  completionLogged = false;
  waitingForSafetyDrop = false;
  Serial.println(F(">> Programme loaded: pressure vacuum"));
}

static void exampleUpdate(const ProgrammeInputs &inputs, ProgrammeOutputs &outputs) {
  if (phase == PHASE_COMPLETE) {
    outputs.diaphragmPct = 0;
    outputs.pistonPct = 0;
    outputs.solenoidOpen = true;
    outputs.finished = true;
    if (!completionLogged) {
      Serial.println(F(">> Programme idle."));
      completionLogged = true;
    }
    return;
  }

  if (inputs.abp2_temp_c > MAX_TEMPERATURE_C || inputs.tmp1075_c > MAX_TEMPERATURE_C) {
    phase = PHASE_COMPLETE;
    outputs.diaphragmPct = 0;
    outputs.pistonPct = 0;
    outputs.solenoidOpen = true;
    outputs.finished = true;
    Serial.println(F(">> Programme stopped: temperature limit."));
    return;
  }

  if (inputs.safetyStopTriggered && ADVANCE_ON_SAFETY_STOP && outputs.diaphragmPct == 0 && outputs.pistonPct == 0) {
    enterSafetyHold(inputs, outputs);
    return;
  }

  if (phase == PHASE_PRESSURISING) {
    outputs.diaphragmPct = currentDiaphragmPct;
    outputs.pistonPct = currentPistonPct;
    outputs.solenoidOpen = true;
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
        outputs.solenoidOpen = true;
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
      outputs.solenoidOpen = true;
      outputs.finished = false;
    }

    lastPressureBar = inputs.abp2_pressure_bar;
    return;
  }

  if (phase == PHASE_WAIT_FOR_DROP) {
    outputs.diaphragmPct = 0;
    outputs.pistonPct = 0;
    outputs.solenoidOpen = true;
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

static void exampleEnd() {
  phase = PHASE_COMPLETE;
  completionLogged = false;
  waitingForSafetyDrop = false;
}

const ProgrammeDefinition EXAMPLE_PRESSURE_PROGRAMME = {
    "Pressure staircase",
    true,
    exampleBegin,
    exampleUpdate,
    exampleEnd,
};