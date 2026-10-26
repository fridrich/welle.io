/******************************************************************************\
 * Description:
 * Key map and configuration of the evdev input plugin: the
 * "input" entry of welle-pi.json, key code -> InputAction
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
#include "remote_config.hpp"

#include <cstdlib>
#include <linux/input-event-codes.h>

#include "libs/json.hpp"

namespace {

struct KeyEntry {
    const char* name;
    int code;
};

/* The keys the defaults use, plus the ones a remote is likely to send instead
 * of them. Enough to write a map without looking up codes in evtest. */
const KeyEntry KEYS[] = {
    {"KEY_UP", KEY_UP},
    {"KEY_DOWN", KEY_DOWN},
    {"KEY_LEFT", KEY_LEFT},
    {"KEY_RIGHT", KEY_RIGHT},
    {"KEY_ENTER", KEY_ENTER},
    {"KEY_KPENTER", KEY_KPENTER},
    {"KEY_OK", KEY_OK},
    {"KEY_SELECT", KEY_SELECT},
    {"KEY_ESC", KEY_ESC},
    {"KEY_HOME", KEY_HOME},
    {"KEY_HOMEPAGE", KEY_HOMEPAGE},
    {"KEY_MENU", KEY_MENU},
    {"KEY_COMPOSE", KEY_COMPOSE},
    {"KEY_BACK", KEY_BACK},
    {"KEY_EXIT", KEY_EXIT},
    {"KEY_POWER", KEY_POWER},
    {"KEY_POWER2", KEY_POWER2},
    {"KEY_SLEEP", KEY_SLEEP},
    {"KEY_WAKEUP", KEY_WAKEUP},
    {"KEY_F19", KEY_F19},
    {"KEY_I", KEY_I},
    {"KEY_C", KEY_C},
    {"KEY_LEFTCTRL", KEY_LEFTCTRL},
    {"KEY_VOLUMEUP", KEY_VOLUMEUP},
    {"KEY_VOLUMEDOWN", KEY_VOLUMEDOWN},
    {"KEY_MUTE", KEY_MUTE},
    {"KEY_MINUS", KEY_MINUS},
    {"KEY_EQUAL", KEY_EQUAL},
    {"KEY_PLAYPAUSE", KEY_PLAYPAUSE},
    {"KEY_PLAY", KEY_PLAY},
    {"KEY_PAUSE", KEY_PAUSE},
    {"KEY_STOP", KEY_STOP},
    {"KEY_NEXTSONG", KEY_NEXTSONG},
    {"KEY_PREVIOUSSONG", KEY_PREVIOUSSONG},
    {"KEY_INFO", KEY_INFO},
};

struct ActionEntry {
    const char* name;
    int action;
};

const ActionEntry ACTIONS[] = {
    {"up", WELLE_PI_UP},
    {"down", WELLE_PI_DOWN},
    {"left", WELLE_PI_LEFT},
    {"right", WELLE_PI_RIGHT},
    {"enter", WELLE_PI_ENTER},
    {"menu", WELLE_PI_MENU},
    {"quit", WELLE_PI_QUIT},
    {"volume_up", WELLE_PI_VOLUME_UP},
    {"volume_down", WELLE_PI_VOLUME_DOWN},
    {"mute", WELLE_PI_MUTE},
    {"home", WELLE_PI_HOME},
    {"none", -1},
};

/* The key codes of the HiFiBerry remote: the wheel and OK are the arrow keys
 * and Enter of a HID keyboard, as HiFiBerry's own audiocontrol2 uses them
 * (ac2/plugins/control/keyboard.py). The other buttons are standard consumer
 * control codes, which is what such a dongle sends; took the ones that a
 * remote of this shape usually has, everything is overridable with "map". */
