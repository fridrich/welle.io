/*
 *    Copyright (C) 2026
 *    Silicon Labs Si4688 DABBoard Input Backend for welle.io
 */

#include <iostream>
#include "si4688_input.h"

CSi4688Input::CSi4688Input(RadioControllerInterface& rc)
    : radioController(rc)
{
    std::clog << "Si4688Input: Initializing Silicon Labs Si4688 DABBoard backend..." << std::endl;
    isRunning = true;
    isDeviceOk = true;
}

CSi4688Input::~CSi4688Input()
{
    stop();
}

bool CSi4688Input::restart()
{
    stop();
    isRunning = true;
    return true;
}

bool CSi4688Input::is_ok()
{
    return isDeviceOk;
}

void CSi4688Input::stop()
{
    if (isRunning) {
        isRunning = false;
        std::clog << "Si4688Input: Stopping backend..." << std::endl;
    }
}

void CSi4688Input::reset()
{
    // Mock reset
}

int32_t CSi4688Input::getSamples(DSPCOMPLEX *buffer, int32_t size)
{
    // Return dummy silence samples so welle.io soft-DSP doesn't block or error
    for (int i = 0; i < size; i++) {
        buffer[i] = DSPCOMPLEX(0.0f, 0.0f);
    }
    return size;
}

std::vector<DSPCOMPLEX> CSi4688Input::getSpectrumSamples(int size)
{
    // Return dummy spectrum
    return std::vector<DSPCOMPLEX>(size, DSPCOMPLEX(0.0f, 0.0f));
}

int32_t CSi4688Input::getSamplesToRead()
{
    return 0;
}

void CSi4688Input::setFrequency(int freq)
{
    frequency = freq;
    std::clog << "Si4688Input: Tuning hardware to frequency: " << frequency << " Hz" << std::endl;
}

int CSi4688Input::getFrequency() const
{
    return frequency;
}

float CSi4688Input::getGain() const
{
    return 0.0f;
}

float CSi4688Input::setGain(int gain_index)
{
    (void)gain_index;
    return 0.0f;
}

int CSi4688Input::getGainCount()
{
    return 0;
}

void CSi4688Input::setAgc(bool AGC)
{
    (void)AGC;
}

std::string CSi4688Input::getDescription()
{
    return "Silicon Labs Si4688 DABBoard Hardware Receiver";
}

CDeviceID CSi4688Input::getID()
{
    return CDeviceID::DABBOARD;
}
