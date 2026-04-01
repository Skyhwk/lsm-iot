#pragma once

#include <Arduino.h>

/**
 * Pembacaan Gravity Analog Sound Level Meter (sesuai logika master.ino).
 * ADC1: pin sensor suara + pin kalibrasi (trimpot).
 */
class GravityLSM
{
public:
    explicit GravityLSM(int pinSound = 34, int pinCalib = 35);

    void begin();

    /** Satu pengukuran penuh: RMS → dBA + offset trimpot, dibatasi 30–130 dBA. */
    float readDBA();

    int pinSound() const { return _pinSound; }
    int pinCalib() const { return _pinCalib; }

private:
    int _pinSound;
    int _pinCalib;

    static constexpr float kVref = 3.3f;
    static constexpr int kRmsSamples = 600;
};
