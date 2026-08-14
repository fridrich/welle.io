#include <thread>
#include <chrono>
#include <cstring>
/*
 *    Copyright (C) 2026
 *    Silicon Labs Si4688 DABBoard Input Backend for welle.io
 */

#include <iostream>
#include <si468x.h>
#include "si4688_input.h"

CSi4688Input::CSi4688Input(RadioControllerInterface& rc)
    : radioController(rc)
{
    std::clog << "Si4688Input: Connecting to libsi468x..." << std::endl;

    // Initialize physical board via libsi468x C-API
    int ret = si468x_init("/dev/spidev0.0", 23, SI468X_BOOT_DAB);
    if (ret == SI468X_SUCCESS) {
        std::clog << "Si4688Input: Successfully initialized hardware board!" << std::endl;
        isDeviceOk = true;
        isRunning = true;
    } else {
        std::cerr << "Si4688Input: Hardware board initialization failed (code: " << ret << ")" << std::endl;
        isDeviceOk = false;
        isRunning = false;
    }
}

CSi4688Input::~CSi4688Input()
{
    stop();
}

bool CSi4688Input::restart()
{
    stop();

    int ret = si468x_init("/dev/spidev0.0", 23, SI468X_BOOT_DAB);
    if (ret == SI468X_SUCCESS) {
        isDeviceOk = true;
        isRunning = true;
        return true;
    }
    return false;
}

bool CSi4688Input::is_ok()
{
    return isDeviceOk;
}

void CSi4688Input::stop()
{
    if (isRunning) {
        isRunning = false;
        std::clog << "Si4688Input: Releasing hardware..." << std::endl;
        si468x_shutdown();
    }
}

void CSi4688Input::reset()
{
    restart();
}

int32_t CSi4688Input::getSamples(DSPCOMPLEX *buffer, int32_t size)
{
    // Return dummy silence samples so welle.io soft-DSP doesn't block or error
    for (int i = 0; i < size; i++) {
        buffer[i] = DSPCOMPLEX(0.0f, 0.0f);
    }

    // Periodically poll the chip's live signal quality (RSSI, SNR)
    // and invoke welle.io's onSNR callback to dynamically update GUI and CLI signal meters!
    if (isDeviceOk) {
        static int status_poll_cnt = 0;
        if (status_poll_cnt++ % 8 == 0) { // Poll periodically to avoid bus saturation
            si468x_signal_status_t sig_status;
            if (si468x_get_signal_status(&sig_status) == SI468X_SUCCESS) {
                // Pass real hardware SNR directly to welle.io controller
                radioController.onSNR(sig_status.snr);
            }
        }
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
    if (isDeviceOk) {
        si468x_set_frequency(frequency);

        // Wait for tuner lock and trigger default auto-play of the first service
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        playService("");
    }
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
    return "Silicon Labs Si4688 DABBoard Hardware Receiver via libsi468x";
}

CDeviceID CSi4688Input::getID()
{
    return CDeviceID::DABBOARD;
}


void CSi4688Input::playService(const std::string& name)
{
    if (!isDeviceOk) return;

    // Query on-chip service database
    si468x_service_t services[32];
    std::memset(services, 0, sizeof(services));
    int num_services = si468x_get_service_list(services, 32);

    if (num_services > 0) {
        int service_to_play = 0; // Default to first available service
        if (!name.empty()) {
            for (int s = 0; s < num_services; s++) {
                if (std::string(services[s].label) == name ||
                    std::string(services[s].short_label) == name) {
                    service_to_play = s;
                    break;
                }
            }
        }

        std::clog << "Si4688Input: Playing service: '" << services[service_to_play].label
                  << "' (SId: 0x" << std::hex << services[service_to_play].service_id
                  << ", CompId: " << std::dec << services[service_to_play].component_id << ")" << std::endl;

        si468x_play_service(services[service_to_play].service_id, services[service_to_play].component_id);
    } else {
        std::cerr << "Si4688Input: No active services found on tuned frequency!" << std::endl;
    }
}
