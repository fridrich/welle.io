/*
 *    Copyright (C) 2018
 *    Matthias P. Braendli (matthias.braendli@mpb.li)
 *    Albrecht Lohofener (albrechtloh@gmx.de)
 *
 *    This file is part of the welle.io.
 *    Many of the ideas as implemented in welle.io are derived from
 *    other work, made available through the GNU general Public License.
 *    All copyrights of the original authors are recognized.
 *
 *    welle.io is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    welle.io is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with welle.io; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#pragma once

#include <atomic>
#include <thread>
#include "CVirtualInput.h"
#include "ringbuffer.h"
#include <SoapySDR/Version.hpp>
#include <SoapySDR/Modules.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Device.hpp>

class CSoapySdr_Thread;

class CSoapySdr : public CVirtualInput
{
public:
    CSoapySdr();
    ~CSoapySdr();
    CSoapySdr(const CSoapySdr&) = delete;
    CSoapySdr operator=(const CSoapySdr&) = delete;

    virtual void setFrequency(int32_t Frequency);
    virtual bool restart(void);
    virtual void stop(void);
    virtual void reset(void);
    virtual int32_t getSamples(DSPCOMPLEX* Buffer, int32_t Size);
    virtual int32_t getSpectrumSamples(DSPCOMPLEX* Buffer, int32_t Size);
    virtual int32_t getSamplesToRead(void);
    virtual float setGain(int32_t Gain);
    virtual int32_t getGainCount(void);
    virtual void setAgc(bool AGC);
    virtual void setHwAgc(bool hwAGC);
    virtual std::string getName(void);
    virtual CDeviceID getID(void);
    virtual void setDriverArgs(const std::string& args);
    virtual void setAntenna(const std::string& antenna);
    virtual void setClockSource(const std::string& clock_source);

private:
    int32_t m_freq = 0;
    std::string m_driver_args;
    std::string m_antenna;
    std::string m_clock_source;
    SoapySDR::Device *m_device = nullptr;
    std::atomic<bool> m_running;

    RingBuffer<DSPCOMPLEX> m_sampleBuffer;
    RingBuffer<DSPCOMPLEX> m_spectrumSampleBuffer;

    std::thread m_thread;
    void workerthread(void);
    void process(SoapySDR::Stream *stream);
};
