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
#ifndef WELLE_PI_REMOTE_CONFIG_HPP
#define WELLE_PI_REMOTE_CONFIG_HPP

#include <map>
#include <string>

#include "input_plugin.h"

/* One "input" entry of welle-pi.json, see
 * docs/development/welle-pi-input.md. Key codes are the Linux input event
 * codes (linux/input-event-codes.h), the map holds the actions of
 * enum welle_pi_action and -1 for a key that is deliberately ignored. */
struct RemoteConfig {
    std::string device;     // "" = find a device, else /dev/input/eventN
    std::string name;       // "" = any device name, else a case-insensitive part of it
    bool grab = true;       // EVIOCGRAB: keep the keys away from the console
    std::map<int, int> map; // key code -> welle_pi_action, -1 = ignored
};

/* Fills cfg with the defaults for the HiFiBerry remote and applies the JSON
 * object the plugin got as config. Returns false with a message in error if
 * the entry is unusable; cfg then holds the defaults. An empty config is
 * valid. */
bool parseRemoteConfig(const std::string& json, RemoteConfig& cfg, std::string& error);

/* True if the key code is in the map, then action is the welle_pi_action or
 * -1 for a key that is recognised but ignored. False if the key is unknown. */
bool lookupKey(const RemoteConfig& cfg, int code, int& action);

/* "KEY_UP" <-> 103. keyCodeFromName also accepts a decimal number, as printed
 * by evtest and by the plugin's log. nullptr/false if unknown. */
const char* keyName(int code);
bool keyCodeFromName(const std::string& name, int& code);

/* "up", "down", "left", "right", "enter", "menu", "quit", "volume_up",
 * "volume_down", "mute", "home", "none"; nullptr/false if unknown. */
const char* actionName(int action);
bool actionFromName(const std::string& name, int& action);

#endif // WELLE_PI_REMOTE_CONFIG_HPP
