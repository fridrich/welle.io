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
#include <condition_variable>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <set>
#include <utility>
#include <cstdio>
#include <unistd.h>
#ifdef HAVE_SOAPYSDR
#  include "soapy_sdr.h"
#endif
#include "rtl_tcp.h"
#include "welle-cli/alsa-output.h"
#include <liblcd/liblcd.h>
#include "backend/radio-receiver.h"
#include "input/input_factory.h"
#include "various/channels.h"
#include "libs/json.hpp"

#ifdef GITDESCRIBE
#define VERSION GITDESCRIBE
#else
#define VERSION "unknown"
#endif

using namespace std;

class LCDInfoScreen
{
    public:
        LCDInfoScreen() {
            m_display.backlightOn();
            m_thread = thread(&LCDInfoScreen::draw, this);
        }
        ~LCDInfoScreen() {
            m_exit = true;
            setProgramName("");
            sleep(2);
            m_display.clear();
            if (m_thread.joinable())
                m_thread.join();
            m_changed = true;
        }
        void setChannelName(const string& channel_name) {
            if (m_channelName.compare(channel_name) != 0) {
                m_channelName = channel_name;
                m_changed = true;
            }
        }
        void setProgramName(const string& program_name) {
            if (m_programName.compare(program_name) != 0) {
                m_programName = program_name;
                m_changed = true;
                m_dlsQueue.clear();
                m_dls.clear();
                m_display.interrupt();
            }

        }
        void setDLS(const string& dls) {
            if (m_dlsQueue.empty())
                m_changed = true;
            if (m_dlsQueue.empty() || m_dlsQueue.back().compare(dls) != 0) {
                m_dlsQueue.push_back(dls);
            }
        }
    private:
        void draw() {
            while (!m_exit) {
                if (m_changed) {
                    m_changed = false;
                    m_display.clear();
                    m_display.gotoXY(0,0);
                    m_display.write(m_programName.c_str());
                    m_display.killEOL();
                    m_display.gotoXY(0,1);
                    m_display.write(m_channelName.c_str());
                    m_display.killEOL();
                    m_display.gotoXY(0,0);
                    m_display.gotoLastLine();
                    while (!m_changed && !m_exit) {
                        if (!m_dlsQueue.empty()) {
                            m_dls = m_dlsQueue.front();
                            m_dlsQueue.pop_front();
                        }
                        if (m_display.scroll(m_dls.c_str())) {
                        }
                        else {
                            while(!m_changed && !m_exit && m_dlsQueue.empty()) {
                                sleep(1);
                            }
                        }
                    }
                }
            }
        }
        liblcd::LCDDisplay m_display;
        string m_channelName;
        string m_programName;
        string m_dls;
        deque<string> m_dlsQueue;
        thread m_thread;
        bool m_changed = true;
        bool m_exit = false;
};

class AlsaProgrammeHandler: public ProgrammeHandlerInterface {
    public:
        AlsaProgrammeHandler(LCDInfoScreen* infoScreen, const string& device) : lcdInfoScreen(infoScreen)
        {
            if (!device.empty())
            {
                pcm_device = device;
            }
        }
        virtual void onFrameErrors(int frameErrors) override { (void)frameErrors; }
        virtual void onNewAudio(vector<int16_t>&& audioData, int sampleRate, const string& mode) override
        {
            (void)mode;
            lock_guard<mutex> lock(aomutex);

            bool reset_ao = sampleRate != (int)rate;
            rate = sampleRate;

            if (!ao or reset_ao) {
                cerr << "Create audio output rate " << rate << endl;
                ao = make_unique<AlsaOutput>(pcm_device.c_str(), 2, rate);
            }

            ao->playPCM(move(audioData));
        }

        virtual void onRsErrors(bool uncorrectedErrors, int numCorrectedErrors) override {
            (void)uncorrectedErrors; (void)numCorrectedErrors; }
        virtual void onAacErrors(int aacErrors) override { (void)aacErrors; }
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

    private:
        mutex aomutex;
        unique_ptr<AlsaOutput> ao;
        bool stereo = true;
        unsigned int rate = 48000;
        LCDInfoScreen* lcdInfoScreen;
        string pcm_device;
};

class RadioInterface : public RadioControllerInterface {
    public:
        RadioInterface(LCDInfoScreen* infoScreen) : lcdInfoScreen(infoScreen) {}
        virtual void onSNR(float /*snr*/) override { }
        virtual void onFrequencyCorrectorChange(int /*fine*/, int /*coarse*/) override { }
        virtual void onSyncChange(char isSync) override { synced = isSync; }
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

