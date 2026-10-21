/*
 *    Copyright (C) 2026
 *    Silicon Labs Si4688 DABBoard Input Backend for welle.io
 */

#ifndef CSI4688INPUT_H
#define CSI4688INPUT_H

#include <vector>
#include <string>
#include <atomic>
#include "virtual_input.h"
#include "dab-constants.h"
#include "radio-controller.h"
#include "MathHelper.h"

class CSi4688Input : public CVirtualInput
{
public:
    CSi4688Input(RadioControllerInterface& radioController);
    ~CSi4688Input(void);

    // Interface methods
    bool restart(void) override;
    bool is_ok(void) override;
    void stop(void) override;
    void reset(void) override;
    int32_t getSamples(DSPCOMPLEX *buffer, int32_t size) override;
    std::vector<DSPCOMPLEX> getSpectrumSamples(int size) override;
    int32_t getSamplesToRead(void) override;
    void setFrequency(int Frequency) override;
    int getFrequency(void) const override;
    float getGain(void) const override;
    float setGain(int gain_index) override;
    int getGainCount(void) override;
    void setAgc(bool AGC) override;
    std::string getDescription(void) override;

    CDeviceID getID(void) override;

    // Frontend-agnostic hardware playback control
    void playService(const std::string& name);

private:
    RadioControllerInterface& radioController;
    int frequency = kHz(174928);
    std::atomic<bool> isDeviceOk{false};
    std::atomic<bool> isRunning{false};
};

#endif // CSI4688INPUT_H
