/*
 *    Copyright (C) 2026
 *    Silicon Labs Si4688 DABBoard Input Backend for welle.io
 */

#include <iostream>
#include "si4688_input.h"

#if defined(HAVE_ALSA) && !defined(QT_CORE_LIB)
#include <alsa/asoundlib.h>
#endif

CSi4688Input::CSi4688Input(RadioControllerInterface& rc)
#ifdef QT_CORE_LIB
    : QObject(),
      radioController(rc)
#else
    : radioController(rc)
#endif
{
    std::clog << "Si4688Input: Initializing Silicon Labs Si4688 DABBoard backend..." << std::endl;
    
    isRunning = true;
    isDeviceOk = true;

#if defined(HAVE_ALSA) && !defined(QT_CORE_LIB)
    // If not in Qt (e.g. welle-cli/welle-pi), start ALSA thread
    alsaCaptureThread = std::thread(&CSi4688Input::alsaCaptureLoop, this);
#endif

#ifdef QT_CORE_LIB
    // If in Qt GUI, start Qt Multimedia capture
    startQtCapture();
#endif
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
        
#if defined(HAVE_ALSA) && !defined(QT_CORE_LIB)
        if (alsaCaptureThread.joinable()) {
            alsaCaptureThread.join();
        }
#endif

#ifdef QT_CORE_LIB
        stopQtCapture();
#endif
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

#if defined(HAVE_ALSA) && !defined(QT_CORE_LIB)
void CSi4688Input::alsaCaptureLoop()
{
    std::clog << "Si4688Input: Starting native ALSA capture-to-playback loop..." << std::endl;
    snd_pcm_t *capture_handle = nullptr;
    snd_pcm_t *playback_handle = nullptr;
    
    // 1. Open I2S capture card
    if (snd_pcm_open(&capture_handle, "hw:ugreendabboard", SND_PCM_STREAM_CAPTURE, 0) < 0) {
        std::cerr << "Si4688Input ALSA: Could not open capture device 'hw:ugreendabboard'" << std::endl;
        return;
    }
    
    // 2. Open default playback device
    if (snd_pcm_open(&playback_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "Si4688Input ALSA: Could not open default playback device" << std::endl;
        snd_pcm_close(capture_handle);
        return;
    }
    
    // 3. Configure Capture HW parameters
    snd_pcm_hw_params_t *c_params;
    snd_pcm_hw_params_alloca(&c_params);
    snd_pcm_hw_params_any(capture_handle, c_params);
    snd_pcm_hw_params_set_access(capture_handle, c_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_handle, c_params, SND_PCM_FORMAT_S16_LE);
    unsigned int rate = 48000;
    snd_pcm_hw_params_set_rate_near(capture_handle, c_params, &rate, 0);
    snd_pcm_hw_params_set_channels(capture_handle, c_params, 2);
    snd_pcm_hw_params(capture_handle, c_params);
    snd_pcm_prepare(capture_handle);
    
    // 4. Configure Playback HW parameters (match capture)
    snd_pcm_hw_params_t *p_params;
    snd_pcm_hw_params_alloca(&p_params);
    snd_pcm_hw_params_any(playback_handle, p_params);
    snd_pcm_hw_params_set_access(playback_handle, p_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playback_handle, p_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(playback_handle, p_params, &rate, 0);
    snd_pcm_hw_params_set_channels(playback_handle, p_params, 2);
    snd_pcm_hw_params(playback_handle, p_params);
    snd_pcm_prepare(playback_handle);
    
    const int frames_to_read = 512;
    std::vector<int16_t> temp_buf(frames_to_read * 2);
    
    while (isRunning) {
        int read_result = snd_pcm_readi(capture_handle, temp_buf.data(), frames_to_read);
        if (read_result > 0) {
            int write_result = snd_pcm_writei(playback_handle, temp_buf.data(), read_result);
            if (write_result == -EPIPE) {
                snd_pcm_prepare(playback_handle);
            }
        } else if (read_result == -EPIPE) {
            snd_pcm_prepare(capture_handle);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    
    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);
    std::clog << "Si4688Input: ALSA loopback finished." << std::endl;
}
#endif

#ifdef QT_CORE_LIB
void CSi4688Input::startQtCapture()
{
    std::clog << "Si4688Input: Starting Qt Multimedia audio loopback..." << std::endl;
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);

    // Capture device
    QAudioDevice targetDevice;
    const auto devices = QMediaDevices::audioInputs();
    for (const auto &device : devices) {
        if (device.description().contains("ugreen-dabboard") || 
            device.description().contains("DABBoard")) {
            targetDevice = device;
            break;
        }
    }
    if (targetDevice.isNull()) {
        targetDevice = QMediaDevices::defaultAudioInput();
    }

    // Playback device
    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();

    qtAudioSource = new QAudioSource(targetDevice, format, this);
    qtAudioSink = new QAudioSink(outputDevice, format, this);

    qtAudioDevice = qtAudioSource->start();
    qtPlaybackDevice = qtAudioSink->start();

    connect(qtAudioDevice, &QIODevice::readyRead, this, [this]() {
        QByteArray pcmData = qtAudioDevice->readAll();
        if (qtPlaybackDevice) {
            qtPlaybackDevice->write(pcmData);
        }
    });
}

void CSi4688Input::stopQtCapture()
{
    if (qtAudioSource) {
        std::clog << "Si4688Input: Stopping Qt Multimedia loopback..." << std::endl;
        qtAudioSource->stop();
        delete qtAudioSource;
        qtAudioSource = nullptr;
    }
    if (qtAudioSink) {
        qtAudioSink->stop();
        delete qtAudioSink;
        qtAudioSink = nullptr;
    }
}
#endif
