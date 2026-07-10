#pragma once
#include <Arduino.h>

struct MPX5500Data {
  float pressure_kPa;   // Pressure in kPa
  float pressure_hPa;   // Pressure in hPa
  float raw;           // Raw voltage reading (for debugging)
};

class MPX5500Sensor {
public:
  // analogPin: Arduino analog input pin
  // vSupply: supply voltage (default 5.0V)
  MPX5500Sensor(uint8_t analogPin, float vSupply = 5.0f);

  bool begin();  // always true unless you want to add checks

  void read(MPX5500Data &data);

private:
  uint8_t _pin;
  float   _vSupply;

  // ADC resolution (10-bit Arduino = 1023)
  static constexpr float ADC_MAX = 1023.0f;
};