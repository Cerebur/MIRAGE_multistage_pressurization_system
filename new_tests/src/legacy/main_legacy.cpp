#include <Arduino.h>
#include <Wire.h>

// -------------------- Addresses --------------------

#define TMP1075_ADDR 0x48
#define ABP2_ADDR    0x28
#define MS5803_ADDR  0x77
#define SHT45_ADDR   0x44


// -------------------- MS5803 --------------------

uint16_t ms5803_coeff[8];

void ms5803Reset()
{
    Wire.beginTransmission(MS5803_ADDR);
    Wire.write(0x1E);
    Wire.endTransmission();

    delay(10);
}


uint16_t ms5803ReadPROM(uint8_t index)
{
    Wire.beginTransmission(MS5803_ADDR);
    Wire.write(0xA0 + index * 2);
    Wire.endTransmission();

    Wire.requestFrom(MS5803_ADDR, 2);

    return ((uint16_t)Wire.read() << 8) | Wire.read();
}

void ms5803PrintPROM()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        Serial.print("C");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(ms5803_coeff[i]);
    }
}


uint32_t ms5803ReadADC()
{
    Wire.beginTransmission(MS5803_ADDR);
    Wire.write(0x00);       // ADC read command
    Wire.endTransmission();

    Wire.requestFrom(MS5803_ADDR, (uint8_t)3);

    if (Wire.available() != 3)
    {
        Serial.println("MS5803 ADC read failed");
        return 0;
    }

    return ((uint32_t)Wire.read() << 16) |
           ((uint32_t)Wire.read() << 8) |
            Wire.read();
}

void readMS5803(float &pressure, float &temperature)
{
    // D1 pressure conversion
    Wire.beginTransmission(MS5803_ADDR);
    Wire.write(0x48);       // OSR=4096 pressure
    Wire.endTransmission();

    delay(10);

    uint32_t D1 = ms5803ReadADC();


    // D2 temperature conversion
    Wire.beginTransmission(MS5803_ADDR);
    Wire.write(0x58);       // OSR=4096 temperature
    Wire.endTransmission();

    delay(10);

    uint32_t D2 = ms5803ReadADC();


    int32_t dT = D2 - ((uint32_t)ms5803_coeff[5] << 8);

    int64_t TEMP =
        2000 +
        ((int64_t)dT * ms5803_coeff[6]) / 8388608;


    int64_t OFF =
        ((int64_t)ms5803_coeff[2] << 16) +
        ((int64_t)ms5803_coeff[4] * dT) / 128;

    int64_t SENS =
        ((int64_t)ms5803_coeff[1] << 15) +
        ((int64_t)ms5803_coeff[3] * dT) / 256;


    int64_t P =
        (((D1 * SENS) / 2097152 - OFF) / 32768);

 
    // Second order compensation
    int64_t T2 = 0;
    int64_t OFF2 = 0;
    int64_t SENS2 = 0;

    if (TEMP < 2000)
    {
        T2 = (((int64_t)dT * dT)) / (1LL << 31);

        OFF2 = 3 * ((TEMP - 2000) * (TEMP - 2000));

        SENS2 = 7 * ((TEMP - 2000) * (TEMP - 2000)) / 8;
        if (TEMP < -1500)
        {
            SENS2 += 2 * ((TEMP + 1500) * (TEMP + 1500));
        }
    }
    else if (TEMP >= 4500)
    {
        SENS2 = SENS2 - ((TEMP-4500)*(TEMP-4500))/8;
    }

    TEMP -= T2;
    OFF -= OFF2;
    SENS -= SENS2;

    P = (((D1 * SENS) / 2097152 - OFF) / 32768);


    temperature = TEMP / 100.0;
    pressure = P;
}


// -------------------- TMP1075 --------------------

float readTMP1075()
{
    Wire.requestFrom(TMP1075_ADDR, 2);

    int16_t raw =
        ((int16_t)Wire.read() << 8) |
         Wire.read();

    return raw / 256.0;
}


