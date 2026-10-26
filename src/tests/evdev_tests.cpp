/******************************************************************************\
 * Description:
 * Tests of the evdev key map: defaults, overrides and errors of
 * the plugin's "input" entry. The device handling needs a remote and is not
 * covered here.
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

#include <cassert>
#include <iostream>
#include <linux/input-event-codes.h>
#include <string>

static int actionOf(const RemoteConfig& cfg, int code)
{
    int action = 0;
    assert(lookupKey(cfg, code, action));
    return action;
}

int main()
{
    RemoteConfig cfg;
    std::string error;

    // no config at all: both remotes work out of the box
    assert(parseRemoteConfig("", cfg, error));
    assert(actionOf(cfg, KEY_UP) == WELLE_PI_UP);
    assert(actionOf(cfg, KEY_DOWN) == WELLE_PI_DOWN);
    assert(actionOf(cfg, KEY_LEFT) == WELLE_PI_LEFT);
    assert(actionOf(cfg, KEY_RIGHT) == WELLE_PI_RIGHT);
    assert(actionOf(cfg, KEY_ENTER) == WELLE_PI_ENTER);
    assert(actionOf(cfg, KEY_MENU) == WELLE_PI_MENU);
    assert(actionOf(cfg, KEY_BACK) == WELLE_PI_LEFT);
    assert(actionOf(cfg, KEY_POWER) == WELLE_PI_QUIT);
    assert(cfg.grab && cfg.device.empty() && cfg.name.empty());

    // the codes the remote actually sends: on/off is KEY_SLEEP, home
    // KEY_HOMEPAGE, menu KEY_COMPOSE, back KEY_ESC, mouse mode KEY_F19
    assert(actionOf(cfg, KEY_SLEEP) == WELLE_PI_QUIT);
    assert(actionOf(cfg, KEY_HOMEPAGE) == WELLE_PI_MENU);
    assert(actionOf(cfg, KEY_COMPOSE) == WELLE_PI_MENU);
    assert(actionOf(cfg, KEY_ESC) == WELLE_PI_LEFT);
    assert(actionOf(cfg, KEY_WAKEUP) == -1);        // the same button after resume
    assert(actionOf(cfg, KEY_F19) == -1);           // mouse mode switch
    assert(actionOf(cfg, KEY_VOLUMEUP) == -1);      // known, but no action
    assert(actionOf(cfg, KEY_VOLUMEDOWN) == -1);

    // the OSMC remote, which sends keyboard keys instead: volume on minus and
    // equal, context on C, info on I, and play/pause and stop
    assert(actionOf(cfg, KEY_MINUS) == -1);
    assert(actionOf(cfg, KEY_EQUAL) == -1);
    assert(actionOf(cfg, KEY_STOP) == -1);
    assert(actionOf(cfg, KEY_PLAYPAUSE) == -1);
    assert(actionOf(cfg, KEY_C) == WELLE_PI_MENU);
    assert(actionOf(cfg, KEY_I) == WELLE_PI_MENU);

    int action = 0;
    assert(!lookupKey(cfg, BTN_MOUSE, action));     // clicks in mouse mode
    assert(!lookupKey(cfg, BTN_RIGHT, action));
    assert(!lookupKey(cfg, 42, action));

    // the JSON object of the "input" array, "plugin" is not the plugin's
    // business
    assert(parseRemoteConfig(R"({"plugin":"evdev"})", cfg, error));
    assert(actionOf(cfg, KEY_UP) == WELLE_PI_UP);

    // remap for another remote: by name, by number, disabled, and a default
    // that is not mentioned stays
    assert(parseRemoteConfig(
            R"({"map":{"KEY_POWER":"menu","KEY_SLEEP":"right","108":"none","KEY_ENTER":"up"}})",
            cfg, error));
    assert(actionOf(cfg, KEY_POWER) == WELLE_PI_MENU);
    assert(actionOf(cfg, KEY_SLEEP) == WELLE_PI_RIGHT);
    assert(actionOf(cfg, KEY_DOWN) == -1);
    assert(actionOf(cfg, KEY_ENTER) == WELLE_PI_UP);
    assert(actionOf(cfg, KEY_UP) == WELLE_PI_UP);
    assert(actionOf(cfg, KEY_VOLUMEUP) == -1);

    // device selection
    assert(parseRemoteConfig(
            R"({"device":"/dev/input/event7","name":"remote","grab":false})", cfg, error));
    assert(cfg.device == "/dev/input/event7");
    assert(cfg.name == "remote");
    assert(!cfg.grab);

    // a mistake is reported and leaves the defaults in place
    assert(!parseRemoteConfig(R"({"map":{"KEY_NOSUCH":"up"}})", cfg, error));
    assert(!error.empty());
    assert(actionOf(cfg, KEY_UP) == WELLE_PI_UP);
    assert(!parseRemoteConfig(R"({"map":{"103":"wheee"}})", cfg, error));
    assert(!parseRemoteConfig(R"({"map":{"103":1}})", cfg, error));
    assert(!parseRemoteConfig(R"({"map":[]})", cfg, error));
    assert(!parseRemoteConfig(R"({"grab":"yes"})", cfg, error));
    assert(!parseRemoteConfig(R"({"device":7})", cfg, error));
    assert(!parseRemoteConfig(R"([{"plugin":"evdev"}])", cfg, error));

    // names used in "map" and in the log
    int code = 0;
    assert(keyCodeFromName("KEY_UP", code) && code == KEY_UP);
    assert(keyCodeFromName("103", code) && code == 103);
    assert(!keyCodeFromName("KEY_UP ", code));
    assert(!keyCodeFromName("", code));
    assert(!keyCodeFromName("ups", code));
    assert(!keyCodeFromName("9999", code));
    assert(std::string(keyName(KEY_ENTER)) == "KEY_ENTER");
    assert(keyName(42) == nullptr);
    assert(std::string(actionName(WELLE_PI_QUIT)) == "quit");
    assert(actionName(-1) != nullptr);
    assert(actionName(WELLE_PI_QUIT + 1) == nullptr);

    std::cout << "evdev_tests: OK" << std::endl;
    return 0;
}
