/*
 *    Copyright (C) 2018
 *    Matthias P. Braendli (matthias.braendli@mpb.li)
 *
 *    Copyright (C) 2017
 *    Albrecht Lohofener (albrechtloh@gmx.de)
 *
 *    This file is based on SDR-J
 *    Copyright (C) 2010, 2011, 2012
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
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

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <queue>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <set>
#include <utility>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef HAVE_SOAPYSDR
#  include "soapy_sdr.h"
#endif
#include "rtl_tcp.h"
#include "welle-cli/alsa-output.h"
#include "backend/ensemble_wait.h"
#include <liblcd/liblcd.h>
#include "backend/radio-receiver.h"
#include "input/input_factory.h"
#include "input/raw_file.h"
#include "various/channels.h"
#include "libs/json.hpp"
#include "input_queue.hpp"
#include "config_manager.hpp"
#include "ui_screen.hpp"
#include "screens.hpp"
#include "ui_manager.hpp"
#include "utils.hpp"

#ifdef GITDESCRIBE
#define VERSION GITDESCRIBE
#else
#define VERSION "unknown"
#endif

using namespace std;

/* Set by the signal handler, polled by all the loops that would otherwise
 * run forever. */
static volatile sig_atomic_t quit_requested = 0;

/* The input device failed and the receiver stopped. welle-pi cannot do
 * anything about it, it has to terminate with an error so that whoever
 * started it can restart it. */
static atomic<bool> input_failure(false);

static bool stop_requested()
{
    return quit_requested or input_failure;
}

/* The ensemble was reconfigured, or the IQ file was rewound. The subchannel
 * we are decoding may have moved, so the programme has to be selected again.
 * This is set from the FIB processor thread and acted upon by the main
 * thread. */
static atomic<bool> retune_requested(false);

/* The receiver is tuned but no audio arrived for that long: the subchannel we
 * are decoding is probably not carrying our programme any more. */
static const uint64_t AUDIO_TIMEOUT_MS = 30000;

/* No OFDM sync for that long: the demodulator is lost, restarting it makes it
 * search for the frequency offset again. */
static const uint64_t SYNC_TIMEOUT_MS = 120000;

/* Reception errors are counted and reported at most that often. */
static const uint64_t ERROR_REPORT_MS = 10000;

/* Delay between two attempts at opening the sound card. */
static const uint64_t AO_RETRY_MS = 5000;

/* The frequency correction is reported at most that often, and only when it
 * moved by at least that much. */
static const uint64_t CORRECTOR_REPORT_MS = 60000;
static const int CORRECTOR_REPORT_HZ = 100;



#define HIGH 1
#define LOW 0
#define PIN_NEXT 23
#define PIN_PREV 24

static int readPin(int pin) {
    (void)pin;
    return LOW;
}