void setDefaults(RemoteConfig& cfg)
{
    cfg.device.clear();
    cfg.name.clear();
    cfg.grab = true;
    cfg.map.clear();

    /* The wheel and OK are the arrow keys and Enter of a HID keyboard, as
     * HiFiBerry's own audiocontrol2 reads them
     * (ac2/plugins/control/keyboard.py). */
    cfg.map[KEY_UP] = WELLE_PI_UP;
    cfg.map[KEY_DOWN] = WELLE_PI_DOWN;
    cfg.map[KEY_LEFT] = WELLE_PI_LEFT;
    cfg.map[KEY_RIGHT] = WELLE_PI_RIGHT;
    cfg.map[KEY_ENTER] = WELLE_PI_ENTER;
    /* some remotes send one of these instead of KEY_ENTER */
    cfg.map[KEY_KPENTER] = WELLE_PI_ENTER;
    cfg.map[KEY_OK] = WELLE_PI_ENTER;
    cfg.map[KEY_SELECT] = WELLE_PI_ENTER;

    /* The other buttons, from a remote of this shape: home is HOMEPAGE, the
     * menu key is COMPOSE and back is ESC, the codes an Android style remote
     * sends; KEY_MENU, KEY_BACK and KEY_EXIT are what others use. Home returns
     * to the radio screen from wherever the user is, MENU pops one screen and
     * opens the menu on the radio screen, LEFT is what the station list and
     * the menu take as back. */
    cfg.map[KEY_HOMEPAGE] = WELLE_PI_HOME;
    cfg.map[KEY_HOME] = WELLE_PI_HOME;
    cfg.map[KEY_COMPOSE] = WELLE_PI_MENU;
    cfg.map[KEY_MENU] = WELLE_PI_MENU;
    cfg.map[KEY_ESC] = WELLE_PI_LEFT;
    cfg.map[KEY_BACK] = WELLE_PI_LEFT;
    cfg.map[KEY_EXIT] = WELLE_PI_LEFT;

    /* on/off: this remote sends KEY_SLEEP, the code that suspends a PC. The
     * radio stops; with the systemd unit's ALLOW_SHUTDOWN that also powers the
     * Pi off. KEY_POWER is what other remotes send. */
    cfg.map[KEY_SLEEP] = WELLE_PI_QUIT;
    cfg.map[KEY_POWER] = WELLE_PI_QUIT;
    cfg.map[KEY_POWER2] = WELLE_PI_QUIT;
    /* KEY_WAKEUP is the same button after a suspend: by then welle-pi is not
     * running any more, and it must not stop the radio that starts. */
    cfg.map[KEY_WAKEUP] = -1;

    /* The volume and mute buttons turn the mixer of the sound card, see
     * docs/development/welle-pi-volume.md. The button that switches the
     * remote to mouse mode is recognised so that it does not look like an
     * unknown key. */
    cfg.map[KEY_VOLUMEUP] = WELLE_PI_VOLUME_UP;
    cfg.map[KEY_VOLUMEDOWN] = WELLE_PI_VOLUME_DOWN;
    cfg.map[KEY_MUTE] = WELLE_PI_MUTE;
    cfg.map[KEY_F19] = -1;

    /* A remote of the same shape that sends keyboard keys rather than
     * consumer control codes (the OSMC remote sends minus and equal for the
     * volume, and play/pause and stop) - the same buttons as above. */
    cfg.map[KEY_EQUAL] = WELLE_PI_VOLUME_UP;
    cfg.map[KEY_MINUS] = WELLE_PI_VOLUME_DOWN;
    cfg.map[KEY_PLAYPAUSE] = -1;
    cfg.map[KEY_STOP] = -1;

    /* Info and context buttons, which the OSMC remote sends as I and C:
     * welle-pi has neither an info nor a context screen, both open the menu
     * (or "none" in "map" for a button that is to do nothing). */
    cfg.map[KEY_I] = WELLE_PI_MENU;
    cfg.map[KEY_C] = WELLE_PI_MENU;
}

bool getString(const nlohmann::json& cfg, const char* key, std::string& out, std::string& error)
{
    const auto it = cfg.find(key);
    if (it == cfg.end()) {
        return true;
    }
    if (!it->is_string()) {
        error = std::string("\"") + key + "\" must be a string";
        return false;
    }
    out = it->get<std::string>();
    return true;
}

} // namespace

bool parseRemoteConfig(const std::string& json, RemoteConfig& cfg, std::string& error)
{
    setDefaults(cfg);
    error.clear();

    if (json.empty()) {
        return true;
    }

    nlohmann::json entry;
    try {
        entry = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        error = std::string("config: ") + e.what();
        return false;
    }
    if (!entry.is_object()) {
        error = "config must be a JSON object";
        return false;
    }

    if (!getString(entry, "device", cfg.device, error) ||
        !getString(entry, "name", cfg.name, error)) {
        return false;
    }

    const auto grab = entry.find("grab");
    if (grab != entry.end()) {
        if (!grab->is_boolean()) {
            error = "\"grab\" must be true or false";
            return false;
        }
        cfg.grab = grab->get<bool>();
    }

    const auto map = entry.find("map");
    if (map == entry.end()) {
        return true;
    }
    if (!map->is_object()) {
        error = "\"map\" must be an object";
        return false;
    }

    for (auto it = map->begin(); it != map->end(); ++it) {
        int code = 0;
        if (!keyCodeFromName(it.key(), code)) {
            error = "map: unknown key \"" + it.key() + "\"";
            return false;
        }
        if (!it.value().is_string()) {
            error = "map: \"" + it.key() + "\" must be an action name";
            return false;
        }
        const std::string name = it.value().get<std::string>();
        int action = 0;
        if (!actionFromName(name, action)) {
            error = "map: unknown action \"" + name + "\"";
            return false;
        }
        cfg.map[code] = action;
    }

    return true;
}

bool lookupKey(const RemoteConfig& cfg, int code, int& action)
{
    const auto it = cfg.map.find(code);
    if (it == cfg.map.end()) {
        return false;
    }
    action = it->second;
    return true;
}

const char* keyName(int code)
{
    for (const auto& key : KEYS) {
        if (key.code == code) {
            return key.name;
        }
    }
    return nullptr;
}

bool keyCodeFromName(const std::string& name, int& code)
{
    if (name.empty()) {
        return false;
    }
    if (name.find_first_not_of("0123456789") == std::string::npos) {
        const long value = std::strtol(name.c_str(), nullptr, 10);
        if (value < 0 || value > KEY_MAX) {
            return false;
        }
        code = int(value);
        return true;
    }
    for (const auto& key : KEYS) {
        if (name == key.name) {
            code = key.code;
            return true;
        }
    }
    return false;
}

const char* actionName(int action)
{
    for (const auto& a : ACTIONS) {
        if (a.action == action) {
            return a.name;
        }
    }
    return nullptr;
}

bool actionFromName(const std::string& name, int& action)
{
    for (const auto& a : ACTIONS) {
        if (name == a.name) {
            action = a.action;
            return true;
        }
    }
    return false;
}

bool repeatableAction(int action)
{
    /* the steps where a second one is what the user means: the station list
     * scrolls and the volume ramps. A long press on enter, menu, home, mute or
     * quit must not open several screens or stop the radio. */
    return action == WELLE_PI_UP || action == WELLE_PI_DOWN
        || action == WELLE_PI_VOLUME_UP || action == WELLE_PI_VOLUME_DOWN;
}