        bool synced = false;
        LCDInfoScreen* lcdInfoScreen;
};

struct options_t {
    string soapySDRDriverArgs = "";
    string antenna = "";
    int gain = -1;
    string channel = "10B";
    string programme = "GRRIF";
    string frontend = "auto";
    string frontend_args = "";
    string pcm = PCM_DEVICE;

    RadioReceiverOptions rro;
};

static void usage()
{
    cerr <<
    "Usage: welle-pi [OPTION]" << endl <<
    "   or: welle-pi -w <port> [OPTION]" << endl <<
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
    "    -D            Select ALSA PCM device by name." << endl <<
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
    "welle-pi -c 10B -p GRRIF -F rtl_tcp,localhost:1234" << endl <<
    "    Receive 'GRRIF' on channel '10B' using 'rtl_tcp' driver on localhost:1234," << endl <<
    "    and play with ALSA." << endl <<
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
    while ((opt = getopt(argc, argv, "A:c:D:F:g:hp:s:uv")) != -1) {
        switch (opt) {
            case 'A':
                options.antenna = optarg;
                break;
            case 'c':
                options.channel = optarg;
                break;
            case 'D':
                options.pcm = optarg;
                break;
            case 'F':
                fe_opt = optarg;
                break;
            case 'g':
                options.gain = atoi(optarg);
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
        return (unsigned)stoi(name, nullptr, 0);
    }
    catch (const invalid_argument&) {
        return 0;
    }
};

int main(int argc, char **argv)
{
    auto options = parse_cmdline(argc, argv);
    version();

    LCDInfoScreen lcdIS;

    RadioInterface ri(&lcdIS);

    Channels channels;

    unique_ptr<CVirtualInput> in = nullptr;

    in.reset(CInputFactory::GetDevice(ri, options.frontend));

    if (not in) {
        cerr << "Could not start device" << endl;
        return 1;
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
    string service_to_tune = options.programme;
    unsigned service_to_tune_idx = parse_service_to_tune(service_to_tune);

    RadioReceiver rx(ri, *in, options.rro);

    rx.restart(false);

    cerr << "Wait for sync" << endl;
    while (not ri.synced) {
        this_thread::sleep_for(chrono::seconds(1));
    }

    cerr << "Wait for service list" << endl;
    while (rx.getServiceList().empty()) {
         this_thread::sleep_for(chrono::seconds(1));
    }

    // Wait an additional 3 seconds so that the receiver can complete the service list
    this_thread::sleep_for(chrono::seconds(3));

    AlsaProgrammeHandler ph(&lcdIS, options.pcm);
    while (not service_to_tune.empty()) {
        cerr << "Service list" << endl;
        for (const auto& s : rx.getServiceList()) {
            cerr << "  [0x" << hex << s.serviceId << dec << "] " <<
                s.serviceLabel.utf8_label() << " ";
            for (const auto& sc : rx.getComponents(s)) {
                cerr << " [component "  << sc.componentNr <<
                    " ASCTy: " <<
                    (sc.audioType() == AudioServiceComponentType::DAB ? "DAB" :
                     sc.audioType() == AudioServiceComponentType::DABPlus ? "DAB+" : "unknown") << " ]";
                const auto& sub = rx.getSubchannel(sc);
                cerr << " [subch " << sub.subChId << " bitrate:" << sub.bitrate() << " at SAd:" << sub.startAddr << "]";
            }
            cerr << endl;
        }

        bool service_selected = false;
        for (const auto& s : rx.getServiceList()) {
                    if ((service_to_tune_idx && s.serviceId == service_to_tune_idx) || s.serviceLabel.utf8_label().find(service_to_tune) != string::npos) {
                service_selected = true;
                string dumpFileName;
                if (rx.playSingleProgramme(ph, dumpFileName, s) == false) {
                    cerr << "Tune to " << service_to_tune << " failed" << endl;
                }
                else {
                    lcdIS.setProgramName(s.serviceLabel.utf8_label());
                }
            }
        }
        if (not service_selected) {
            cerr << "Could not tune to " << service_to_tune << endl;
        }
        cerr << "**** Please enter programme name. Enter '.' to quit." << endl;

        cin >> service_to_tune;
        if (service_to_tune == ".") {
            break;
        }
        service_to_tune_idx = parse_service_to_tune(service_to_tune);
        cerr << "**** Trying to tune to " << service_to_tune << endl;
    }

    return 0;
}