static void runGPIOPolling(InputQueue& queue) {
    while (!stop_requested()) {
        if (readPin(PIN_NEXT) == HIGH) {
            queue.push(InputAction::DOWN);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (readPin(PIN_PREV) == HIGH) {
            queue.push(InputAction::UP);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static void runStdinReader(InputQueue& queue) {
    while (!stop_requested()) {
        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;

        int r = poll(&pfd, 1, 500);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) continue;

        string line;
        if (!getline(cin, line)) break;
        if (line == ".") {
            queue.push(InputAction::QUIT);
            break;
        }
        if (line == "u" || line == "up") {
            queue.push(InputAction::UP);
        } else if (line == "d" || line == "down") {
            queue.push(InputAction::DOWN);
        } else if (line == "l" || line == "left") {
            queue.push(InputAction::LEFT);
        } else if (line == "r" || line == "right") {
            queue.push(InputAction::RIGHT);
        } else if (line == "e" || line == "enter") {
            queue.push(InputAction::ENTER);
        } else if (line == "m" || line == "menu") {
            queue.push(InputAction::MENU);
        } else if (line == "q" || line == "quit") {
            queue.push(InputAction::QUIT);
        }
    }
}

class AlsaProgrammeHandler: public ProgrammeHandlerInterface {
    public:
        AlsaProgrammeHandler(RadioScreen* infoScreen, const string& device) : lcdInfoScreen(infoScreen)
        {
            if (!device.empty())
            {
                pcm_device = device;
            }
        }
        virtual void onFrameErrors(int frameErrors) override
        {
            lock_guard<mutex> lock(errmutex);
            frame_errors += frameErrors;
            report_errors();
        }

        virtual void onNewAudio(vector<int16_t>&& audioData, int sampleRate, const string& mode) override
        {
            (void)mode;
            last_audio = now_ms();
            lock_guard<mutex> lock(aomutex);

            bool reset_ao = sampleRate != (int)rate;
            rate = sampleRate;

            if (!ao or reset_ao) {
                /* The sound card may be busy, for instance because another
                 * instance is still shutting down. Do not hammer it. */
                const uint64_t now = now_ms();
                if (ao_failed and now - last_ao_attempt < AO_RETRY_MS) {
                    return;
                }
                last_ao_attempt = now;

                cerr << "Create audio output rate " << rate << endl;
                ao = make_unique<AlsaOutput>(pcm_device.c_str(), 2, rate);
                if (not ao->ok()) {
                    cerr << "Could not open the audio output, trying again in "
                        << AO_RETRY_MS / 1000 << " seconds" << endl;
                    ao.reset();
                    ao_failed = true;
                    return;
                }
                ao_failed = false;
            }

            ao->playPCM(move(audioData));
        }

        virtual void onRsErrors(bool uncorrectedErrors, int numCorrectedErrors) override
        {
            lock_guard<mutex> lock(errmutex);
            if (uncorrectedErrors) {
                rs_uncorrected++;
            }
            rs_corrected += numCorrectedErrors;
            report_errors();
        }

        virtual void onAacErrors(int aacErrors) override
        {
            lock_guard<mutex> lock(errmutex);
            aac_errors += aacErrors;
            report_errors();
        }
        virtual void onNewDynamicLabel(const string& label) override
        {
            cout << "DLS: " << label << endl;
            lcdInfoScreen->setDLS(label);
        }

        virtual void onMOT(const mot_file_t& mot_file) override { (void)mot_file; }
        virtual void onPADLengthError(size_t announced_xpad_len, size_t xpad_len) override
        {
            cout << "X-PAD length mismatch, expected: " << announced_xpad_len << " got: " << xpad_len << endl;
        }

        /* Milliseconds since the last decoded audio frame. */
        uint64_t audio_age() const
        {
            return now_ms() - last_audio;
        }

        /* Called when we tune, so that the watchdog gives the decoder time to
         * deliver the first frames. */
        void resetAudioWatchdog()
        {
            last_audio = now_ms();
        }

    private:
        /* Reception errors are frequent when the signal is weak, so they are
         * summarised instead of being printed one by one. Called with errmutex
         * held. */
        void report_errors()
        {
            const uint64_t now = now_ms();
            if (now - last_error_report < ERROR_REPORT_MS) {
                return;
            }
            last_error_report = now;

            if (frame_errors or rs_uncorrected or rs_corrected or aac_errors) {
                cerr << "Reception errors in the last " <<
                    ERROR_REPORT_MS / 1000 << " seconds:" <<
                    " frame " << frame_errors <<
                    ", RS uncorrected " << rs_uncorrected <<
                    ", RS corrected " << rs_corrected <<
                    ", AAC " << aac_errors << endl;
            }

            frame_errors = 0;
            rs_uncorrected = 0;
            rs_corrected = 0;
            aac_errors = 0;
        }

        /* Written by the MSC handler thread, read by the main thread. */
        atomic<uint64_t> last_audio{now_ms()};
        mutex errmutex;
        uint64_t last_error_report = now_ms();
        int frame_errors = 0;
        int rs_uncorrected = 0;
        int rs_corrected = 0;
        int aac_errors = 0;
        mutex aomutex;
        unique_ptr<AlsaOutput> ao;
        bool ao_failed = false;
        uint64_t last_ao_attempt = 0;
        bool stereo = true;
        unsigned int rate = 48000;
        RadioScreen* lcdInfoScreen;
        string pcm_device;
};

class RadioInterface : public RadioControllerInterface {
    public:
        RadioInterface(RadioScreen* infoScreen) : lcdInfoScreen(infoScreen) {}
        virtual void onSNR(float /*snr*/) override { }
        /* Called for every frame. The drift is worth knowing, it tells how far
         * the tuner is off and how much correction range is left, but it has
         * to be reported sparingly. */
        virtual void onFrequencyCorrectorChange(int fine, int coarse) override
        {
            const int correction = fine + coarse;
            const uint64_t now = now_ms();
            if (now - last_corrector_report < CORRECTOR_REPORT_MS or
                    abs(correction - reported_correction) < CORRECTOR_REPORT_HZ) {
                return;
            }
            last_corrector_report = now;
            reported_correction = correction;

            cerr << "Frequency correction " << correction << " Hz (coarse " <<
                coarse << " Hz, fine " << fine << " Hz)" << endl;
        }

        virtual void onSyncChange(char isSync) override
        {
            synced = isSync;
            if (isSync) {
                last_sync = now_ms();
            }
            lcdInfoScreen->setSignalPresent(isSync);
        }
        virtual void onSignalPresence(bool /*isSignal*/) override { }
        virtual void onServiceDetected(uint32_t sId) override
        {
            cout << "New Service: 0x" << hex << sId << dec << endl;
        }

        virtual void onNewEnsemble(uint16_t eId) override
        {
            cout << "Ensemble name id: " << hex << eId << dec << endl;
        }

        virtual void onSetEnsembleLabel(DabLabel& label) override
        {
            // cout << "Ensemble label: " << label.utf8_label() << endl;
            lcdInfoScreen->setChannelName(label.utf8_label());
        }

        virtual void onDateTimeUpdate(const dab_date_time_t& dateTime) override { (void)dateTime; }

        virtual void onFIBDecodeSuccess(bool /* crcCheckOk */, const uint8_t* /* fib */) override { }
        virtual void onNewImpulseResponse(vector<float>&& data) override { (void)data; }
        virtual void onNewNullSymbol(vector<DSPCOMPLEX>&& data) override { (void)data; }
        virtual void onConstellationPoints(vector<DSPCOMPLEX>&& data) override { (void)data; }
        virtual void onMessage(message_level_t level, const string& text, const string& text2 = string()) override
        {
            string fullText;
            if (text2.empty())
                fullText = text;
            else
                fullText = text + text2;

            switch (level) {
                case message_level_t::Information:
                    cerr << "Info: " << fullText << endl;
                    break;
                case message_level_t::Error:
                    cerr << "Error: " << fullText << endl;
                    break;
            }
        }

        virtual void onTIIMeasurement(tii_measurement_t&& m) override { (void)m; }

        virtual void onInputFailure(void) override
        {
            cerr << "Input device failure, terminating" << endl;
            input_failure = true;
        }

        virtual void onRestartService(void) override
        {
            retune_requested = true;
        }

        /* Milliseconds since the demodulator was last in sync. */
        uint64_t sync_age() const
        {
            return synced ? 0 : now_ms() - last_sync;
        }

        /* Called when the receiver is restarted, so that the watchdog gives it
         * time to find the signal again. */
        void resetSyncWatchdog()
        {
            last_sync = now_ms();
        }

        atomic<bool> synced{false};
        RadioScreen* lcdInfoScreen;

    private:
        /* Written by the OFDM processor thread, read by the main thread. */
        atomic<uint64_t> last_sync{now_ms()};

        /* Only used by the OFDM processor thread. */
        uint64_t last_corrector_report = 0;
        int reported_correction = 0;
};

struct options_t {
    string soapySDRDriverArgs = "";
    string antenna = "";
    int gain = -1;
    string channel = "10B";
    string iqsource = "";
    string programme = "GRRIF";
    string frontend = "auto";
    string frontend_args = "";
    string pcm = PCM_DEVICE;
    bool daemon = false;
    string logfile = "";
    string pidfile = "";

    RadioReceiverOptions rro;
};

static void handle_signal(int signum)
{
    (void)signum;
    quit_requested = 1;
}

static void install_signal_handlers()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    /* No SA_RESTART: a blocking read on stdin shall return so that the
     * interactive loop notices the shutdown request. */
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, nullptr);
}

/* Redirect stdout and stderr either to <logfile> or, if none was given, to
 * /dev/null. When <close_stdin> is set, stdin is connected to /dev/null too.
 * Returns false on error. */
static bool redirect_streams(const string& logfile, bool close_stdin)
{
    int devnull = open("/dev/null", O_RDWR);
    if (devnull < 0) {
        return false;
    }

    int out = devnull;
    if (!logfile.empty()) {
        out = open(logfile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (out < 0) {
            cerr << "Could not open logfile " << logfile << ": " <<
                strerror(errno) << endl;
            close(devnull);
            return false;
        }
    }

    bool ok = dup2(out, STDOUT_FILENO) != -1 and
              dup2(out, STDERR_FILENO) != -1;

    if (ok and close_stdin) {
        ok = dup2(devnull, STDIN_FILENO) != -1;
    }

    if (out != devnull) {
        close(out);
    }
    if (devnull > STDERR_FILENO) {
        close(devnull);
    }

    /* Once redirected to a file, cout would be fully buffered and the log
     * would only show up much later. */
    cout.setf(ios::unitbuf);

    return ok;
}

/* Classic double fork so that the process ends up without a controlling
 * terminal and is reparented to init. */
static bool daemonise(const string& logfile)
{
    pid_t pid = fork();
    if (pid < 0) {
        cerr << "First fork failed: " << strerror(errno) << endl;
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    if (setsid() < 0) {
        cerr << "setsid failed: " << strerror(errno) << endl;
        return false;
    }

    pid = fork();
    if (pid < 0) {
        cerr << "Second fork failed: " << strerror(errno) << endl;
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    umask(022);
    if (chdir("/") != 0) {
        cerr << "Could not chdir to /: " << strerror(errno) << endl;
    }

    return redirect_streams(logfile, true);
}

/* Removes the pidfile again when welle-pi terminates. */
class PidFile {
    public:
        PidFile(const string& path) : m_path(path) {
            if (m_path.empty()) {
                return;
            }
            ofstream f(m_path);
            if (!f) {
                cerr << "Could not write pidfile " << m_path << endl;
                m_path.clear();
                return;
            }
            f << getpid() << endl;
        }
        ~PidFile() {
            if (!m_path.empty()) {
                unlink(m_path.c_str());
            }
        }
        PidFile(const PidFile&) = delete;
        PidFile& operator=(const PidFile&) = delete;
    private:
        string m_path;
};

static void usage()
{
    cerr <<
    "Usage: welle-pi [OPTION]" << endl <<
    "   or: welle-pi -b [OPTION]" << endl <<
    endl <<
    "welle-pi is welle.io's interface for Raspberry PI." << endl <<
    endl <<
    "Options:" << endl <<
    endl <<
    "Tuning:" << endl <<
    "    -c channel    Tune to <channel> (eg. 10B, 5A, LD...)." << endl <<
    "    -p programme  Play <programme> with ALSA. The <programme> can be either" << endl <<
    "                  * a station's label (eg. GRIFF) - or a part of it - or" << endl <<
    "                  * a station's Service Id (eg. 0x4f57 or 20311)." << endl <<
    endl <<
    "Backend and input options:" << endl <<
    "    -f file       Read an IQ file <file> and play with ALSA." << endl <<
    "                  IQ file format is u8, unless the file ends with 'FORMAT.iq'." << endl <<
    "    -u            Disable coarse corrector, for receivers who have a low " << endl <<
    "                  frequency offset." << endl <<
    "    -g gain       Set input gain to <gain> or -1 for auto gain." << endl <<
    "    -F driver     Set input driver and arguments." << endl <<
    "                  Please note that some input drivers are available only if" << endl <<
    "                  they were enabled at build time." << endl <<
    "                  Possible values are: auto (default), airspy, rtl_sdr," << endl <<
    "                  android_rtl_sdr, rtl_tcp, soapysdr." << endl <<
    "                  With \"rtl_tcp\", host IP and port can be specified as " << endl <<
    "                  \"rtl_tcp,<HOST_IP>:<PORT>\"." << endl <<
    "    -s args       SoapySDR Driver arguments." << endl <<
    "    -A antenna    Set input antenna to ANT (for SoapySDR input only)." << endl <<
    endl <<
    "Output options:" << endl <<
    "    -o device     Specify alsa PCM device by name." << endl <<
    endl <<
    "Daemon options:" << endl <<
    "    -b            Detach from the terminal and run in the background." << endl <<
    "                  In that mode welle-pi does not read commands from" << endl <<
    "                  standard input any more, it keeps playing the selected" << endl <<
    "                  programme until it receives SIGINT, SIGTERM or SIGHUP." << endl <<
    "    -l logfile    Append standard output and standard error to <logfile>." << endl <<
    "                  Without this option they are sent to /dev/null when" << endl <<
    "                  running as a daemon." << endl <<
    "    -k pidfile    Write the process id to <pidfile> and remove that file" << endl <<
    "                  again on exit." << endl <<
    endl <<
    "Other options:" << endl <<
    "    -h            Display this help and exit." << endl <<
    "    -v            Output version information and exit." << endl <<
    endl <<
    "Examples:" << endl <<
    endl <<
    "welle-pi -c 10B -p GRRIF" << endl <<
    "    Receive 'GRRIF' on channel '10B' using 'auto' driver, and play with ALSA." << endl <<
    endl <<
    "welle-pi -f ./ofdm.iq -p GRRIF" << endl <<
    "    Read IQ file './ofdm.iq' (in u8 format) and play programme 'GRIFF' with ALSA." << endl <<
    endl <<
    "welle-pi -c 10B -p GRRIF -F rtl_tcp,localhost:1234" << endl <<
    "    Receive 'GRRIF' on channel '10B' using 'rtl_tcp' driver on localhost:1234," << endl <<
    "    and play with ALSA." << endl <<
    endl <<
    "welle-pi -b -l /var/log/welle-pi.log -k /run/welle-pi.pid -c 10B -p GRRIF" << endl <<
    "    Same as above, but in the background, logging to a file." << endl <<
    endl <<
    "Report bugs to: <https://github.com/AlbrechtL/welle.io/issues>" << endl;
}

static void copyright()
{
    cerr <<
    "Copyright (C) 2018 Matthias P. Braendli." << endl <<
    "Copyright (C) 2017 Albrecht Lohofener." << endl <<
    "License GPL-2.0-or-later: GNU General Public License v2.0 or later" << endl <<
    "<https://www.gnu.org/licenses/old-licenses/gpl-2.0-standalone.html>" << endl <<
    endl <<
    "Written by: Albrecht Lohofener & Matthias P. Braendli." << endl <<
    "Other contributors: <https://github.com/AlbrechtL/welle.io/blob/master/AUTHORS>" << endl;
}

static void version()
{
    cerr << "welle-pi " << VERSION << endl;
}

options_t parse_cmdline(int argc, char **argv)
{
    options_t options;
    string fe_opt = "";
    options.rro.decodeTII = false;

    int opt;
    while ((opt = getopt(argc, argv, "A:bc:f:F:g:hk:l:o:p:s:uv")) != -1) {
        switch (opt) {
            case 'A':
                options.antenna = optarg;
                break;
            case 'b':
                options.daemon = true;
                break;
            case 'c':
                options.channel = optarg;
                break;
            case 'f':
                options.iqsource = optarg;
                break;
            case 'F':
                fe_opt = optarg;
                break;
            case 'g':
                options.gain = atoi(optarg);
                break;
            case 'k':
                options.pidfile = optarg;
                break;
            case 'l':
                options.logfile = optarg;
                break;
            case 'o':
                options.pcm = optarg;
                break;
            case 'p':
                options.programme = optarg;
                break;
            case 'h':
                usage();
                exit(1);
            case 's':
                options.soapySDRDriverArgs = optarg;
                break;
            case 'v':
                version();
                cerr << endl;
                copyright();
                exit(0);
            case 'u':
                options.rro.disableCoarseCorrector = true;
                break;
            default:
                cerr << "Unknown option. Use -h for help" << endl;
                exit(1);
        }
    }

    if (!fe_opt.empty()) {
        size_t comma = fe_opt.find(',');
        if (comma != string::npos) {
            options.frontend      = fe_opt.substr(0,comma);
            options.frontend_args = fe_opt.substr(comma+1);
        } else {
            options.frontend = fe_opt;
        }
    }

    return options;
}

unsigned parse_service_to_tune(const string& name) {
    try {
        unsigned long id = stoul(name, nullptr, 0);
        if (id <= numeric_limits<unsigned>::max())
            return (unsigned)id;
        else
            return 0;
    }
    catch (...) {
        return 0;
    }
};



/* Returns true if we are playing the requested service. */
static bool tune_to_service(RadioReceiver& rx, AlsaProgrammeHandler& ph,
        RadioScreen& lcdIS, const string& service_to_tune,
        unsigned service_to_tune_idx)
{
    bool service_selected = false;

    ph.resetAudioWatchdog();

    for (const auto& s : rx.getServiceList()) {
        if ((service_to_tune_idx && s.serviceId == service_to_tune_idx) ||
                s.serviceLabel.utf8_label().find(service_to_tune) != string::npos) {
            string dumpFileName;
            if (rx.playSingleProgramme(ph, dumpFileName, s) == false) {
                cerr << "Tune to " << service_to_tune << " failed" << endl;
            }
            else {
                service_selected = true;
                lcdIS.setProgramName(s.serviceLabel.utf8_label());
            }
        }
    }

    if (not service_selected) {
        cerr << "Could not tune to " << service_to_tune << endl;
    }

    return service_selected;
}

static void runAutoScanner(RadioReceiver& rx, CVirtualInput* in, RadioInterface& ri, Channels& channels, ConfigManager& config, UIManager& uiManager) {
    auto scanningScreen = make_shared<ScanningScreen>();
    uiManager.setScreen(scanningScreen);

    const std::vector<std::string> SCAN_CHANNELS = {
        "5A", "5B", "5C", "5D",
        "6A", "6B", "6C", "6D",
        "7A", "7B", "7C", "7D",
        "8A", "8B", "8C", "8D",
        "9A", "9B", "9C", "9D",
        "10A", "10B", "10C", "10D",
        "11A", "11B", "11C", "11D",
        "12A", "12B", "12C", "12D",
        "13A", "13B", "13C", "13D", "13E", "13F"
    };

    config.clearStations();

    for (const auto& channel : SCAN_CHANNELS) {
        if (stop_requested()) break;

        int foundCount = config.getStations().size();
        scanningScreen->setScanningStatus(channel, foundCount);

        auto freq = channels.getFrequency(channel);
        in->setFrequency(freq);
        rx.restart(false);
        ri.resetSyncWatchdog();
        ri.synced = false;

        int sync_wait = 30;
        while (!ri.synced && sync_wait > 0 && !stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sync_wait--;
        }

        if (ri.synced) {
            int service_wait = 50;
            while (rx.getServiceList().empty() && service_wait > 0 && !stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                service_wait--;
            }
            if (!rx.getServiceList().empty() && !stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                for (const auto& s : rx.getServiceList()) {
                    ConfigManager::Station station;
                    station.channel = channel;
                    station.program = s.serviceLabel.utf8_label();
                    station.service_id = s.serviceId;
                    config.addStation(station);
                }
            }
        }
    }

    if (!stop_requested()) {
        config.saveConfig();
    }
}

int main(int argc, char **argv)
{
    auto options = parse_cmdline(argc, argv);

    if (options.daemon) {
        if (not daemonise(options.logfile)) {
            cerr << "Could not run as a daemon" << endl;
            return 1;
        }
    }
    else if (not options.logfile.empty()) {
        if (not redirect_streams(options.logfile, false)) {
            cerr << "Could not redirect the output to " << options.logfile << endl;
            return 1;
        }
    }

    install_signal_handlers();

    /* Without a terminal there is nobody who could enter a programme name,
     * so welle-pi keeps playing until it is asked to terminate. */
    const bool interactive = isatty(STDIN_FILENO);

    PidFile pidfile(options.pidfile);

    version();

    if (not interactive and options.programme.empty()) {
        cerr << "No programme given and no terminal to ask for one" << endl;
        return 1;
    }

    UIManager uiManager;
    auto radioScreen = make_shared<RadioScreen>();
    uiManager.setScreen(radioScreen);
    auto& lcdIS = *radioScreen;

    RadioInterface ri(&lcdIS);

    Channels channels;

    unique_ptr<CVirtualInput> in = nullptr;

    if (options.iqsource.empty()) {
        in.reset(CInputFactory::GetDevice(ri, options.frontend));

        if (not in) {
            cerr << "Could not start device" << endl;
            return 1;
        }
    }
    else {
        // Throttle the file input and rewind at the end, so that welle-pi
        // plays the recording like a real receiver would.
        auto in_file = make_unique<CRAWFile>(ri, true, true);
        in_file->setFileName(options.iqsource, "auto");
        in = move(in_file);
    }

    if (options.gain == -1) {
        in->setAgc(true);
    }
    else {
        in->setAgc(false);
        in->setGain(options.gain);
    }


#ifdef HAVE_SOAPYSDR
    if (not options.antenna.empty() and in->getID() == CDeviceID::SOAPYSDR) {
        dynamic_cast<CSoapySdr*>(in.get())->setDeviceParam(DeviceParam::SoapySDRAntenna, options.antenna);
    }

    if (not options.soapySDRDriverArgs.empty() and in->getID() == CDeviceID::SOAPYSDR) {
        dynamic_cast<CSoapySdr*>(in.get())->setDeviceParam(DeviceParam::SoapySDRDriverArgs, options.soapySDRDriverArgs);
    }
#endif
    if (options.frontend == "rtl_tcp" && !options.frontend_args.empty()) {
        string args = options.frontend_args;
        size_t colon = args.find(':');
        if (colon == string::npos) {
            cerr << "I need a colon ':' to parse rtl_tcp options!" << endl;
            return 1;
        }
        else {
            string host = args.substr(0, colon);
            string port = args.substr(colon + 1);
            if (!host.empty()) {
                dynamic_cast<CRTL_TCP_Client*>(in.get())->setServerAddress(host);
            }
            if (!port.empty()) {
                dynamic_cast<CRTL_TCP_Client*>(in.get())->setPort(atoi(port.c_str()));
            }
            // cout << "setting rtl_tcp host to '" << host << "', port to '" << atoi(port.c_str()) << "'" << endl;
        }
    }
    auto freq = channels.getFrequency(options.channel);
    in->setFrequency(freq);

    RadioReceiver rx(ri, *in, options.rro);
    AlsaProgrammeHandler ph(&lcdIS, options.pcm);

    ConfigManager config;
    bool hasConfig = config.loadConfig();

    auto stations = config.getStations();
    int current_station_idx = -1;

    auto tuneToStationIndex = [&](int idx) {
        if (idx < 0 || idx >= (int)stations.size()) return;
        current_station_idx = idx;
        const auto& st = stations[current_station_idx];

        cerr << "Tuning to station: " << st.program << " on " << st.channel << endl;

        auto freq = channels.getFrequency(st.channel);
        in->setFrequency(freq);
        rx.restart(false);
        ri.resetSyncWatchdog();
        ri.synced = false;

        int sync_wait = 30;
        while (!ri.synced && sync_wait > 0 && !stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sync_wait--;
        }

        int service_wait = 50;
        while (rx.getServiceList().empty() && service_wait > 0 && !stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            service_wait--;
        }

        bool tuned = tune_to_service(rx, ph, *radioScreen, st.program, st.service_id);
        if (tuned) {
            config.setLastPlayed(st.channel, st.program);
            config.saveConfig();
        }
    };

    auto menuScreen = make_shared<MenuScreen>();

    radioScreen->setInputCallback([&](InputAction action) {
        if (action == InputAction::UP) {
            if (!stations.empty()) {
                int next_idx = (current_station_idx - 1 + stations.size()) % stations.size();
                tuneToStationIndex(next_idx);
            }
        } else if (action == InputAction::DOWN) {
            if (!stations.empty()) {
                int next_idx = (current_station_idx + 1) % stations.size();
                tuneToStationIndex(next_idx);
            }
        } else if (action == InputAction::MENU) {
            uiManager.setScreen(menuScreen);
        }
    });

    menuScreen->setSelectCallback([&](int optionIdx) {
        if (optionIdx == 0) {
            runAutoScanner(rx, in.get(), ri, channels, config, uiManager);
            stations = config.getStations();
            if (!stations.empty()) {
                uiManager.setScreen(radioScreen);
                tuneToStationIndex(0);
            } else {
                uiManager.setScreen(radioScreen);
            }
        } else if (optionIdx == 1) {
            uiManager.setScreen(radioScreen);
        }
    });

    if (!hasConfig || stations.empty()) {
        cerr << "No config or empty station list, running auto scan..." << endl;
        runAutoScanner(rx, in.get(), ri, channels, config, uiManager);
        stations = config.getStations();
    }

    if (!stations.empty()) {
        int start_idx = 0;
        string last_ch = config.getLastPlayedChannel();
        string last_pr = config.getLastPlayedProgram();
        if (!options.programme.empty()) {
            for (size_t i = 0; i < stations.size(); ++i) {
                if (stations[i].program.find(options.programme) != string::npos) {
                    start_idx = i;
                    break;
                }
            }
        } else if (!last_ch.empty() && !last_pr.empty()) {
            for (size_t i = 0; i < stations.size(); ++i) {
                if (stations[i].channel == last_ch && stations[i].program == last_pr) {
                    start_idx = i;
                    break;
                }
            }
        }
        uiManager.setScreen(radioScreen);
        tuneToStationIndex(start_idx);
    } else {
        cerr << "No stations found after scan." << endl;
        uiManager.setScreen(radioScreen);
    }

    InputQueue inputQueue;
    thread gpioThread(runGPIOPolling, ref(inputQueue));
    gpioThread.detach();

    thread stdinThread;
    if (interactive) {
        stdinThread = thread(runStdinReader, ref(inputQueue));
    }

    while (!stop_requested()) {
        InputAction action = inputQueue.pop();
        if (action == InputAction::QUIT) {
            break;
        }
        if (action != InputAction::NONE) {
            uiManager.processInput(action);
        }
    }

    if (stdinThread.joinable()) {
        stdinThread.join();
    }

    cerr << "Shutting down" << endl;

    return input_failure ? 1 : 0;
}
