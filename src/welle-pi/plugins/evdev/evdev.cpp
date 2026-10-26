/******************************************************************************\
 * Description:
 * welle-pi input plugin: remote control whose USB dongle appears as a
 * keyboard (2.4 GHz or infrared), read through /dev/input/event*
 *
 ******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
\******************************************************************************/
#include "input_plugin.h"
#include "remote_config.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <fcntl.h>
#include <glob.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

extern "C" {
const char welle_pi_plugin_name[] = "evdev";
const char welle_pi_plugin_description[] = "Remote control as keyboard (evdev)";
const int welle_pi_input_abi = WELLE_PI_INPUT_ABI;
}

namespace {

const char* const LOG = "evdev: ";

/* How long the input thread sleeps before it looks for a remote again, and
 * how often it complains about not finding one. */
const int RESCAN_MS = 2000;
const unsigned RESCAN_QUIET = 15;

/* A 2.4 GHz dongle often has two event nodes (keyboard and consumer
 * control); read both, but not half the keyboards of a desktop. */
const size_t MAX_DEVICES = 4;

const int POLL_MS = 200;

/* A button that is held down repeats: one step, a pause, then a steady rate,
 * so that the station list scrolls and the volume ramps. The autorepeat of the
 * remote itself (value 2, if the kernel sends it at all) is ignored: it runs
 * at the keyboard rate of 33 ms, which the screens cannot follow. */
const int REPEAT_DELAY_MS = 400;
const int REPEAT_INTERVAL_MS = 120;

struct Device {
    int fd;
    std::string path;
    std::string name;
};

/* A button that is down: the action it sends and when it repeats next */
struct Held {
    int action;
    long due;
};

long nowMs()
{
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

bool testBit(int bit, const unsigned long* bits)
{
    const size_t per_long = 8 * sizeof(long);
    return (bits[size_t(bit) / per_long] >> (size_t(bit) % per_long)) & 1;
}

std::string deviceName(int fd)
{
    char buf[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof buf), buf) < 0) {
        return std::string();
    }
    return std::string(buf);
}

bool hasKeys(int fd)
{
    unsigned long bits[(EV_MAX / (8 * sizeof(long))) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(0, sizeof bits), bits) < 0) {
        return false;
    }
    return testBit(EV_KEY, bits);
}

/* How many of the key codes we handle the device sends at all. */
int keyScore(int fd, const RemoteConfig& cfg)
{
    unsigned long bits[(KEY_CNT / (8 * sizeof(long))) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0) {
        return 0;
    }
    int score = 0;
    for (const auto& key : cfg.map) {
        if (testBit(key.first, bits)) {
            score++;
        }
    }
    return score;
}

bool containsNoCase(const std::string& text, const std::string& part)
{
    const auto it = std::search(text.begin(), text.end(), part.begin(), part.end(),
            [](char a, char b) {
                return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
            });
    return it != text.end();
}

std::vector<std::string> eventPaths()
{
    std::vector<std::string> paths;
    glob_t found = {};
    if (glob("/dev/input/event*", 0, nullptr, &found) == 0) {
        for (size_t i = 0; i < found.gl_pathc; ++i) {
            paths.push_back(found.gl_pathv[i]);
        }
    }
    globfree(&found);
    return paths;
}

/* The keyboard node of a 2.4 GHz dongle and its consumer control node (the
 * one with the volume keys) have different names but come from one USB
 * device, so they report the same vendor and product. Devices without an
 * identity (vendor and product 0) are only matched by their name. */
bool sameDevice(const struct input_id& id, const struct input_id& other)
{
    if (id.vendor == 0 && id.product == 0) {
        return false;
    }
    return id.bustype == other.bustype && id.vendor == other.vendor
        && id.product == other.product;
}

/* All event nodes of the remote: the ones that carry our keys, limited to the
 * device of the node that carries the most of them. */
std::vector<std::string> findDevices(const RemoteConfig& cfg)
{
    if (!cfg.device.empty()) {
        return {cfg.device};
    }

    struct Candidate {
        std::string path;
        std::string name;
        int score;
        struct input_id id;
    };
    std::vector<Candidate> candidates;
    for (const auto& path : eventPaths()) {
        const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        const std::string name = deviceName(fd);
        const int score = hasKeys(fd) ? keyScore(fd, cfg) : 0;
        /* stays empty when the device has no identity, then it is matched by
         * its name only */
        struct input_id id = {};
        (void)ioctl(fd, EVIOCGID, &id);
        close(fd);
        if (score <= 0) {
            continue;
        }
        if (!cfg.name.empty() && !containsNoCase(name, cfg.name)) {
            continue;
        }
        candidates.push_back({path, name, score, id});
    }
    if (candidates.empty()) {
        return {};
    }

    size_t best = 0;
    for (size_t i = 1; i < candidates.size(); ++i) {
        if (candidates[i].score > candidates[best].score) {
            best = i;
        }
    }

    std::vector<std::string> paths;
    for (const auto& candidate : candidates) {
        const bool same = sameDevice(candidates[best].id, candidate.id)
            || candidate.name == candidates[best].name;
        if (same && paths.size() < MAX_DEVICES) {
            paths.push_back(candidate.path);
        }
    }
    return paths;
}

/* Lists what is there, so that "device" or "name" can be set from the log on
 * a machine with several input devices. */
void logCandidates(const RemoteConfig& cfg)
{
    for (const auto& path : eventPaths()) {
        const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            /* most often EACCES: the user is not in the "input" group */
            std::cerr << LOG << path << ": " << strerror(errno) << std::endl;
            continue;
        }
        const std::string name = deviceName(fd);
        std::cerr << LOG << path << ": \"" << name << "\", "
                  << (hasKeys(fd) ? keyScore(fd, cfg) : 0) << " of our keys" << std::endl;
        close(fd);
    }
}

