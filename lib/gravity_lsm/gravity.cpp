#include "gravity.h"
#include <math.h>
#include "lcd_manager.h"

GravityLSM::GravityLSM(int pinSound, int pinCalib)
    : _pinSound(pinSound), _pinCalib(pinCalib)
{
}

void GravityLSM::begin()
{
    analogSetAttenuation(ADC_11db);
    analogSetPinAttenuation(_pinSound, ADC_11db);
    analogSetPinAttenuation(_pinCalib, ADC_11db);
    pinMode(_pinCalib, INPUT);
}

float GravityLSM::readDBA()
{
    float sumSq = 0.0f;
    for (int i = 0; i < kRmsSamples; i++)
    {
        float val = static_cast<float>(analogRead(_pinSound));
        sumSq += val * val;
        delayMicroseconds(125);
    }

    float rms = sqrtf(sumSq / static_cast<float>(kRmsSamples));
    float voltageValue = rms / 4095.0f * kVref;
    float dba = 50.20f * voltageValue + 10.1f;

    int sumCal = 0;
    for (int i = 0; i < 5; i++)
    {
        sumCal += analogRead(_pinCalib);
        delay(2);
    }
    float calibAdjust =
        ((sumCal / 5.0f) / 4095.0f - 0.5f) * 20.0f;
    dba += calibAdjust;

    float dbaConstrained = constrain(dba, 30.0f, 130.0f);
    return dbaConstrained;
}
