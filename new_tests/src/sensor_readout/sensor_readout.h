#ifndef SENSOR_READOUT_H
#define SENSOR_READOUT_H

#include <Arduino.h>

// I2C Addresses
#define TMP1075_ADDR 0x48
#define ABP2_ADDR    0x28
#define MS5803_ADDR  0x77
#define SHT45_ADDR   0x44

// Initialization function (call this in your setup)
void initSensors();

// Sensor Readout functions
float readTMP1075();
void readABP2(float &pressure, float &temperature);
void readMS5803(float &pressure, float &temperature);
void readSHT45(float &temperature, float &humidity);
void readBMP280(float &temperature, float &pressure);

// Optional helper to inspect MS5803 calibration data
void ms5803PrintPROM();

extern bool bmp280Present; // Global flag to indicate if BMP280 is present

#endif // SENSOR_READOUT_H