/* How long the input thread sleeps at most, and when the next held button is
 * due: the thread sleeps exactly as long as it can. */
int repeatTimeout(const std::map<int, Held>& held)
{
    if (held.empty()) {
        return POLL_MS;
    }
    const long now = nowMs();
    long next = POLL_MS;
    for (const auto& key : held) {
        next = std::min(next, key.second.due - now);
    }
    return static_cast<int>(std::max(1L, next));
}

/* Sends the next step of every button that is held long enough */
void repeatHeld(std::map<int, Held>& held, const welle_pi_input_host* host)
{
    const long now = nowMs();
    for (auto& key : held) {
        if (key.second.due > now) {
            continue;
        }
        host->push(host->ctx, static_cast<welle_pi_action>(key.second.action));
        key.second.due = now + REPEAT_INTERVAL_MS;
    }
}

void handleKey(int code, const RemoteConfig& cfg, const welle_pi_input_host* host,
        std::set<int>& reported, std::map<int, Held>& held)
{
    int action = 0;
    if (lookupKey(cfg, code, action)) {
        if (action < 0) {
            return;
        }
        host->push(host->ctx, static_cast<welle_pi_action>(action));
        /* a repeatable button stays down until its release event */
        if (repeatableAction(action)) {
            held[code] = {action, nowMs() + REPEAT_DELAY_MS};
        }
        return;
    }

    /* Mouse and gamepad buttons of the same dongle: never an action. */
    if (code >= BTN_MISC && code < KEY_OK) {
        return;
    }

    /* Once per code: the log tells which code to put into "map". */
    if (reported.insert(code).second) {
        const char* name = keyName(code);
        std::cerr << LOG << "key " << code << " (" << (name ? name : "unknown")
                  << ") is not mapped, see \"map\" in welle-pi.json" << std::endl;
    }
}

/* Reads the devices until one of them goes away (returns) or welle-pi stops. */
void readDevices(std::vector<Device>& devices, const RemoteConfig& cfg,
        const welle_pi_input_host* host)
{
    std::vector<pollfd> fds;
    for (const auto& device : devices) {
        fds.push_back({device.fd, POLLIN, 0});
    }

    std::set<int> reported;
    std::map<int, Held> held;
    while (!host->stop_requested(host->ctx)) {
        const int ready = poll(fds.data(), fds.size(), repeatTimeout(held));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << LOG << "poll: " << strerror(errno) << std::endl;
            return;
        }

        for (size_t i = 0; i < fds.size(); ++i) {
            if (!(fds[i].revents & (POLLIN | POLLERR | POLLHUP))) {
                continue;
            }
            struct input_event events[64];
            const ssize_t got = read(fds[i].fd, events, sizeof events);
            if (got < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                }
                std::cerr << LOG << devices[i].path << ": " << strerror(errno) << std::endl;
                return;
            }
            if (got == 0) {
                return;
            }
            const size_t count = size_t(got) / sizeof events[0];
            for (size_t e = 0; e < count; ++e) {
                if (events[e].type != EV_KEY) {
                    continue;
                }
                /* 1 is the press, 0 the release that stops the repeat, 2 the
                 * remote's own autorepeat, which repeatHeld() does itself */
                if (events[e].value == 1) {
                    handleKey(events[e].code, cfg, host, reported, held);
                } else if (events[e].value == 0) {
                    held.erase(events[e].code);
                }
            }
        }

        repeatHeld(held, host);
    }
}

void sleepUnlessStopped(const welle_pi_input_host* host, int ms)
{
    for (int slept = 0; slept < ms && !host->stop_requested(host->ctx); slept += 100) {
        usleep(100 * 1000);
    }
}

int run(const RemoteConfig& cfg, const welle_pi_input_host* host)
{
    std::cerr << LOG << cfg.map.size() << " keys, "
              << (cfg.grab ? "grabbed" : "not grabbed") << ", "
              << (cfg.device.empty() ? "looking for a device" : "device " + cfg.device)
              << std::endl;

    unsigned attempts = 0;
    while (!host->stop_requested(host->ctx)) {
        std::vector<Device> devices;
        for (const auto& path : findDevices(cfg)) {
            const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                std::cerr << LOG << path << ": " << strerror(errno) << std::endl;
                continue;
            }
            if (!hasKeys(fd)) {
                std::cerr << LOG << path << ": not a keyboard, ignoring" << std::endl;
                close(fd);
                continue;
            }
            const std::string name = deviceName(fd);
            if (cfg.grab && ioctl(fd, EVIOCGRAB, 1) < 0) {
                std::cerr << LOG << path << ": cannot grab (" << strerror(errno)
                          << "), keys reach the console as well" << std::endl;
            }
            std::cerr << LOG << "listening on " << path << " (\"" << name << "\")" << std::endl;
            devices.push_back({fd, path, name});
        }

        if (devices.empty()) {
            if (attempts++ % RESCAN_QUIET == 0) {
                logCandidates(cfg);
                std::cerr << LOG << "no remote found, retrying" << std::endl;
            }
        } else {
            attempts = 0;
            readDevices(devices, cfg, host);
        }

        for (auto& device : devices) {
            if (cfg.grab) {
                ioctl(device.fd, EVIOCGRAB, 0);
            }
            close(device.fd);
        }
        sleepUnlessStopped(host, RESCAN_MS);
    }
    return 0;
}

} // namespace

int welle_pi_input_run(const struct welle_pi_input_host* host, const char* config)
{
    RemoteConfig cfg;
    std::string error;
    if (!parseRemoteConfig(config ? config : "", cfg, error)) {
        std::cerr << LOG << error << std::endl;
        return 1;
    }
    return run(cfg, host);
}
