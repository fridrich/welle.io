#include <cstdlib>
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
#include "charsets.h"
#include <algorithm>

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

        // Runtime check for Analog headphone jack routing
        const char* env_analog = std::getenv("DABBOARD_ANALOG");
        if (env_analog && std::string(env_analog) == "1") {
            std::clog << "Si4688Input: DABBOARD_ANALOG=1 detected. Routing audio to 3.5mm analog jack!" << std::endl;
            si468x_set_audio_output(0);
        }
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

        // Runtime check for Analog headphone jack routing
        const char* env_analog = std::getenv("DABBOARD_ANALOG");
        if (env_analog && std::string(env_analog) == "1") {
            std::clog << "Si4688Input: DABBOARD_ANALOG=1 detected. Routing audio to 3.5mm analog jack!" << std::endl;
            si468x_set_audio_output(0);
        }
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

        // Trigger default auto-play of the first service (tuning handles delays)
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

    // Query on-chip service database with retries to allow FIC sync and decode
    si468x_service_t services[32];
    int num_services = 0;

    for (int retry = 0; retry < 6; retry++) {
        std::memset(services, 0, sizeof(services));
        num_services = si468x_get_service_list(services, 32);
        if (num_services > 0) {
            break;
        }
        std::clog << "Si4688Input: Waiting for ensemble sync & FIC database decoding (retry "
                  << (retry + 1) << "/6)..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    if (num_services > 0) {
        int service_to_play = 0; // Default to first available service
        if (!name.empty()) {
            for (int s = 0; s < num_services; s++) {
                std::string s_label = toUtf8StringUsingCharset(services[s].label, CharacterSet::EbuLatin, 16);
                std::string s_short = toUtf8StringUsingCharset(services[s].short_label, CharacterSet::EbuLatin, 8);
                if (s_label.find(name) != std::string::npos ||
                    s_short.find(name) != std::string::npos) {
                    service_to_play = s;
                    break;
                }
            }
        }

        std::string clean_label = toUtf8StringUsingCharset(services[service_to_play].label, CharacterSet::EbuLatin, 16);
        clean_label.erase(std::find_if(clean_label.rbegin(), clean_label.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), clean_label.end());

        std::clog << "Si4688Input: Playing service: '" << clean_label
                  << "' (SId: 0x" << std::hex << services[service_to_play].service_id
                  << ", CompId: " << std::dec << services[service_to_play].component_id << ")" << std::endl;

        si468x_play_service(services[service_to_play].service_id, services[service_to_play].component_id);

        // Explicitly set co-processor hardware volume to 55 to un-mute the audio DAC!
        si468x_set_volume(55);
    } else {
        std::cerr << "Si4688Input: No active services found on tuned frequency!" << std::endl;
    }
}