// -------------------- ABP2 --------------------
void readABP2(float &pressure, float &temperature)
{
    // Trigger measurement
    Wire.beginTransmission(ABP2_ADDR);
    Wire.write(0xAA);
    Wire.write(0x00);
    Wire.write(0x00);

    if (Wire.endTransmission() != 0)
    {
        Serial.println("ABP2 command failed");
        return;
    }

    delay(10);


    Wire.requestFrom(ABP2_ADDR, (uint8_t)7);

    if (Wire.available() != 7)
    {
        Serial.println("ABP2 read failed");
        return;
    }


    uint8_t status = Wire.read();

    uint32_t pressure_counts =
        ((uint32_t)Wire.read() << 16) |
        ((uint32_t)Wire.read() << 8) |
         Wire.read();

    uint32_t temperature_counts =
        ((uint32_t)Wire.read() << 16) |
        ((uint32_t)Wire.read() << 8) |
         Wire.read();


    Serial.print("ABP status: 0x");
    Serial.println(status, HEX);

    Serial.print("Pressure counts: ");
    Serial.println(pressure_counts);

    Serial.print("Temperature counts: ");
    Serial.println(temperature_counts);


    // Datasheet constants
    const float OUTPUT_MIN = 1677722.0f;
    const float OUTPUT_MAX = 15099494.0f;

    const float PMIN = 0.0f;
    const float PMAX = 30.0f;   // psi


    // Pressure calculation
    float pressure_psi =
        ((pressure_counts - OUTPUT_MIN) *
        (PMAX - PMIN) /
        (OUTPUT_MAX - OUTPUT_MIN))
        + PMIN;

    // psi -> bar
    pressure = pressure_psi * 0.0689476f;


    // Temperature calculation
    temperature =
        (temperature_counts * 200.0f / 16777215.0f)
        - 50.0f;
}

// -------------------- SHT45 --------------------

uint8_t crc8(uint8_t *data)
{
    uint8_t crc = 0xFF;

    for (uint8_t i=0;i<2;i++)
    {
        crc ^= data[i];

        for(uint8_t b=0;b<8;b++)
            crc = (crc & 0x80) ? (crc<<1)^0x31 : crc<<1;
    }

    return crc;
}


void readSHT45(float &temperature, float &humidity)
{
    Wire.beginTransmission(SHT45_ADDR);
    Wire.write(0xFD);       // high precision measurement
    Wire.endTransmission();

    delay(10);

    uint8_t data[6];

    Wire.requestFrom(SHT45_ADDR,6);

    for(int i=0;i<6;i++)
        data[i]=Wire.read();


    if(crc8(data)!=data[2] ||
       crc8(data+3)!=data[5])
    {
        Serial.println("SHT45 CRC error");
        return;
    }


    uint16_t rawT =
        ((uint16_t)data[0]<<8)|data[1];

    uint16_t rawRH =
        ((uint16_t)data[3]<<8)|data[4];


    temperature =
        -45.0 +
        175.0 * rawT / 65535.0;


    humidity =
        100.0 *
        rawRH / 65535.0;
}


// -------------------- Setup --------------------

void setup()
{
    Serial.begin(115200);

    Wire.begin();

    Serial.println("Starting sensors...");

    ms5803Reset();

    for(uint8_t i=0;i<8;i++)
        ms5803_coeff[i]=ms5803ReadPROM(i);

    Serial.println("Ready");
}


// -------------------- Loop --------------------

void loop()
{
    float tmp;
    float abpP, abpT;
    float msP, msT;
    float shtT, shtRH;


    tmp = readTMP1075();

    readABP2(abpP, abpT);

    readMS5803(msP, msT);

    readSHT45(shtT, shtRH);


    Serial.println("----------------");

    Serial.print("TMP1075: ");
    Serial.print(tmp);
    Serial.println(" C");


    Serial.print("ABP2: ");
    Serial.print(abpP, 6);
    Serial.print(" mbar, ");
    Serial.print(abpT);
    Serial.println(" C");


    Serial.print("MS5803: ");
    Serial.print(msP);
    Serial.print(" mbar, ");
    Serial.print(msT);
    Serial.println(" C");


    Serial.print("SHT45: ");
    Serial.print(shtT);
    Serial.print(" C, ");
    Serial.print(shtRH);
    Serial.println(" %RH");


    delay(1000);
}