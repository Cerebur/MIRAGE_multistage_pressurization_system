#include "MPX5500Sensor.h"

MPX5500Sensor::MPX5500Sensor(uint8_t analogPin, float vSupply)
  : _pin(analogPin), _vSupply(vSupply) {}

bool MPX5500Sensor::begin() {
  pinMode(_pin, INPUT);
  return true;
}

void MPX5500Sensor::read(MPX5500Data &data) {
  int raw = analogRead(_pin);

  // Convert ADC value → voltage
  float vOut = (raw / ADC_MAX) * _vSupply;

  // Convert voltage → pressure (kPa)
  float pressure_kPa = ((vOut / _vSupply) - 0.04f) / 0.0018f;

  data.pressure_kPa = pressure_kPa;
  data.pressure_hPa = pressure_kPa * 10.0f;  // 1 kPa = 10 hPa
  data.raw = vOut;
}