#ifndef PROGRAMME_FRAMEWORK_H
#define PROGRAMME_FRAMEWORK_H

#include <Arduino.h>

struct ProgrammeInputs {
  unsigned long nowMs;
  float tmp1075_c;
  float abp2_pressure_bar;
  float abp2_temp_c;
  float ms5803_pressure_bar;
  float ms5803_temp_c;
  float sht45_temp_c;
  float sht45_rh;
  float bmp_temp_c;
  float bmp_pressure_bar;
  uint8_t currentDiaphragmPct;
  uint8_t currentPistonPct;
  bool safetyStopTriggered;
};

struct ProgrammeOutputs {
  uint8_t diaphragmPct;
  uint8_t pistonPct;
  bool solenoidOpen;
  bool finished;
};

struct ProgrammeDefinition {
  const char *name;
  bool advanceOnSafetyStop;
  void (*begin)();
  void (*update)(const ProgrammeInputs &inputs, ProgrammeOutputs &outputs);
  void (*end)();
};

#endif